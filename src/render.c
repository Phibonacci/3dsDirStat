// render.c - screen drawing with citro2d
//
// Colours: citro2d packs a colour as 0xAABBGGRR (C2D_Color32(r, g, b, a)), so
// the 0xRRGGBB values used here and in colors.c go through HEX()/shade().
#include "render.h"
#include "treemap.h"
#include "colors.h"
#include "sizes.h"

#include <3ds.h>
#include <citro2d.h>

#include <stdio.h>
#include <string.h>

#define RGBA(r, g, b, a) ((u32)(r) | ((u32)(g) << 8) | ((u32)(b) << 16) | ((u32)(a) << 24))
#define HEX(h)           RGBA(((h) >> 16) & 0xFF, ((h) >> 8) & 0xFF, (h) & 0xFF, 0xFF)

// Palette (dark theme).
#define C_BG        HEX(0x0B1120)
#define C_ROW_ALT   HEX(0x111827)
#define C_PANEL     HEX(0x1E293B)
#define C_BUTTON    HEX(0x334155)
#define C_TEXT      HEX(0xF1F5F9)
#define C_TEXT_DIM  HEX(0x94A3B8)
#define C_TEXT_MUTE HEX(0x64748B)
#define C_ACCENT    HEX(0x2563EB)
#define C_FOLDER    HEX(0xCBD5E1)
#define C_WHITE     HEX(0xFFFFFF)
#define C_BLACK     HEX(0x000000)
#define C_LABEL_BG  RGBA(0, 0, 0, 160)

// Text scales.
#define SC_TITLE  1.0f
#define SC_NORMAL 0.5f
#define SC_SMALL  0.45f
#define SC_TINY   0.42f

static C3D_RenderTarget *g_top, *g_bot;
static C2D_TextBuf       g_txt_top, g_txt_bot, g_meas;

void render_init(void)
{
    g_top     = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    g_bot     = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    g_txt_top = C2D_TextBufNew(4096);
    g_txt_bot = C2D_TextBufNew(4096);
    g_meas    = C2D_TextBufNew(1024);
}

void render_fini(void)
{
    C2D_TextBufDelete(g_txt_top);
    C2D_TextBufDelete(g_txt_bot);
    C2D_TextBufDelete(g_meas);
}

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

// Lighten (f > 1) towards white or darken (f < 1) a 0xRRGGBB colour.
static u32 shade(u32 rgb, float f)
{
    int c[3] = { (int)((rgb >> 16) & 0xFF), (int)((rgb >> 8) & 0xFF), (int)(rgb & 0xFF) };
    for (int i = 0; i < 3; i++) {
        float v = f >= 1.0f ? c[i] + (255 - c[i]) * (f - 1.0f) : c[i] * f;
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        c[i] = (int)v;
    }
    return RGBA(c[0], c[1], c[2], 0xFF);
}

static float line_h(float sc)
{
    return C2D_FontGetInfo(NULL)->lineFeed * sc;
}

static float text_w(const char *s, float sc)
{
    C2D_TextBufClear(g_meas);
    C2D_Text t;
    if (!C2D_TextParse(&t, g_meas, s)) return 0.0f;
    float w = 0.0f;
    C2D_TextGetDimensions(&t, sc, sc, &w, NULL);
    return w;
}

