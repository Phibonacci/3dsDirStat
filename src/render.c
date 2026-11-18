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

#include <math.h>
#include <stdio.h>
#include <string.h>

#define RGBA(r, g, b, a) ((u32)(r) | ((u32)(g) << 8) | ((u32)(b) << 16) | ((u32)(a) << 24))
#define HEX(h)           RGBA(((h) >> 16) & 0xFF, ((h) >> 8) & 0xFF, (h) & 0xFF, 0xFF)

// Palette (light "system settings" theme).
#define C_BG_TOP     HEX(0xF2F2F2)
#define C_BG_BOT     HEX(0xD8D8D8)
#define C_STRIPE     RGBA(0, 0, 0, 9)
#define C_BAR_TOP    HEX(0xFDFDFD)
#define C_BAR_BOT    HEX(0xD3D3D3)
#define C_BAR_LINE   HEX(0xA8A8A8)
#define C_FOOT_TOP   HEX(0xE6E6E6)
#define C_FOOT_BOT   HEX(0xC4C4C4)
#define C_TEXT       HEX(0x3A3A3A)
#define C_TEXT2      HEX(0x7C7C7C)
#define C_TEXT3      HEX(0xA2A2A2)
#define C_WARN       HEX(0xB45309)   // amber: caution text
#define C_DANGER     HEX(0xC62828)   // red: destructive / risky
#define C_DIM        RGBA(0, 0, 0, 120)
#define C_BTN_TOP    HEX(0xFFFFFF)
#define C_BTN_BOT    HEX(0xEAEAEA)
#define C_BTN_EDGE   HEX(0xBCBCBC)
#define C_BTN_DOWN_T HEX(0xDADADA)
#define C_BTN_DOWN_B HEX(0xF0F0F0)
#define C_SEL_TOP    HEX(0xF3FBFF)
#define C_SEL_BOT    HEX(0xD9F0FB)
#define C_SHADOW     RGBA(0, 0, 0, 38)
#define C_WHITE      HEX(0xFFFFFF)
#define C_TRACK      HEX(0xE2E2E2)
#define C_FOLDER     HEX(0xE9B949)
#define C_LABEL_BG   HEX(0xFFFFFF)
#define C_GROUP      HEX(0x8C8C8C)   // frame around each item of the map
#define CURSOR_A     0x1E9FE3    // pulsing selection cursor, dark end
#define CURSOR_B     0x8AD5F7    // light end

// Text scales.
#define SC_TITLE  0.95f
#define SC_NORMAL 0.5f
#define SC_SMALL  0.45f
#define SC_TINY   0.4f

// Button glyphs: letter-in-a-disc, or a pill for START / SELECT.
// Pill text stays at >= 0.42 scale: smaller system-font text gets blocky.
#define GLYPH_D   17.0f
#define PILL_H    15.0f
#define PILL_SC   0.42f
#define PILL_PAD  12.0f

typedef struct { float x, y, w, h; } Box;

static const Box kFooter[FB_COUNT] = {
    {   4, FOOTER_Y + 4, 100, 21 },
    { 110, FOOTER_Y + 4, 100, 21 },
    { 216, FOOTER_Y + 4, 100, 21 },
};
// Dialog box on the bottom screen and its buttons.
#define DLG_X 12.0f
#define DLG_Y 26.0f
#define DLG_W (BOT_W - 24.0f)
#define DLG_H 182.0f
static const Box kDlg2[2] = {
    { DLG_X + 12, DLG_Y + DLG_H - 44, 128, 32 },              // Cancel
    { DLG_X + DLG_W - 12 - 128, DLG_Y + DLG_H - 44, 128, 32 }, // Confirm
};
static const Box kDlg1 = { (BOT_W - 140) / 2.0f, DLG_Y + DLG_H - 44, 140, 32 };

static const Box kMsgBtn[2] = {
    {  20, 146, 134, 34 },
    { 166, 146, 134, 34 },
};

static C3D_RenderTarget *g_top, *g_bot;
static C2D_TextBuf       g_txt_top, g_txt_bot, g_meas;
static C2D_TextBuf       g_cur;          // buffer of the screen being drawn
static C2D_Font          g_font;         // NULL = the console's system font

void render_init(void)
{
    g_top     = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    g_bot     = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    g_txt_top = C2D_TextBufNew(4096);
    g_txt_bot = C2D_TextBufNew(4096);
    g_meas    = C2D_TextBufNew(1024);
#ifdef SCRIPT
    // Test builds: a font converted with mkbcfnt can stand in for the system
    // font (the emulator's replacement font has different shapes/metrics).
    g_font    = C2D_FontLoad("sdmc:/logs/font.bcfnt");
#endif
}

void render_fini(void)
{
    C2D_TextBufDelete(g_txt_top);
    C2D_TextBufDelete(g_txt_bot);
    C2D_TextBufDelete(g_meas);
    if (g_font) C2D_FontFree(g_font);
}

static bool in_box(const Box *b, int x, int y)
{
    return x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h;
}

int render_footer_button_at(int x, int y)
{
    for (int i = 0; i < FB_COUNT; i++)
        if (in_box(&kFooter[i], x, y)) return i;
    return FB_NONE;
}

int render_message_button_at(int x, int y)
{
    for (int i = 0; i < 2; i++)
        if (in_box(&kMsgBtn[i], x, y)) return i;
    return MB_NONE;
}

// ---------------------------------------------------------------------------
// Colour, text and formatting helpers
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

// Blend two 0xRRGGBB colours, t in [0, 1].
static u32 mix(u32 a, u32 b, float t)
{
    int r = (int)(((a >> 16) & 0xFF) * (1 - t) + ((b >> 16) & 0xFF) * t);
    int g = (int)(((a >> 8) & 0xFF) * (1 - t) + ((b >> 8) & 0xFF) * t);
    int bl = (int)((a & 0xFF) * (1 - t) + (b & 0xFF) * t);
    return RGBA(r, g, bl, 0xFF);
}

// Selection cursor colour, pulsing slowly between two blues.
static u32 cursor_color(void)
{
    float t = (float)(osGetTime() % 1400) / 700.0f;   // 0..2
    if (t > 1.0f) t = 2.0f - t;                        // triangle wave 0..1..0
    return mix(CURSOR_A, CURSOR_B, t * 0.8f);
}

