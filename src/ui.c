// ui.c - application state, scanning flow and interactive loop
#include "ui.h"
#include "scan.h"
#include "render.h"
#include "treemap.h"
#include "debug.h"

#include <3ds.h>
#include <citro2d.h>

#include <stdlib.h>
#include <string.h>

// Treemap nesting shown on the top screen, and the smallest folder rectangle
// (in pixels) that is still subdivided into its contents.
#define MAP_DEPTH    4
#define MAP_MIN_AREA 120.0f

// Minimum time between two scan progress frames.
#define SCAN_FRAME_MS 50

// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------

static void add_cat_bytes(const DirEntry *e, uint64_t *acc)
{
    if (!e->is_dir) {
        if (e->category >= 0 && e->category < CAT_COUNT) acc[e->category] += e->size;
        return;
    }
    for (int i = 0; i < e->nchildren; i++)
        add_cat_bytes(e->children[i], acc);
}

static void clamp_scroll(ui_state *s)
{
    int max = s->dir->nchildren - LIST_ROWS;
    if (max < 0) max = 0;
    if (s->scroll > max) s->scroll = max;
    if (s->scroll < 0) s->scroll = 0;
}

static void ensure_visible(ui_state *s)
{
    if (s->sel < 0) return;
    if (s->sel < s->scroll) s->scroll = s->sel;
    if (s->sel >= s->scroll + LIST_ROWS) s->scroll = s->sel - LIST_ROWS + 1;
    clamp_scroll(s);
}

static void enter_dir(ui_state *s, DirEntry *dir, int sel)
{
    s->dir    = dir;
    s->sel    = dir->nchildren > 0 ? sel : -1;
    s->scroll = s->sel - LIST_ROWS / 2;  // keep the selection mid-list
    clamp_scroll(s);
    ensure_visible(s);

    memset(s->catBytes, 0, sizeof s->catBytes);
    add_cat_bytes(dir, s->catBytes);
    layout_tree(dir, MAP_X, MAP_Y, MAP_W, MAP_H, MAP_DEPTH, MAP_MIN_AREA);
}

static void open_selected(ui_state *s)
{
    if (s->sel < 0) return;
    DirEntry *c = s->dir->children[s->sel];
    if (c->is_dir && c->nchildren > 0)
        enter_dir(s, c, 0);
}

static void go_up(ui_state *s)
{
    DirEntry *from = s->dir, *parent = from->parent;
    if (!parent) return;
    int idx = 0;
    for (int i = 0; i < parent->nchildren; i++)
        if (parent->children[i] == from) { idx = i; break; }
    enter_dir(s, parent, idx);
}

static void move_sel(ui_state *s, int delta)
{
    int n = s->dir->nchildren;
    if (n == 0) return;
    int sel = s->sel + delta;
    if (sel < 0) sel = 0;
    if (sel >= n) sel = n - 1;
    s->sel = sel;
    ensure_visible(s);
}

static void tap(ui_state *s, int x, int y)
{
    if (y < BOT_HDR_H) {
        if (!s->legend && x < BACK_BTN_W) go_up(s);
        return;
    }
    if (s->legend || y < LIST_Y || y >= FOOTER_Y) return;
    int idx = s->scroll + (y - LIST_Y) / ROW_H;
    if (idx >= s->dir->nchildren) return;
    if (idx == s->sel) open_selected(s);   // second tap opens
    else               s->sel = idx;
}