// Copy `src` into `out`, shortened with "..." so it is at most `maxW` pixels
// wide at scale `sc`. `keepTail` keeps the end of the string (for paths).
static void fit(const char *src, float maxW, float sc, bool keepTail, char *out, size_t n)
{
    if (n == 0) return;
    snprintf(out, n, "%s", src);
    if (text_w(out, sc) <= maxW) return;

    // Byte offset of every UTF-8 character start, so cuts never split one.
    int starts[512];
    int nch = 0;
    for (int i = 0; src[i] && nch < 511; i++)
        if (((unsigned char)src[i] & 0xC0) != 0x80) starts[nch++] = i;
    starts[nch] = (int)strlen(src);

    // Largest k (characters kept) whose shortened string still fits.
    int lo = 0, hi = nch;
    while (lo < hi) {
        int k = (lo + hi + 1) / 2;
        char tmp[600];
        if (keepTail) snprintf(tmp, sizeof tmp, "...%s", src + starts[nch - k]);
        else          snprintf(tmp, sizeof tmp, "%.*s...", starts[k], src);
        if (text_w(tmp, sc) <= maxW) lo = k; else hi = k - 1;
    }
    if (keepTail) snprintf(out, n, "...%s", src + starts[nch - lo]);
    else          snprintf(out, n, "%.*s...", starts[lo], src);
}

// Draw a string. `flags` may add C2D_AlignRight / C2D_AlignCenter, in which
// case `x` is the right edge / the centre.
static void text(C2D_TextBuf buf, const char *s, float x, float y, float sc, u32 col, u32 flags)
{
    C2D_Text t;
    if (!C2D_TextParse(&t, buf, s)) return;
    C2D_TextOptimize(&t);
    C2D_DrawText(&t, C2D_WithColor | flags, x, y, 0.0f, sc, sc, col);
}

// Draw `s` vertically centred in the band [y, y + h).
static void text_mid(C2D_TextBuf buf, const char *s, float x, float y, float h,
                     float sc, u32 col, u32 flags)
{
    text(buf, s, x, y + (h - line_h(sc)) / 2.0f, sc, col, flags);
}

// Outline of thickness `t` drawn inside the rectangle.
static void frame(float x, float y, float w, float h, float t, u32 col)
{
    C2D_DrawRectSolid(x, y, 0, w, t, col);
    C2D_DrawRectSolid(x, y + h - t, 0, w, t, col);
    C2D_DrawRectSolid(x, y, 0, t, h, col);
    C2D_DrawRectSolid(x + w - t, y, 0, t, h, col);
}

// 1234567 -> "1,234,567"
static void fmt_count(uint64_t v, char *out, size_t n)
{
    char tmp[32];
    int len = snprintf(tmp, sizeof tmp, "%llu", (unsigned long long)v);
    size_t o = 0;
    for (int i = 0; i < len && o + 1 < n; i++) {
        if (i > 0 && (len - i) % 3 == 0 && o + 1 < n) out[o++] = ',';
        if (o + 1 < n) out[o++] = tmp[i];
    }
    out[o] = '\0';
}

static void fmt_pct(uint64_t part, uint64_t whole, char *out, size_t n)
{
    double p = whole ? 100.0 * (double)part / (double)whole : 0.0;
    if (p >= 10.0 || p == 0.0) snprintf(out, n, "%.0f%%", p);
    else if (p >= 0.1)         snprintf(out, n, "%.1f%%", p);
    else                       snprintf(out, n, "<0.1%%");
}

static void folder_icon(float x, float y, u32 col)
{
    C2D_DrawRectSolid(x, y, 0, 5, 2, col);
    C2D_DrawRectSolid(x, y + 2, 0, 12, 8, col);
}

static void file_icon(float x, float y, u32 col)
{
    C2D_DrawRectSolid(x + 1, y, 0, 10, 10, col);
}

static void entry_icon(const DirEntry *e, float x, float y)
{
    if (e->is_dir) folder_icon(x, y, C_FOLDER);
    else           file_icon(x, y, HEX(category_color(e->category)));
}

// "SD:/a/b" for a folder.
static void path_of(const DirEntry *dir, char *out, size_t n)
{
    const DirEntry *chain[64];
    int depth = 0;
    for (const DirEntry *e = dir; e && e->parent && depth < 64; e = e->parent)
        chain[depth++] = e;
    size_t o = (size_t)snprintf(out, n, "SD:");
    if (depth == 0 && o < n) snprintf(out + o, n - o, "/");
    for (int i = depth - 1; i >= 0 && o < n; i--) {
        int w = snprintf(out + o, n - o, "/%s", chain[i]->name);
        if (w < 0) break;
        o += (size_t)w;
    }
}