static float line_h(float sc)
{
    return C2D_FontGetInfo(g_font)->lineFeed * sc;
}

static float text_w(const char *s, float sc)
{
    C2D_TextBufClear(g_meas);
    C2D_Text t;
    if (!C2D_TextFontParse(&t, g_font, g_meas, s)) return 0.0f;
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
static void text(const char *s, float x, float y, float sc, u32 col, u32 flags)
{
    C2D_Text t;
    if (!C2D_TextFontParse(&t, g_font, g_cur, s)) return;
    C2D_TextOptimize(&t);
    C2D_DrawText(&t, C2D_WithColor | flags, x, y, 0.0f, sc, sc, col);
}

// Draw `s` vertically centred in the band [y, y + h).
static void text_mid(const char *s, float x, float y, float h, float sc, u32 col, u32 flags)
{
    text(s, x, y + (h - line_h(sc)) / 2.0f, sc, col, flags);
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
// Widgets
// ---------------------------------------------------------------------------

// ---- Anti-aliased shapes ---------------------------------------------------
// citro2d does no anti-aliasing. Shapes are therefore built as triangle
// polygons with a 1 px "feather" band around their edge whose alpha fades from
// opaque (0.5 px inside the edge) to transparent (0.5 px outside), which
// smooths curves and diagonals on any background. Nothing overlaps, so
// translucent colours blend correctly too.

// Per-channel blend of two citro2d colours (alpha included).
static u32 lerp32(u32 a, u32 b, float t)
{
    u32 out = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        float ca = (float)((a >> sh) & 0xFF), cb = (float)((b >> sh) & 0xFF);
        out |= (u32)(ca + (cb - ca) * t + 0.5f) << sh;
    }
    return out;
}

static u32 transparent(u32 c) { return c & 0x00FFFFFFu; }

#define RR_MAX_SEG 6
#define RR_MAX_PTS (4 * (RR_MAX_SEG + 1))

// Outline of a rounded rectangle grown by `d` (negative shrinks), clockwise.
static int rr_outline(float x, float y, float w, float h, float r, float d,
                      int seg, float *px, float *py)
{
    static float cs[RR_MAX_SEG + 1], sn[RR_MAX_SEG + 1];
    static int tableSeg = 0;
    if (tableSeg != seg) {
        for (int k = 0; k <= seg; k++) {
            float a = (float)k / seg * 1.5707963f;
            cs[k] = cosf(a);
            sn[k] = sinf(a);
        }
        tableSeg = seg;
    }
    float R = r + d;
    if (R < 0.0f) R = 0.0f;
    const float cx[4] = { x + r, x + w - r, x + w - r, x + r };
    const float cy[4] = { y + r, y + r, y + h - r, y + h - r };
    int n = 0;
    for (int k = 0; k <= seg; k++) { px[n] = cx[0] - R * cs[k]; py[n++] = cy[0] - R * sn[k]; }
    for (int k = 0; k <= seg; k++) { px[n] = cx[1] + R * sn[k]; py[n++] = cy[1] - R * cs[k]; }
    for (int k = 0; k <= seg; k++) { px[n] = cx[2] + R * cs[k]; py[n++] = cy[2] + R * sn[k]; }
    for (int k = 0; k <= seg; k++) { px[n] = cx[3] - R * sn[k]; py[n++] = cy[3] + R * cs[k]; }
    return n;
}

// Rounded rectangle with a vertical gradient (top -> bot) and soft edges.
// With r = h / 2 it is a pill, with w = h = 2r a disc.
static void rrect(float x, float y, float w, float h, float r, u32 top, u32 bot)
{
    if (w <= 0.0f || h <= 0.0f) return;
    if (r * 2 > h) r = h / 2;
    if (r * 2 > w) r = w / 2;
    if (r < 0.5f) r = 0.5f;
    int seg = r <= 2.5f ? 2 : r <= 5.0f ? 4 : RR_MAX_SEG;

    float ix[RR_MAX_PTS], iy[RR_MAX_PTS], ox[RR_MAX_PTS], oy[RR_MAX_PTS];
    int n = rr_outline(x, y, w, h, r, -0.5f, seg, ix, iy);
    rr_outline(x, y, w, h, r, 0.5f, seg, ox, oy);

    #define COL_AT(yy) lerp32(top, bot, ((yy) - y) / h < 0 ? 0 : ((yy) - y) / h > 1 ? 1 : ((yy) - y) / h)
    float mx = x + w / 2, my = y + h / 2;
    u32 cm = COL_AT(my);
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        u32 ci = COL_AT(iy[i]), cj = COL_AT(iy[j]);
        // Interior fan.
        C2D_DrawTriangle(mx, my, cm, ix[i], iy[i], ci, ix[j], iy[j], cj, 0);
        // Feather band.
        u32 ti = transparent(COL_AT(oy[i])), tj = transparent(COL_AT(oy[j]));
        C2D_DrawTriangle(ix[i], iy[i], ci, ix[j], iy[j], cj, ox[j], oy[j], tj, 0);
        C2D_DrawTriangle(ix[i], iy[i], ci, ox[j], oy[j], tj, ox[i], oy[i], ti, 0);
    }
    #undef COL_AT
}

// Straight stroke of width `w` with square caps and soft edges.
static void stroke(float x0, float y0, float x1, float y1, float w, u32 col)
{
    float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
    if (len <= 0.0f) return;
    dx /= len; dy /= len;
    float nx = -dy, ny = dx;
    float hi = w / 2 - 0.5f, ho = w / 2 + 0.5f;     // inner / outer half-width
    if (hi < 0.0f) hi = 0.0f;
    float ax[4], ay[4], bx[4], by[4];                // inner / outer quads
    const float sx[4] = { -1, 1, 1, -1 }, sy[4] = { -1, -1, 1, 1 };
    for (int k = 0; k < 4; k++) {
        float px = sx[k] < 0 ? x0 : x1, py = sx[k] < 0 ? y0 : y1;
        ax[k] = px + dx * sx[k] * hi + nx * sy[k] * hi;
        ay[k] = py + dy * sx[k] * hi + ny * sy[k] * hi;
        bx[k] = px + dx * sx[k] * ho + nx * sy[k] * ho;
        by[k] = py + dy * sx[k] * ho + ny * sy[k] * ho;
    }
    u32 t = transparent(col);
    C2D_DrawTriangle(ax[0], ay[0], col, ax[1], ay[1], col, ax[2], ay[2], col, 0);
    C2D_DrawTriangle(ax[0], ay[0], col, ax[2], ay[2], col, ax[3], ay[3], col, 0);
    for (int k = 0; k < 4; k++) {
        int m = (k + 1) % 4;
        C2D_DrawTriangle(ax[k], ay[k], col, ax[m], ay[m], col, bx[m], by[m], t, 0);
        C2D_DrawTriangle(ax[k], ay[k], col, bx[m], by[m], t, bx[k], by[k], t, 0);
    }
}