static void handle_input(ui_state *s, const Input *in, bool *rescan)
{
    u32 rep = in->repeat;

    if (in->down & KEY_START) s->running = false;
    if (in->down & KEY_X)     *rescan = true;
    if (in->down & KEY_Y)     s->legend = !s->legend;
    if (in->down & KEY_B) {
        if (s->legend) s->legend = false;
        else           go_up(s);
    }
    if (!s->legend) {
        if (rep & KEY_UP)               move_sel(s, -1);
        if (rep & KEY_DOWN)             move_sel(s, +1);
        if (rep & (KEY_LEFT | KEY_L))   move_sel(s, -LIST_ROWS);
        if (rep & (KEY_RIGHT | KEY_R))  move_sel(s, +LIST_ROWS);
        if (in->down & KEY_A)           open_selected(s);
    }

    // Stylus: tap selects (tap again opens), vertical drag scrolls the list.
    if (in->held & KEY_TOUCH) {
        int x = in->touch.px, y = in->touch.py;
        if (!s->touching) {
            s->touching = true;
            s->dragged  = false;
            s->touchY0  = y;
            s->scroll0  = s->scroll;
        }
        s->touchX = x;
        s->touchY = y;
        int dy = y - s->touchY0;
        if (!s->legend && s->touchY0 >= LIST_Y && s->touchY0 < FOOTER_Y &&
            (s->dragged || abs(dy) > 6)) {
            s->dragged = true;
            s->scroll  = s->scroll0 - dy / ROW_H;
            clamp_scroll(s);
        }
    } else if (s->touching) {
        s->touching = false;
        if (!s->dragged) tap(s, s->touchX, s->touchY);
    }
}

// ---------------------------------------------------------------------------
// Scanning
// ---------------------------------------------------------------------------

typedef struct {
    u64  start, last;
    bool quit;          // the system asked the app to close
} ScanUi;

static bool on_progress(const ScanStatus *st, void *userdata)
{
    ScanUi *u = userdata;
    u64 now = osGetTime();
    if (u->last != 0 && now - u->last < SCAN_FRAME_MS) return false;
    u->last = now;

    if (!aptMainLoop()) { u->quit = true; return true; }
    hidScanInput();
    if (hidKeysDown() & KEY_SELECT) return true;

    render_scan(st, now - u->start);
    dbg_scan_frame_done();
    return false;
}

// Scan into a new tree. The previous tree is released first so a rescan never
// needs memory for two trees at once.
static int do_scan(ui_state *s, int maxDepth, bool *quit)
{
    if (s->tree) {
        tree_free(s->tree);
        free(s->tree);
        s->tree = NULL;
        s->dir  = NULL;
    }
    Tree *t = calloc(1, sizeof(Tree));
    if (!t || tree_init(t) != 0) { free(t); return -3; }

    ScanUi u = { osGetTime(), 0, false };
    int rc = scan_sd(t, maxDepth, on_progress, &u);
    *quit = u.quit;
    if (rc != 0) {
        tree_free(t);
        free(t);
        return rc;
    }
    s->tree   = t;
    s->legend = false;
    enter_dir(s, t->root, 0);
    return 0;
}

// Show why the scan stopped. Returns true to scan again, false to exit.
static bool scan_failed(int rc)
{
    const char *body = rc == -2 ? "The scan was cancelled." :
                       rc == -3 ? "Not enough memory to scan this card." :
                                  "Could not read the SD card.";
    while (aptMainLoop()) {
        render_message("Scan stopped", body, "A  Scan again          START  Exit");
        hidScanInput();
        u32 k = hidKeysDown();
        if (k & KEY_A) return true;
        if (k & (KEY_START | KEY_B)) return false;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------

int ui_run(int maxDepth)
{
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS * 2);
    C2D_Prepare();
    fontEnsureMapped();
    render_init();
    dbg_init();
    hidSetRepeatParameters(18, 4);

    ui_state s;
    memset(&s, 0, sizeof s);
    s.running = true;
    bool rescan = true;

    while (s.running && aptMainLoop() && !dbg_quit()) {
        if (rescan) {
            rescan = false;
            bool quit = false;
            int rc = do_scan(&s, maxDepth, &quit);
            if (quit) break;
            if (rc != 0) {
                if (!scan_failed(rc)) break;
                rescan = true;
                continue;
            }
        }

        Input in;
        hidScanInput();
        in.down   = hidKeysDown();
        in.held   = hidKeysHeld();
        in.repeat = hidKeysDownRepeat();
        hidTouchRead(&in.touch);
        dbg_input(&in);

        handle_input(&s, &in, &rescan);
        if (!rescan) {
            render_frame(&s);
            dbg_frame_done();
        }
    }

    if (s.tree) {
        tree_free(s.tree);
        free(s.tree);
    }
    render_fini();
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