// ---------------------------------------------------------------------------
// Top screen: treemap
// ---------------------------------------------------------------------------

// Leaf cells get a "cushion" gradient (light top-left, dark bottom-right) and a
// 1 px gap on their right/bottom edge so neighbouring blocks stay distinct.
static void draw_cell(const DirEntry *e)
{
    if (e->expanded) {
        for (int i = 0; i < e->nchildren; i++)
            draw_cell(e->children[i]);
        return;
    }
    int x0 = (int)(e->rx + 0.5f), y0 = (int)(e->ry + 0.5f);
    int x1 = (int)(e->rx + e->rw + 0.5f), y1 = (int)(e->ry + e->rh + 0.5f);
    int w = x1 - x0, h = y1 - y0;
    if (w <= 0 || h <= 0) return;
    if (w >= 3) w--;
    if (h >= 3) h--;
    u32 base = category_color(e->category);
    C2D_DrawRectangle(x0, y0, 0, w, h,
                      shade(base, 1.35f), shade(base, 1.0f),
                      shade(base, 1.0f), shade(base, 0.55f));
}

static void draw_labels(const DirEntry *dir)
{
    const float sc = SC_TINY, lh = line_h(sc);
    for (int i = 0; i < dir->nchildren; i++) {
        const DirEntry *c = dir->children[i];
        if (c->rw < 34.0f || c->rh < lh + 4.0f) continue;
        bool twoLines = c->rh >= 2.0f * lh + 8.0f;
        float stripH = twoLines ? 2.0f * lh + 2.0f : lh + 1.0f;
        float x = c->rx + 1.0f, y = c->ry + 1.0f, w = c->rw - 3.0f;

        char name[160], buf[160];
        snprintf(buf, sizeof buf, "%s%s", c->name, c->is_dir ? "/" : "");
        fit(buf, w - 5.0f, sc, false, name, sizeof name);
        float tw = text_w(name, sc);
        char sz[32] = "";
        float sw = 0.0f;
        if (twoLines) {
            format_size(c->size, sz, sizeof sz);
            sw = text_w(sz, sc);
        }
        float bw = (tw > sw ? tw : sw) + 6.0f;
        if (bw > w) bw = w;
        C2D_DrawRectSolid(x, y, 0, bw, stripH, C_LABEL_BG);
        text(g_txt_top, name, x + 3.0f, y, sc, C_WHITE, 0);
        if (twoLines)
            text(g_txt_top, sz, x + 3.0f, y + lh, sc, C_TEXT_DIM, 0);
    }
}