typedef enum { BTN_NORMAL, BTN_PRESSED, BTN_SELECTED } BtnState;

// Rounded white button with a 1 px edge and soft shadow. Selected buttons get
// the pulsing blue cursor ring around them.
static void button(float x, float y, float w, float h, BtnState st)
{
    const float r = 6.0f;
    rrect(x, y + 1.5f, w, h, r, C_SHADOW, C_SHADOW);
    if (st == BTN_SELECTED) {
        u32 c = cursor_color();
        rrect(x - 2.5f, y - 2.5f, w + 5.0f, h + 5.0f, r + 2.5f, c, c);
    }
    rrect(x, y, w, h, r, C_BTN_EDGE, C_BTN_EDGE);
    switch (st) {
    case BTN_PRESSED:  rrect(x + 1, y + 1, w - 2, h - 2, r - 1, C_BTN_DOWN_T, C_BTN_DOWN_B); break;
    case BTN_SELECTED: rrect(x + 1, y + 1, w - 2, h - 2, r - 1, C_SEL_TOP, C_SEL_BOT); break;
    default:           rrect(x + 1, y + 1, w - 2, h - 2, r - 1, C_BTN_TOP, C_BTN_BOT); break;
    }
}

// White rounded panel (non-interactive container).
static void panel(float x, float y, float w, float h)
{
    rrect(x, y + 1.5f, w, h, 7, C_SHADOW, C_SHADOW);
    rrect(x, y, w, h, 7, C_BTN_EDGE, C_BTN_EDGE);
    rrect(x + 1, y + 1, w - 2, h - 2, 6, C_WHITE, HEX(0xF6F6F6));
}

// Face-button colours of the New 3DS: A red, B yellow, X blue, Y green.
// START / SELECT are grey pills.
static void glyph_colors(const char *key, u32 *face, u32 *ink)
{
    *ink = 0xFFFFFF;
    switch (key[1] ? 0 : key[0]) {
    case 'A': *face = 0xD93A34; break;
    case 'B': *face = 0xF2BE0F; *ink = 0x4A3B00; break;
    case 'X': *face = 0x2D76DC; break;
    case 'Y': *face = 0x24A148; break;
    default:  *face = 0x6A6A6A; break;
    }
}

static int snap(float v) { return (int)(v + 0.5f); }

// Horizontal ink extent of an ASCII string: offset of the first inked pixel
// from the pen position, and the inked width (pixels at scale `sc`).
// Centring on the ink (not the advance box) avoids the trailing letter gap.
static void ink_extent(const char *str, float sc, float *off, float *w)
{
    float pen = 0, x0 = 0, x1 = 0;
    for (const char *p = str; *p; p++) {
        int gi = C2D_FontGlyphIndexFromCodePoint(g_font, (u8)*p);
        const charWidthInfo_s *ci = C2D_FontGetCharWidthInfo(g_font, gi);
        if (p == str) x0 = pen + ci->left;
        x1 = pen + ci->left + ci->glyphWidth;
        pen += ci->charWidth;
    }
    *off = x0 * sc;
    *w   = (x1 - x0) * sc;
}

// Distance from the top of a text line to the middle of its capital letters,
// in font units. Capitals stand on the baseline and are ~0.71 of the ascent
// tall (measured on Rodin NTLG, the console's system font).
static float cap_mid(void)
{
    const FINF_s *fi = C2D_FontGetInfo(g_font);
    return fi->tglp->baselinePos - 0.355f * fi->ascent;
}

// Draw text whose capitals are vertically centred on `cy`.
static void text_cap(const char *s, float x, float cy, float sc, u32 col)
{
    text(s, x, (float)snap(cy - cap_mid() * sc), sc, col, 0);
}

// A/B/X/Y drawn as strokes (not font text) so they are bold, crisp and exactly
// centred on any system font. Coordinates are relative to the disc centre.
typedef struct { float x0, y0, x1, y1; } Seg;

static void letter_strokes(char k, Seg *seg, int *n)
{
    static const Seg A[] = { {-3.8f, 4.5f, 0, -4.5f}, {0, -4.5f, 3.8f, 4.5f}, {-2.2f, 1.6f, 2.2f, 1.6f} };
    static const Seg B[] = { {-2.9f, -4.5f, -2.9f, 4.5f},
                             {-2.9f, -4.5f, 1.0f, -4.5f}, {1.0f, -4.5f, 2.7f, -3.0f},
                             {2.7f, -3.0f, 2.7f, -1.6f}, {2.7f, -1.6f, 1.2f, -0.2f},
                             {-2.9f, -0.2f, 1.4f, -0.2f}, {1.4f, -0.2f, 3.2f, 1.3f},
                             {3.2f, 1.3f, 3.2f, 3.0f}, {3.2f, 3.0f, 1.5f, 4.5f},
                             {1.5f, 4.5f, -2.9f, 4.5f} };
    static const Seg X[] = { {-3.4f, -4.5f, 3.4f, 4.5f}, {3.4f, -4.5f, -3.4f, 4.5f} };
    static const Seg Y[] = { {-3.6f, -4.5f, 0, 0.2f}, {3.6f, -4.5f, 0, 0.2f}, {0, 0.2f, 0, 4.5f} };
    const Seg *src = NULL;
    int cnt = 0;
    switch (k) {
    case 'A': src = A; cnt = 3; break;
    case 'B': src = B; cnt = 10; break;
    case 'X': src = X; cnt = 2; break;
    case 'Y': src = Y; cnt = 3; break;
    default: break;
    }
    for (int i = 0; i < cnt; i++) seg[i] = src[i];
    *n = cnt;
}

