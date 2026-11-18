// debug.c - scripted input + LCD screenshots for SCRIPT builds (see debug.h)
#ifdef SCRIPT
#include "debug.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CMDS 256

typedef enum { CMD_KEY, CMD_TAP, CMD_SHOT, CMD_QUIT } CmdType;

typedef struct {
    int     frame;
    CmdType type;
    u32     key;
    int     x, y;
    char    name[32];
} Cmd;

static Cmd  g_cmds[MAX_CMDS];
static int  g_ncmds;
static int  g_frame;
static bool g_quit;
static char g_scanshot[32];
static int  g_scanframes;
static int  g_tap_release = -1; // frame on which a scripted tap is released
static bool g_scancancel;

static u32 key_by_name(const char *s)
{
    static const struct { const char *n; u32 k; } keys[] = {
        {"A", KEY_A}, {"B", KEY_B}, {"X", KEY_X}, {"Y", KEY_Y},
        {"L", KEY_L}, {"R", KEY_R}, {"UP", KEY_DUP}, {"DOWN", KEY_DDOWN},
        {"LEFT", KEY_DLEFT}, {"RIGHT", KEY_DRIGHT}, {"START", KEY_START},
        {"SELECT", KEY_SELECT},
    };
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++)
        if (strcmp(s, keys[i].n) == 0) return keys[i].k;
    return 0;
}

void dbg_init(void)
{
    archiveMountSdmc(); // usually already mounted by libctru; harmless if so
    FILE *f = fopen("sdmc:/logs/script.txt", "r");
    if (!f) return;
    char line[128];
    while (g_ncmds < MAX_CMDS && fgets(line, sizeof line, f)) {
        Cmd c;
        memset(&c, 0, sizeof c);
        char verb[16] = "", a[32] = "";
        if (sscanf(line, "scanshot %31s", g_scanshot) == 1) continue;
        if (strncmp(line, "scancancel", 10) == 0) { g_scancancel = true; continue; }
        int n = sscanf(line, "%d %15s %31s %d", &c.frame, verb, a, &c.y);
        if (n < 2) continue;
        if (strcmp(verb, "key") == 0 && n >= 3) {
            c.type = CMD_KEY;
            c.key  = key_by_name(a);
        } else if (strcmp(verb, "tap") == 0 && n >= 4) {
            c.type = CMD_TAP;
            c.x    = atoi(a);
        } else if (strcmp(verb, "shot") == 0 && n >= 3) {
            c.type = CMD_SHOT;
            snprintf(c.name, sizeof c.name, "%s", a);
        } else if (strcmp(verb, "quit") == 0) {
            c.type = CMD_QUIT;
        } else {
            continue;
        }
        g_cmds[g_ncmds++] = c;
    }
    fclose(f);
}

void dbg_input(Input *in)
{
    if (g_tap_release == g_frame) {
        // Release: the stylus is lifted; keep the last position.
        in->held &= ~KEY_TOUCH;
        g_tap_release = -1;
    }
    for (int i = 0; i < g_ncmds; i++) {
        Cmd *c = &g_cmds[i];
        if (c->frame != g_frame) continue;
        if (c->type == CMD_KEY) {
            in->down   |= c->key;
            in->repeat |= c->key;
        } else if (c->type == CMD_TAP) {
            in->down  |= KEY_TOUCH;
            in->held  |= KEY_TOUCH;
            in->touch.px = (u16)c->x;
            in->touch.py = (u16)c->y;
            g_tap_release = g_frame + 1;
        } else if (c->type == CMD_QUIT) {
            g_quit = true;
        }
    }
    if (g_tap_release == g_frame + 1) {
        // Keep reporting the scripted touch position while it is held.
        for (int i = 0; i < g_ncmds; i++)
            if (g_cmds[i].type == CMD_TAP && g_cmds[i].frame == g_frame) {
                in->touch.px = (u16)g_cmds[i].x;
                in->touch.py = (u16)g_cmds[i].y;
            }
    }
}

// Write one LCD as an upright binary PPM. The framebuffers are stored rotated:
// `fw` (240) is the screen height and `fh` the screen width, column-major from
// the bottom-left corner.
static void dump_screen(gfxScreen_t scr, const char *path)
{
    u16 fw = 0, fh = 0;
    u8 *fb = gfxGetFramebuffer(scr, GFX_LEFT, &fw, &fh);
    if (!fb) return;
    GSPGPU_FramebufferFormat fmt = gfxGetScreenFormat(scr);
    int bpp = (int)gspGetBytesPerPixel(fmt);
    GSPGPU_InvalidateDataCache(fb, (u32)fw * fh * bpp);

    int W = fh, H = fw;
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    u8 *row = malloc((size_t)W * 3);
    if (!row) { fclose(f); return; }
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            const u8 *p = fb + ((size_t)x * fw + (size_t)(fw - 1 - y)) * bpp;
            u8 r = 0, g = 0, b = 0;
            switch (fmt) {
            case GSP_RGBA8_OES:  r = p[3]; g = p[2]; b = p[1]; break;
            case GSP_BGR8_OES:   r = p[2]; g = p[1]; b = p[0]; break;
            case GSP_RGB565_OES: {
                u16 v = (u16)(p[0] | (p[1] << 8));
                r = (u8)(((v >> 11) & 31) << 3);
                g = (u8)(((v >> 5) & 63) << 2);
                b = (u8)((v & 31) << 3);
                break;
            }
            default: break;
            }
            row[x * 3 + 0] = r; row[x * 3 + 1] = g; row[x * 3 + 2] = b;
        }
        fwrite(row, 1, (size_t)W * 3, f);
    }
    free(row);
    fclose(f);
}

static void shot(const char *name)
{
    // Let the frame just submitted reach the LCD buffers.
    gspWaitForVBlank();
    gspWaitForVBlank();
    char path[96];
    snprintf(path, sizeof path, "sdmc:/logs/%s_top.ppm", name);
    dump_screen(GFX_TOP, path);
    snprintf(path, sizeof path, "sdmc:/logs/%s_bot.ppm", name);
    dump_screen(GFX_BOTTOM, path);
}

void dbg_frame_done(void)
{
    for (int i = 0; i < g_ncmds; i++)
        if (g_cmds[i].frame == g_frame && g_cmds[i].type == CMD_SHOT)
            shot(g_cmds[i].name);
    g_frame++;
}

void dbg_scan_frame_done(void)
{
    if (++g_scanframes == 3 && g_scanshot[0]) shot(g_scanshot);
}

bool dbg_scan_cancel(void)
{
    if (!g_scancancel || g_scanframes < 1) return false;
    g_scancancel = false;
    return true;
}

bool dbg_quit(void) { return g_quit; }

void dbg_log(const char *msg)
{
    FILE *f = fopen("sdmc:/logs/log.txt", "a");
    if (!f) return;
    fprintf(f, "%s\n", msg);
    fclose(f);
}

#endif // SCRIPT