static void render_top(const ui_state *s)
{
    const DirEntry *dir = s->dir;
    C2D_DrawRectSolid(0, 0, 0, TOP_W, TOP_H, C_BG);

    // Header: path on the left, folder size on the right.
    C2D_DrawRectSolid(0, 0, 0, TOP_W, TOP_HDR_H, C_PANEL);
    char right[64], sz[32], pct[16];
    format_size(dir->size, sz, sizeof sz);
    if (dir != s->tree->root) {
        fmt_pct(dir->size, s->tree->root->size, pct, sizeof pct);
        snprintf(right, sizeof right, "%s  (%s of SD)", sz, pct);
    } else {
        snprintf(right, sizeof right, "%s", sz);
    }
    float rw = text_w(right, SC_SMALL);
    text_mid(g_txt_top, right, TOP_W - 5.0f, 0, TOP_HDR_H, SC_SMALL, C_TEXT, C2D_AlignRight);

    char path[512], shown[512];
    path_of(dir, path, sizeof path);
    fit(path, TOP_W - rw - 20.0f, SC_SMALL, true, shown, sizeof shown);
    text_mid(g_txt_top, shown, 5.0f, 0, TOP_HDR_H, SC_SMALL, C_TEXT, 0);

    if (dir->size == 0) {
        text(g_txt_top, dir->nchildren ? "Only empty files here" : "This folder is empty",
             TOP_W / 2.0f, TOP_H / 2.0f - 10.0f, SC_NORMAL, C_TEXT_DIM, C2D_AlignCenter);
        return;
    }

    for (int i = 0; i < dir->nchildren; i++)
        draw_cell(dir->children[i]);
    draw_labels(dir);

    // Selection: white outline with a dark edge so it shows on any colour.
    if (s->sel >= 0 && s->sel < dir->nchildren && dir->children[s->sel]->size > 0) {
        const DirEntry *c = dir->children[s->sel];
        float x = c->rx - 1.0f, y = c->ry - 1.0f, w = c->rw + 1.0f, h = c->rh + 1.0f;
        if (x < 0) { w += x; x = 0; }
        if (y < TOP_HDR_H) { h -= TOP_HDR_H - y; y = TOP_HDR_H; }
        if (x + w > TOP_W) w = TOP_W - x;
        if (y + h > TOP_H) h = TOP_H - y;
        if (w >= 4.0f && h >= 4.0f) {
            frame(x, y, w, h, 3.0f, C_BLACK);
            frame(x + 1.0f, y + 1.0f, w - 2.0f, h - 2.0f, 2.0f, C_WHITE);
        } else {
            // Too small to outline: mark it with a crosshair-style box.
            frame(x - 3.0f, y - 3.0f, w + 6.0f, h + 6.0f, 2.0f, C_WHITE);
        }
    }
}

// ---------------------------------------------------------------------------
// Bottom screen: list / legend
// ---------------------------------------------------------------------------

static void key_hints(const char *const *pairs, int n)
{
    // pairs: key, label, key, label, ...
    float x = 6.0f, y = FOOTER_Y + 21.0f, h = BOT_H - y;
    for (int i = 0; i + 1 < n; i += 2) {
        text_mid(g_txt_bot, pairs[i], x, y, h, SC_TINY, C_TEXT, 0);
        x += text_w(pairs[i], SC_TINY) + 3.0f;
        text_mid(g_txt_bot, pairs[i + 1], x, y, h, SC_TINY, C_TEXT_MUTE, 0);
        x += text_w(pairs[i + 1], SC_TINY) + 10.0f;
    }
}

static void bottom_header(const ui_state *s, const char *title, const char *right)
{
    C2D_DrawRectSolid(0, 0, 0, BOT_W, BOT_HDR_H, C_PANEL);
    float x = 6.0f;
    if (s->dir != s->tree->root && !s->legend) {
        // "Up" button (tap target), mirrors the B button.
        C2D_DrawRectSolid(2, 2, 0, BACK_BTN_W - 4, BOT_HDR_H - 4, C_BUTTON);
        C2D_DrawTriangle(8, 15, C_TEXT, 16, 15, C_TEXT, 12, 7, C_TEXT, 0);
        text_mid(g_txt_bot, "Up", 19.0f, 0, BOT_HDR_H, SC_SMALL, C_TEXT, 0);
        x = BACK_BTN_W + 4.0f;
    }
    float rw = right ? text_w(right, SC_SMALL) : 0.0f;
    if (right)
        text_mid(g_txt_bot, right, BOT_W - 6.0f, 0, BOT_HDR_H, SC_SMALL, C_TEXT_DIM, C2D_AlignRight);
    char t[256];
    fit(title, BOT_W - x - rw - 14.0f, SC_NORMAL, false, t, sizeof t);
    text_mid(g_txt_bot, t, x, 0, BOT_HDR_H, SC_NORMAL, C_TEXT, 0);
}