static void draw_letter(char k, float cx, float cy, u32 ink)
{
    Seg seg[10];
    int n = 0;
    letter_strokes(k, seg, &n);
    // Square caps (half a stroke past each end) close the joints. Where strokes
    // overlap they share one colour, so the overlap is invisible.
    for (int i = 0; i < n; i++)
        stroke(cx + seg[i].x0, cy + seg[i].y0, cx + seg[i].x1, cy + seg[i].y1, 1.9f, HEX(ink));
}

static float pill_w(const char *key)
{
    float off, w;
    ink_extent(key, PILL_SC, &off, &w);
    return (float)snap(w + PILL_PAD);
}

static float glyph_w(const char *key)
{
    return strlen(key) == 1 ? GLYPH_D : pill_w(key);
}

// Button glyph: a coloured disc with its letter, or a pill for "START" etc.
// `x` is the left edge, `cy` the vertical centre. Returns the glyph width.
static float glyph(const char *key, float x, float cy)
{
    u32 face, ink;
    glyph_colors(key, &face, &ink);
    // Snap to the pixel grid: with an odd diameter the centre must sit on a
    // pixel centre (n + 0.5) for the disc to rasterise symmetrically.
    float lx = (float)(int)x, mcy = (float)(int)cy + 0.5f;

    if (strlen(key) == 1) {
        const float r = GLYPH_D / 2, cx = lx + r;
        rrect(cx - r, mcy - r, GLYPH_D, GLYPH_D, r, shade(face, 1.18f), shade(face, 0.86f));
        draw_letter(key[0], cx, mcy, ink);
        return GLYPH_D;
    }

    float w = pill_w(key), h = PILL_H, y = mcy - h / 2;
    rrect(lx, y, w, h, h / 2, shade(face, 1.12f), shade(face, 0.9f));
    float off, iw;
    ink_extent(key, PILL_SC, &off, &iw);
    text_cap(key, (float)snap(lx + (w - iw) / 2 - off), mcy, PILL_SC, HEX(ink));
    return w;
}

// Glyph + label centred inside a box.
static void glyph_label(const Box *b, const char *key, const char *label, float sc)
{
    float gw = glyph_w(key);
    float lw = text_w(label, sc);
    float x = b->x + (b->w - gw - 5.0f - lw) / 2.0f;
    float cy = b->y + b->h / 2.0f;
    glyph(key, x, cy);
    text_cap(label, (float)snap(x + gw + 5.0f), (float)(int)cy + 0.5f, sc, C_TEXT);
}

// Light-grey background with fine horizontal stripes.
static void background(float w, float h)
{
    C2D_DrawRectangle(0, 0, 0, w, h, C_BG_TOP, C_BG_TOP, C_BG_BOT, C_BG_BOT);
    for (float y = 1; y < h; y += 3)
        C2D_DrawRectSolid(0, y, 0, w, 1, C_STRIPE);
}

// Grey gradient title bar. `center` centres the title.
static void title_bar(float w, const char *title, const char *right, bool center)
{
    C2D_DrawRectangle(0, 0, 0, w, BOT_HDR_H, C_BAR_TOP, C_BAR_TOP, C_BAR_BOT, C_BAR_BOT);
    C2D_DrawRectSolid(0, BOT_HDR_H - 1, 0, w, 1, C_BAR_LINE);
    float rw = right ? text_w(right, SC_SMALL) : 0.0f;
    if (right)
        text_mid(right, w - 7.0f, 0, BOT_HDR_H - 1, SC_SMALL, C_TEXT2, C2D_AlignRight);
    char t[512];
    if (center) {
        float maxW = w - 2.0f * (rw + 14.0f);
        fit(title, maxW, SC_NORMAL, false, t, sizeof t);
        text_mid(t, w / 2.0f, 0, BOT_HDR_H - 1, SC_NORMAL, C_TEXT, C2D_AlignCenter);
    } else {
        fit(title, w - rw - 22.0f, SC_NORMAL, true, t, sizeof t);
        text_mid(t, 8.0f, 0, BOT_HDR_H - 1, SC_NORMAL, C_TEXT, 0);
    }
}

static void footer_bar(void)
{
    C2D_DrawRectangle(0, FOOTER_Y, 0, BOT_W, BOT_H - FOOTER_Y,
                      C_FOOT_TOP, C_FOOT_TOP, C_FOOT_BOT, C_FOOT_BOT);
    C2D_DrawRectSolid(0, FOOTER_Y, 0, BOT_W, 1, C_BAR_LINE);
}

static void footer_button(const ui_state *s, int id, const char *key, const char *label)
{
    const Box *b = &kFooter[id];
    bool pressed = s && s->touching && !s->dragged &&
                   render_footer_button_at(s->touchX, s->touchY) == id;
    button(b->x, b->y, b->w, b->h, pressed ? BTN_PRESSED : BTN_NORMAL);
    glyph_label(b, key, label, SC_SMALL);
}

static void folder_icon(float x, float y, float s)
{
    u32 top = shade(0xE9B949, 1.25f), bot = C_FOLDER;
    rrect(x, y, 6 * s, 3 * s, 1, bot, bot);
    rrect(x, y + 2 * s, 13 * s, 9 * s, 1.5f, top, bot);
}

static void file_chip(float x, float y, float s, u32 rgb)
{
    rrect(x + 1, y, 11 * s, 11 * s, 2.5f, shade(rgb, 1.3f), shade(rgb, 0.85f));
}

static void entry_icon(const DirEntry *e, float x, float y, float s)
{
    if (e->is_dir) folder_icon(x, y, s);
    else           file_chip(x, y, s, category_color(e->category));
}

// Thin rounded usage meter.
static void meter(float x, float y, float w, float frac, u32 rgb)
{
    rrect(x, y, w, 4, 2, C_TRACK, C_TRACK);
    float fw = w * frac;
    if (fw > 0.0f && fw < 4.0f) fw = 4.0f;
    if (fw > 0.0f) rrect(x, y, fw, 4, 2, shade(rgb, 1.2f), shade(rgb, 0.85f));
}

// ---------------------------------------------------------------------------
// Top screen: treemap + description of the selection
// ---------------------------------------------------------------------------

// Leaf cells: soft vertical gradient and a 1 px gap on their right/bottom edge
// so neighbouring blocks stay distinct against the white panel.
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
    u32 top = shade(base, 1.3f), bot = shade(base, 0.82f);
    C2D_DrawRectangle(x0, y0, 0, w, h, top, top, bot, bot);
}