static void render_list(const ui_state *s)
{
    const DirEntry *dir = s->dir;
    int n = dir->nchildren;

    char right[32], cnt[24];
    fmt_count((uint64_t)n, cnt, sizeof cnt);
    snprintf(right, sizeof right, "%s item%s", cnt, n == 1 ? "" : "s");
    bottom_header(s, dir == s->tree->root ? "SD card" : dir->name, right);

    if (n == 0) {
        text(g_txt_bot, "This folder is empty", BOT_W / 2.0f, LIST_Y + 70.0f,
             SC_NORMAL, C_TEXT_DIM, C2D_AlignCenter);
    }

    bool scrollbar = n > LIST_ROWS;
    float listW = BOT_W - (scrollbar ? 6.0f : 0.0f);
    for (int r = 0; r < LIST_ROWS; r++) {
        int i = s->scroll + r;
        if (i >= n) break;
        const DirEntry *e = dir->children[i];
        float y = LIST_Y + r * ROW_H;
        bool sel = i == s->sel;

        C2D_DrawRectSolid(0, y, 0, listW, ROW_H, sel ? C_ACCENT : (i & 1) ? C_ROW_ALT : C_BG);
        // Share of the folder, as a bar along the bottom of the row.
        float frac = dir->size ? (float)((double)e->size / (double)dir->size) : 0.0f;
        if (frac > 0.0f) {
            float bw = listW * frac;
            if (bw < 1.0f) bw = 1.0f;
            C2D_DrawRectSolid(0, y + ROW_H - 3, 0, bw, 2,
                              HEX(category_color(e->category)));
        }
        entry_icon(e, 5.0f, y + 3.0f);

        char sz[32];
        format_size(e->size, sz, sizeof sz);
        float sw = text_w(sz, SC_SMALL);
        text_mid(g_txt_bot, sz, listW - 5.0f, y, ROW_H - 2, SC_SMALL,
                 sel ? C_WHITE : C_TEXT_DIM, C2D_AlignRight);

        char name[256];
        fit(e->name, listW - 22.0f - sw - 12.0f, SC_NORMAL, false, name, sizeof name);
        text_mid(g_txt_bot, name, 22.0f, y, ROW_H - 2, SC_NORMAL, C_TEXT, 0);
    }

    if (scrollbar) {
        float trackH = ROW_H * LIST_ROWS;
        float th = trackH * LIST_ROWS / n;
        if (th < 10.0f) th = 10.0f;
        float ty = LIST_Y + (trackH - th) * s->scroll / (float)(n - LIST_ROWS);
        C2D_DrawRectSolid(BOT_W - 5, LIST_Y, 0, 5, trackH, C_ROW_ALT);
        C2D_DrawRectSolid(BOT_W - 4, ty, 0, 3, th, C_TEXT_MUTE);
    }

    // Footer: details of the selection + key hints.
    C2D_DrawRectSolid(0, FOOTER_Y, 0, BOT_W, BOT_H - FOOTER_Y, C_PANEL);
    if (s->sel >= 0 && s->sel < n) {
        const DirEntry *e = dir->children[s->sel];
        char info[160], pct[16], files[24];
        fmt_pct(e->size, dir->size, pct, sizeof pct);
        if (e->is_dir) {
            fmt_count(e->nfiles, files, sizeof files);
            snprintf(info, sizeof info, "Folder  -  %s file%s  -  %s of this folder",
                     files, e->nfiles == 1 ? "" : "s", pct);
        } else {
            snprintf(info, sizeof info, "%s  -  %s of this folder",
                     category_name(e->category), pct);
        }
        char shown[160];
        fit(info, BOT_W - 12.0f, SC_TINY, false, shown, sizeof shown);
        text_mid(g_txt_bot, shown, 6.0f, FOOTER_Y + 1.0f, 20.0f, SC_TINY, C_TEXT, 0);
    }
    bool canOpen = s->sel >= 0 && s->sel < n && dir->children[s->sel]->is_dir &&
                   dir->children[s->sel]->nchildren > 0;
    const char *hints[] = { "A", "Open", "B", "Up", "Y", "Types",
                            "X", "Rescan", "START", "Exit" };
    if (canOpen) key_hints(hints, 10);
    else         key_hints(hints + 2, 8);
}

static void render_legend(const ui_state *s)
{
    const DirEntry *dir = s->dir;
    bottom_header(s, "File types", dir == s->tree->root ? "whole SD card" : "in this folder");

    int order[CAT_COUNT], n = 0;
    for (int c = 0; c < CAT_COUNT; c++)
        if (s->catBytes[c] > 0) order[n++] = c;
    for (int i = 1; i < n; i++)                // insertion sort, largest first
        for (int j = i; j > 0 && s->catBytes[order[j]] > s->catBytes[order[j - 1]]; j--) {
            int t = order[j]; order[j] = order[j - 1]; order[j - 1] = t;
        }

    const float rowH = 16.0f;
    uint64_t total = dir->size;
    for (int i = 0; i < n; i++) {
        int c = order[i];
        float y = LIST_Y + 2.0f + i * rowH;
        C2D_DrawRectSolid(0, y, 0, BOT_W, rowH, (i & 1) ? C_ROW_ALT : C_BG);
        float frac = total ? (float)((double)s->catBytes[c] / (double)total) : 0.0f;
        C2D_DrawRectSolid(0, y + rowH - 3, 0, BOT_W * frac < 1 ? 1 : BOT_W * frac, 2,
                          HEX(category_color(c)));
        file_icon(5.0f, y + 2.0f, HEX(category_color(c)));
        text_mid(g_txt_bot, category_name(c), 22.0f, y, rowH - 2, SC_SMALL, C_TEXT, 0);
        char sz[32], pct[16], right[64];
        format_size(s->catBytes[c], sz, sizeof sz);
        fmt_pct(s->catBytes[c], total, pct, sizeof pct);
        snprintf(right, sizeof right, "%s   %s", sz, pct);
        text_mid(g_txt_bot, right, BOT_W - 6.0f, y, rowH - 2, SC_SMALL, C_TEXT_DIM, C2D_AlignRight);
    }
    if (n == 0)
        text(g_txt_bot, "No files here", BOT_W / 2.0f, LIST_Y + 70.0f,
             SC_NORMAL, C_TEXT_DIM, C2D_AlignCenter);

    C2D_DrawRectSolid(0, FOOTER_Y, 0, BOT_W, BOT_H - FOOTER_Y, C_PANEL);
    text_mid(g_txt_bot, "Colours match the map on the top screen.", 6.0f,
             FOOTER_Y + 1.0f, 20.0f, SC_TINY, C_TEXT, 0);
    const char *hints[] = { "Y", "Back to list", "START", "Exit" };
    key_hints(hints, 4);
}

void render_frame(const ui_state *s)
{
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

    C2D_TargetClear(g_top, C_BG);
    C2D_SceneBegin(g_top);
    C2D_TextBufClear(g_txt_top);
    render_top(s);

    C2D_TargetClear(g_bot, C_BG);
    C2D_SceneBegin(g_bot);
    C2D_TextBufClear(g_txt_bot);
    if (s->legend) render_legend(s);
    else           render_list(s);

    C3D_FrameEnd(0);
}

// ---------------------------------------------------------------------------
// Scan progress and messages
// ---------------------------------------------------------------------------

static void top_title(const char *subtitle)
{
    C2D_DrawRectSolid(0, 0, 0, TOP_W, TOP_H, C_BG);
    // A few decorative blocks in the category colours.
    static const int cats[] = { CAT_3DS, CAT_CIA, CAT_IMG, CAT_AUD, CAT_VID, CAT_ARCH };
    static const float wts[] = { 0.30f, 0.22f, 0.16f, 0.13f, 0.11f, 0.08f };
    float x = 40.0f;
    for (int i = 0; i < 6; i++) {
        float w = 320.0f * wts[i];
        u32 base = category_color(cats[i]);
        C2D_DrawRectangle(x, 150, 0, w - 2, 30, shade(base, 1.2f), shade(base, 1.2f),
                          shade(base, 0.7f), shade(base, 0.7f));
        x += w;
    }
    text(g_txt_top, "3dsDirStat", TOP_W / 2.0f, 70.0f, SC_TITLE, C_TEXT, C2D_AlignCenter);
    text(g_txt_top, subtitle, TOP_W / 2.0f, 110.0f, SC_NORMAL, C_TEXT_DIM, C2D_AlignCenter);
}