// Frame each item of the current folder so the contents of one sub-folder read
// as one group. Neighbouring frames meet as a 2 px line, heavier than the 1 px
// gaps between the files inside a group.
static void group_frames(const DirEntry *dir)
{
    const u32 c = C_GROUP;
    for (int i = 0; i < dir->nchildren; i++) {
        const DirEntry *e = dir->children[i];
        int x0 = (int)(e->rx + 0.5f), y0 = (int)(e->ry + 0.5f);
        int x1 = (int)(e->rx + e->rw + 0.5f), y1 = (int)(e->ry + e->rh + 0.5f);
        int w = x1 - x0, h = y1 - y0;
        if (w < 3 || h < 3) continue;
        C2D_DrawRectSolid(x0, y0, 0, w, 1, c);
        C2D_DrawRectSolid(x0, y1 - 1, 0, w, 1, c);
        C2D_DrawRectSolid(x0, y0, 0, 1, h, c);
        C2D_DrawRectSolid(x1 - 1, y0, 0, 1, h, c);
    }
}

static void draw_labels(const DirEntry *dir)
{
    const float sc = SC_TINY, lh = line_h(sc);
    for (int i = 0; i < dir->nchildren; i++) {
        const DirEntry *c = dir->children[i];
        if (c->rw < 34.0f || c->rh < lh + 4.0f) continue;
        bool twoLines = c->rh >= 2.0f * lh + 8.0f;
        float x = c->rx + 2.0f, y = c->ry + 2.0f, w = c->rw - 5.0f;

        char name[160], buf[160];
        snprintf(buf, sizeof buf, "%s%s", c->name, c->is_dir ? "/" : "");
        fit(buf, w - 7.0f, sc, false, name, sizeof name);
        // A name cut down to a few letters ("20...") says nothing: show only
        // the size then, which still helps compare the blocks.
        size_t nl = strlen(name);
        if (nl >= 3 && strcmp(name + nl - 3, "...") == 0) {
            int kept = 0;
            for (size_t k = 0; k + 3 < nl; k++)
                if (((unsigned char)name[k] & 0xC0) != 0x80) kept++;
            if (kept < 6) {
                format_size(c->size, name, sizeof name);
                if (text_w(name, sc) > w - 7.0f) continue;
                twoLines = false;
            }
        }
        float tw = text_w(name, sc);
        char sz[32] = "";
        float sw = 0.0f;
        if (twoLines) {
            format_size(c->size, sz, sizeof sz);
            sw = text_w(sz, sc);
        }
        float bw = (tw > sw ? tw : sw) + 7.0f;
        if (bw > w) bw = w;
        float bh = twoLines ? 2.0f * lh + 2.0f : lh + 1.0f;
        rrect(x, y, bw, bh, 3, C_LABEL_BG, C_LABEL_BG);
        text(name, x + 3.5f, y, sc, C_TEXT, 0);
        if (twoLines) text(sz, x + 3.5f, y + lh, sc, C_TEXT2, 0);
    }
}

static void selection_ring(const DirEntry *c)
{
    // Clamp to the map so the ring never leaves the panel.
    float x0 = c->rx - 1.0f, y0 = c->ry - 1.0f;
    float x1 = c->rx + c->rw, y1 = c->ry + c->rh;
    if (x0 < MAP_X - 2) x0 = MAP_X - 2;
    if (y0 < MAP_Y - 2) y0 = MAP_Y - 2;
    if (x1 > MAP_X + MAP_W + 1) x1 = MAP_X + MAP_W + 1;
    if (y1 > MAP_Y + MAP_H + 1) y1 = MAP_Y + MAP_H + 1;
    float w = x1 - x0, h = y1 - y0;
    if (w < 6.0f) { x0 -= (6.0f - w) / 2; w = 6.0f; }
    if (h < 6.0f) { y0 -= (6.0f - h) / 2; h = 6.0f; }
    u32 c1 = cursor_color();
    const float t = 3.0f;
    C2D_DrawRectSolid(x0, y0, 0, w, t, c1);
    C2D_DrawRectSolid(x0, y0 + h - t, 0, w, t, c1);
    C2D_DrawRectSolid(x0, y0, 0, t, h, c1);
    C2D_DrawRectSolid(x0 + w - t, y0, 0, t, h, c1);
    C2D_DrawRectSolid(x0 + t, y0 + t, 0, w - 2 * t, 1, C_WHITE);
    C2D_DrawRectSolid(x0 + t, y0 + h - t - 1, 0, w - 2 * t, 1, C_WHITE);
    C2D_DrawRectSolid(x0 + t, y0 + t, 0, 1, h - 2 * t, C_WHITE);
    C2D_DrawRectSolid(x0 + w - t - 1, y0 + t, 0, 1, h - 2 * t, C_WHITE);
}

static void description(const ui_state *s)
{
    const DirEntry *dir = s->dir;
    float x = MAP_X - MAP_PAD, y = DESC_Y, w = MAP_W + 2 * MAP_PAD, h = TOP_H - DESC_Y - 5;
    panel(x, y, w, h);
    if (s->sel < 0 || s->sel >= dir->nchildren) {
        text_mid("This folder is empty.", x + 12, y, h, SC_NORMAL, C_TEXT2, 0);
        return;
    }
    const DirEntry *e = dir->children[s->sel];
    entry_icon(e, x + 9, y + (h - 11) / 2.0f, 1.0f);

    char sz[32], pct[16], info[128], cnt[24];
    format_size(e->size, sz, sizeof sz);
    fmt_pct(e->size, dir->size, pct, sizeof pct);
    u32 infoCol = C_TEXT2;
    if (ui_is_system_data(e)) {
        snprintf(info, sizeof info,
                 "System data: remove it from System Settings");
        infoCol = C_WARN;
    } else if (e->is_dir) {
        fmt_count(e->nfiles, cnt, sizeof cnt);
        snprintf(info, sizeof info, "Folder  -  %s file%s  -  %s of this folder",
                 cnt, e->nfiles == 1 ? "" : "s", pct);
    } else {
        snprintf(info, sizeof info, "%s  -  %s of this folder", category_name(e->category), pct);
    }

    float sw = text_w(sz, 0.6f);
    text_mid(sz, x + w - 10, y, h, 0.6f, C_TEXT, C2D_AlignRight);
    float tx = x + 30, maxW = w - 30 - sw - 22;
    char name[256], line2[160];
    fit(e->name, maxW, SC_NORMAL, false, name, sizeof name);
    fit(info, maxW, SC_TINY, false, line2, sizeof line2);
    text(name, tx, y + 2.5f, SC_NORMAL, C_TEXT, 0);
    text(line2, tx, y + 3.0f + line_h(SC_NORMAL), SC_TINY, infoCol, 0);
}