void render_scan(const ScanStatus *st, uint64_t ms)
{
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

    C2D_TargetClear(g_top, C_BG);
    C2D_SceneBegin(g_top);
    C2D_TextBufClear(g_txt_top);
    top_title("Disk usage of your SD card");

    C2D_TargetClear(g_bot, C_BG);
    C2D_SceneBegin(g_bot);
    C2D_TextBufClear(g_txt_bot);
    C2D_DrawRectSolid(0, 0, 0, BOT_W, BOT_H, C_BG);
    text(g_txt_bot, "Scanning SD card...", 16.0f, 16.0f, 0.6f, C_TEXT, 0);

    char a[32], b[32], line[96];
    fmt_count(st->files, a, sizeof a);
    fmt_count(st->dirs, b, sizeof b);
    snprintf(line, sizeof line, "%s files in %s folders", a, b);
    text(g_txt_bot, line, 16.0f, 50.0f, SC_NORMAL, C_TEXT, 0);
    format_size(st->bytes, a, sizeof a);
    snprintf(line, sizeof line, "%s found so far", a);
    text(g_txt_bot, line, 16.0f, 70.0f, SC_NORMAL, C_TEXT_DIM, 0);

    char name[160];
    fit(st->name ? st->name : "", BOT_W - 32.0f, SC_SMALL, false, name, sizeof name);
    text(g_txt_bot, name, 16.0f, 96.0f, SC_SMALL, C_TEXT_MUTE, 0);

    // Indeterminate progress bar: a block sliding along the track.
    const float tx = 16.0f, tw = BOT_W - 32.0f, ty = 126.0f, bw = 70.0f;
    C2D_DrawRectSolid(tx, ty, 0, tw, 8, C_PANEL);
    float pos = (float)(ms % 1600) / 1600.0f * (tw + bw) - bw;
    float x0 = pos < 0 ? 0 : pos, x1 = pos + bw > tw ? tw : pos + bw;
    if (x1 > x0) C2D_DrawRectSolid(tx + x0, ty, 0, x1 - x0, 8, C_ACCENT);

    C2D_DrawRectSolid(0, FOOTER_Y, 0, BOT_W, BOT_H - FOOTER_Y, C_PANEL);
    text_mid(g_txt_bot, "Large cards can take a few minutes.", 6.0f, FOOTER_Y + 1.0f,
             20.0f, SC_TINY, C_TEXT, 0);
    const char *hints[] = { "SELECT", "Cancel" };
    key_hints(hints, 2);

    C3D_FrameEnd(0);
}

void render_message(const char *title, const char *body, const char *hint)
{
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

    C2D_TargetClear(g_top, C_BG);
    C2D_SceneBegin(g_top);
    C2D_TextBufClear(g_txt_top);
    top_title(title);

    C2D_TargetClear(g_bot, C_BG);
    C2D_SceneBegin(g_bot);
    C2D_TextBufClear(g_txt_bot);
    C2D_DrawRectSolid(0, 0, 0, BOT_W, BOT_H, C_BG);
    text(g_txt_bot, body, BOT_W / 2.0f, 80.0f, SC_NORMAL, C_TEXT, C2D_AlignCenter);
    C2D_DrawRectSolid(0, FOOTER_Y, 0, BOT_W, BOT_H - FOOTER_Y, C_PANEL);
    text_mid(g_txt_bot, hint, BOT_W / 2.0f, FOOTER_Y, BOT_H - FOOTER_Y, SC_SMALL,
             C_TEXT, C2D_AlignCenter);

    C3D_FrameEnd(0);
}