static void render_top(const ui_state *s)
{
    const DirEntry *dir = s->dir;
    background(TOP_W, TOP_H);

    char path[512], right[64], sz[32], pct[16];
    path_of(dir, path, sizeof path);
    format_size(dir->size, sz, sizeof sz);
    if (dir != s->tree->root) {
        fmt_pct(dir->size, s->tree->root->size, pct, sizeof pct);
        snprintf(right, sizeof right, "%s  (%s of SD)", sz, pct);
    } else {
        snprintf(right, sizeof right, "%s used", sz);
    }
    title_bar(TOP_W, path, right, false);

    panel(MAP_X - MAP_PAD, MAP_Y - MAP_PAD, MAP_W + 2 * MAP_PAD, MAP_H + 2 * MAP_PAD);
    if (dir->size == 0) {
        text_mid(dir->nchildren ? "Only empty files here." : "This folder is empty.",
                 TOP_W / 2.0f, MAP_Y, MAP_H, SC_NORMAL, C_TEXT2, C2D_AlignCenter);
    } else {
        for (int i = 0; i < dir->nchildren; i++)
            draw_cell(dir->children[i]);
        group_frames(dir);
        draw_labels(dir);
        if (s->sel >= 0 && s->sel < dir->nchildren && dir->children[s->sel]->size > 0)
            selection_ring(dir->children[s->sel]);
    }
    description(s);
}

// ---------------------------------------------------------------------------
// Bottom screen: list of buttons / file-type legend
// ---------------------------------------------------------------------------

static void scrollbar(int first, int shown, int total)
{
    if (total <= shown) return;
    float trackY = LIST_Y, trackH = ROW_H * LIST_ROWS - 3;
    float th = trackH * shown / total;
    if (th < 12.0f) th = 12.0f;
    float ty = trackY + (trackH - th) * first / (float)(total - shown);
    rrect(BOT_W - 7, trackY, 4, trackH, 2, HEX(0xC2C2C2), HEX(0xC2C2C2));
    rrect(BOT_W - 7, ty, 4, th, 2, HEX(0x8A8A8A), HEX(0x6E6E6E));
}

static void render_list(const ui_state *s)
{
    const DirEntry *dir = s->dir;
    int n = dir->nchildren;

    char right[32], cnt[24];
    fmt_count((uint64_t)n, cnt, sizeof cnt);
    snprintf(right, sizeof right, "%s item%s", cnt, n == 1 ? "" : "s");
    title_bar(BOT_W, dir == s->tree->root ? "SD Card" : dir->name, right, true);

    if (n == 0) {
        panel(16, LIST_Y + 40, BOT_W - 32, 50);
        text_mid("This folder is empty.", BOT_W / 2.0f, LIST_Y + 40, 50, SC_NORMAL,
                 C_TEXT2, C2D_AlignCenter);
    }

    bool sb = n > LIST_ROWS;
    float bx = 9.0f, bw = BOT_W - 18.0f - (sb ? 6.0f : 0.0f);
    int pressedRow = -1;
    if (s->touching && !s->dragged && s->touchY >= LIST_Y && s->touchY < FOOTER_Y)
        pressedRow = s->scroll + (s->touchY - LIST_Y) / ROW_H;

    for (int r = 0; r < LIST_ROWS; r++) {
        int i = s->scroll + r;
        if (i >= n) break;
        const DirEntry *e = dir->children[i];
        float y = LIST_Y + 2 + r * ROW_H;
        BtnState st = i == s->sel ? BTN_SELECTED : i == pressedRow ? BTN_PRESSED : BTN_NORMAL;
        button(bx, y, bw, ROW_BTN_H, st);

        entry_icon(e, bx + 8, y + 5, 1.0f);
        char sz[32];
        format_size(e->size, sz, sizeof sz);
        float sw = text_w(sz, SC_SMALL);
        text(sz, bx + bw - 9, y + 2.0f, SC_SMALL, C_TEXT2, C2D_AlignRight);

        char name[256];
        fit(e->name, bw - 30 - sw - 16, SC_NORMAL, false, name, sizeof name);
        text(name, bx + 28, y + 1.5f, SC_NORMAL, C_TEXT, 0);

        float frac = dir->size ? (float)((double)e->size / (double)dir->size) : 0.0f;
        meter(bx + 28, y + ROW_BTN_H - 8, bw - 37, frac, category_color(e->category));
    }
    scrollbar(s->scroll, LIST_ROWS, n);

    footer_bar();
    if (dir == s->tree->root) footer_button(s, FB_BACK, "START", "Close");
    else                      footer_button(s, FB_BACK, "B", "Back");
    footer_button(s, FB_MID, "X", "Delete");
    footer_button(s, FB_TYPES, "Y", "File types");
}

static void render_legend(const ui_state *s)
{
    const DirEntry *dir = s->dir;
    title_bar(BOT_W, "File types", dir == s->tree->root ? "whole SD" : "this folder", true);

    int order[CAT_COUNT], n = 0;
    for (int c = 0; c < CAT_COUNT; c++)
        if (s->catBytes[c] > 0) order[n++] = c;
    for (int i = 1; i < n; i++)                // insertion sort, largest first
        for (int j = i; j > 0 && s->catBytes[order[j]] > s->catBytes[order[j - 1]]; j--) {
            int t = order[j]; order[j] = order[j - 1]; order[j - 1] = t;
        }

    // One white panel holding all rows, separated by hairlines.
    float px = 9, py = LIST_Y + 2, pw = BOT_W - 18, ph = FOOTER_Y - py - 6;
    panel(px, py, pw, ph);
    if (n == 0) {
        text_mid("No files here.", BOT_W / 2.0f, py, ph, SC_NORMAL, C_TEXT2, C2D_AlignCenter);
    } else {
        float rowH = (ph - 4) / (n > 7 ? n : 7);
        uint64_t total = dir->size;
        for (int i = 0; i < n; i++) {
            int c = order[i];
            float y = py + 2 + i * rowH;
            if (i > 0) C2D_DrawRectSolid(px + 8, y, 0, pw - 16, 1, HEX(0xE3E3E3));
            file_chip(px + 7, y + (rowH - 11) / 2.0f, 1.0f, category_color(c));
            text_mid(category_name(c), px + 26, y, rowH, SC_SMALL, C_TEXT, 0);
            char sz[32], pct[16];
            format_size(s->catBytes[c], sz, sizeof sz);
            fmt_pct(s->catBytes[c], total, pct, sizeof pct);
            text_mid(pct, px + pw - 8, y, rowH, SC_SMALL, C_TEXT2, C2D_AlignRight);
            text_mid(sz, px + pw - 50, y, rowH, SC_SMALL, C_TEXT, C2D_AlignRight);
        }
    }

    footer_bar();
    footer_button(s, FB_BACK, "B", "Back");
    footer_button(s, FB_MID, "X", "Rescan");
    footer_button(s, FB_TYPES, "Y", "Folder list");
}

// ---------------------------------------------------------------------------
// Dialogs
// ---------------------------------------------------------------------------

int render_dialog_button_at(const ui_state *s, int x, int y)
{
    switch (s->dlg) {
    case DLG_DELETE:
        if (in_box(&kDlg2[0], x, y)) return DB_CANCEL;
        if (in_box(&kDlg2[1], x, y)) return DB_CONFIRM;
        return DB_NONE;
    case DLG_SYSTEM:
    case DLG_ERROR:
        return in_box(&kDlg1, x, y) ? DB_CONFIRM : DB_NONE;
    default:
        return DB_NONE;
    }
}

static void dialog_button(const ui_state *s, const Box *b, int id,
                          const char *key, const char *label)
{
    bool pressed = s->touching && !s->dragged &&
                   render_dialog_button_at(s, s->touchX, s->touchY) == id;
    button(b->x, b->y, b->w, b->h, pressed ? BTN_PRESSED : BTN_NORMAL);
    glyph_label(b, key, label, SC_NORMAL);
}

// Centred line of dialog text; returns the y of the next line.
static float dlg_line(const char *s, float y, float sc, u32 col)
{
    char t[256];
    fit(s, DLG_W - 28, sc, false, t, sizeof t);
    text(t, BOT_W / 2.0f, y, sc, col, C2D_AlignCenter);
    return y + line_h(sc) + 1.0f;
}

// Icon + name, centred.
static float dlg_entry(const DirEntry *e, const char *name, float y)
{
    char t[256];
    fit(name, DLG_W - 50, SC_NORMAL, false, t, sizeof t);
    float tw = text_w(t, SC_NORMAL);
    float x = (BOT_W - tw - 19) / 2.0f;
    if (e) entry_icon(e, x, y + 3.0f, 1.0f);
    text(t, x + (e ? 19 : 9), y, SC_NORMAL, C_TEXT, 0);
    return y + line_h(SC_NORMAL) + 4.0f;
}

static void render_dialog(const ui_state *s)
{
    C2D_DrawRectSolid(0, 0, 0, BOT_W, BOT_H, C_DIM);
    panel(DLG_X, DLG_Y, DLG_W, DLG_H);
    float y = DLG_Y + 12;
    const DirEntry *e = s->dlgEntry;

    switch (s->dlg) {
    case DLG_DELETE: {
        y = dlg_line(e->is_dir ? "Delete this folder?" : "Delete this file?", y, 0.6f, C_TEXT);
        y += 6;
        y = dlg_entry(e, e->name, y);
        char sz[32], cnt[24], info[96];
        format_size(e->size, sz, sizeof sz);
        if (e->is_dir) {
            fmt_count(e->nfiles, cnt, sizeof cnt);
            snprintf(info, sizeof info, "%s file%s  -  %s", cnt, e->nfiles == 1 ? "" : "s", sz);
        } else {
            snprintf(info, sizeof info, "%s  -  %s", category_name(e->category), sz);
        }
        y = dlg_line(info, y, SC_SMALL, C_TEXT2);
        y += 6;
        if (ui_is_boot_file(e))
            y = dlg_line("Warning: custom firmware may need this to start!", y, SC_SMALL, C_DANGER);
        dlg_line("This cannot be undone.", y, SC_SMALL, C_WARN);
        dialog_button(s, &kDlg2[0], DB_CANCEL, "B", "Cancel");
        dialog_button(s, &kDlg2[1], DB_CONFIRM, "A", "Delete");
        break;
    }
    case DLG_SYSTEM:
        y = dlg_line("System data", y, 0.6f, C_TEXT);
        y += 8;
        y = dlg_line("Files in \"Nintendo 3DS\" are managed by", y, SC_SMALL, C_TEXT);
        y = dlg_line("the console. Remove software, save data", y, SC_SMALL, C_TEXT);
        y = dlg_line("and extra data from System Settings >", y, SC_SMALL, C_TEXT);
        dlg_line("Data Management instead.", y, SC_SMALL, C_TEXT);
        dialog_button(s, &kDlg1, DB_CONFIRM, "A", "OK");
        break;
    case DLG_ERROR: {
        y = dlg_line("Could not delete", y, 0.6f, C_DANGER);
        y += 6;
        y = dlg_entry(NULL, s->dlgName, y);
        char err[48];
        snprintf(err, sizeof err, "Error 0x%08lX", (unsigned long)s->dlgError);
        y = dlg_line(err, y, SC_SMALL, C_TEXT2);
        y += 4;
        dlg_line("Part of it may be gone: rescan to check.", y, SC_SMALL, C_TEXT);
        dialog_button(s, &kDlg1, DB_CONFIRM, "A", "OK");
        break;
    }
    case DLG_BUSY:
        y = dlg_line("Deleting...", DLG_Y + 50, 0.6f, C_TEXT);
        y += 6;
        y = dlg_entry(NULL, s->dlgName, y);
        dlg_line("Please wait.", y + 4, SC_SMALL, C_TEXT2);
        break;
    default:
        break;
    }
}

void render_frame(const ui_state *s)
{
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

    C2D_TargetClear(g_top, C_BG_TOP);
    C2D_SceneBegin(g_top);
    C2D_TextBufClear(g_txt_top);
    g_cur = g_txt_top;
    render_top(s);

    C2D_TargetClear(g_bot, C_BG_TOP);
    C2D_SceneBegin(g_bot);
    C2D_TextBufClear(g_txt_bot);
    g_cur = g_txt_bot;
    background(BOT_W, BOT_H);
    if (s->legend) render_legend(s);
    else           render_list(s);
    if (s->dlg != DLG_NONE) render_dialog(s);

    C3D_FrameEnd(0);
}

// ---------------------------------------------------------------------------
// Scan progress and messages
// ---------------------------------------------------------------------------

// Top screen used while scanning / for messages: the bar says what is going
// on, the panel shows the app name (only once) and its tagline.
static void top_title(const char *status)
{
    background(TOP_W, TOP_H);
    title_bar(TOP_W, status, NULL, true);
    float px = 50, py = 52, pw = TOP_W - 100, ph = 150;
    panel(px, py, pw, ph);
    text("3dsDirStat", TOP_W / 2.0f, py + 18, SC_TITLE, C_TEXT, C2D_AlignCenter);
    text("Disk usage of your SD card", TOP_W / 2.0f, py + 56, SC_NORMAL, C_TEXT2,
         C2D_AlignCenter);

    // A small treemap-like strip in the category colours.
    static const int cats[] = { CAT_3DS, CAT_CIA, CAT_IMG, CAT_AUD, CAT_VID, CAT_ARCH };
    static const float wts[] = { 0.30f, 0.22f, 0.16f, 0.13f, 0.11f, 0.08f };
    float x = px + 24, sw = pw - 48;
    for (int i = 0; i < 6; i++) {
        float w = sw * wts[i];
        u32 base = category_color(cats[i]);
        C2D_DrawRectangle(x, py + 96, 0, w - 2, 30, shade(base, 1.3f), shade(base, 1.3f),
                          shade(base, 0.82f), shade(base, 0.82f));
        x += w;
    }
}

void render_scan(const ScanStatus *st, uint64_t ms)
{
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

    C2D_TargetClear(g_top, C_BG_TOP);
    C2D_SceneBegin(g_top);
    C2D_TextBufClear(g_txt_top);
    g_cur = g_txt_top;
    top_title("Scanning SD Card");

    C2D_TargetClear(g_bot, C_BG_TOP);
    C2D_SceneBegin(g_bot);
    C2D_TextBufClear(g_txt_bot);
    g_cur = g_txt_bot;
    background(BOT_W, BOT_H);
    title_bar(BOT_W, "Progress", NULL, true);

    float px = 12, py = 36, pw = BOT_W - 24, ph = 150;
    panel(px, py, pw, ph);
    text("Please wait...", BOT_W / 2.0f, py + 10, 0.55f, C_TEXT, C2D_AlignCenter);

    char a[32], b[32], line[96];
    fmt_count(st->files, a, sizeof a);
    fmt_count(st->dirs, b, sizeof b);
    snprintf(line, sizeof line, "%s files in %s folders", a, b);
    text(line, BOT_W / 2.0f, py + 40, SC_NORMAL, C_TEXT, C2D_AlignCenter);
    format_size(st->bytes, a, sizeof a);
    snprintf(line, sizeof line, "%s found so far", a);
    text(line, BOT_W / 2.0f, py + 60, SC_NORMAL, C_TEXT2, C2D_AlignCenter);

    // Indeterminate progress bar: a glossy block sliding along a rounded track.
    const float tx = px + 20, tw = pw - 40, ty = py + 92, th = 12, bw = 70;
    rrect(tx - 1, ty - 1, tw + 2, th + 2, 7, HEX(0xA9A9A9), HEX(0xC9C9C9));
    rrect(tx, ty, tw, th, 6, HEX(0xD6D6D6), HEX(0xF2F2F2));
    float pos = (float)(ms % 1600) / 1600.0f * (tw + bw) - bw;
    float x0 = pos < 0 ? 0 : pos, x1 = pos + bw > tw ? tw : pos + bw;
    if (x1 - x0 > 2) rrect(tx + x0, ty, x1 - x0, th, 6, HEX(0x7DD3F7), HEX(0x1C93D6));

    char name[160];
    fit(st->name ? st->name : "", pw - 30, SC_TINY, false, name, sizeof name);
    text(name, BOT_W / 2.0f, py + 118, SC_TINY, C_TEXT3, C2D_AlignCenter);

    footer_bar();
    button(kFooter[FB_MID].x, kFooter[FB_MID].y, kFooter[FB_MID].w,
           kFooter[FB_MID].h, BTN_NORMAL);
    glyph_label(&kFooter[FB_MID], "SELECT", "Cancel", SC_SMALL);

    C3D_FrameEnd(0);
}

void render_message(const char *title, const char *body)
{
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

    C2D_TargetClear(g_top, C_BG_TOP);
    C2D_SceneBegin(g_top);
    C2D_TextBufClear(g_txt_top);
    g_cur = g_txt_top;
    top_title(title);

    C2D_TargetClear(g_bot, C_BG_TOP);
    C2D_SceneBegin(g_bot);
    C2D_TextBufClear(g_txt_bot);
    g_cur = g_txt_bot;
    background(BOT_W, BOT_H);
    title_bar(BOT_W, "What would you like to do?", NULL, true);
    panel(16, 44, BOT_W - 32, 80);
    text_mid(body, BOT_W / 2.0f, 44, 80, SC_NORMAL, C_TEXT, C2D_AlignCenter);
    button(kMsgBtn[MB_RETRY].x, kMsgBtn[MB_RETRY].y, kMsgBtn[MB_RETRY].w,
           kMsgBtn[MB_RETRY].h, BTN_SELECTED);
    glyph_label(&kMsgBtn[MB_RETRY], "A", "Scan again", SC_NORMAL);
    button(kMsgBtn[MB_CLOSE].x, kMsgBtn[MB_CLOSE].y, kMsgBtn[MB_CLOSE].w,
           kMsgBtn[MB_CLOSE].h, BTN_NORMAL);
    glyph_label(&kMsgBtn[MB_CLOSE], "START", "Close", SC_NORMAL);

    C3D_FrameEnd(0);
}
