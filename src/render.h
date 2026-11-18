// render.h - screen drawing (citro2d)
#ifndef RENDER_H
#define RENDER_H

#include "ui.h"
#include "scan.h"

// Screen geometry (pixels). The top LCD is 400x240, the bottom one 320x240.
#define TOP_W     400
#define TOP_H     240
#define BOT_W     320
#define BOT_H     240

// Top screen: header bar, then the treemap filling the rest.
#define TOP_HDR_H 18
#define MAP_X     1.0f
#define MAP_Y     (TOP_HDR_H + 1.0f)
#define MAP_W     (TOP_W - 2.0f)
#define MAP_H     (TOP_H - TOP_HDR_H - 2.0f)

// Bottom screen: header, list rows, footer.
#define BOT_HDR_H 22
#define ROW_H     18
#define LIST_ROWS 10
#define LIST_Y    BOT_HDR_H
#define FOOTER_Y  (LIST_Y + ROW_H * LIST_ROWS)
#define BACK_BTN_W 44   // "Up" button at the left of the bottom header

void render_init(void);
void render_fini(void);

// Draw both screens for the interactive view.
void render_frame(const ui_state *s);

// Draw the scan progress screens. `ms` is the time since the scan started.
void render_scan(const ScanStatus *st, uint64_t ms);

// Draw a message screen (title + body + hint line).
void render_message(const char *title, const char *body, const char *hint);

#endif // RENDER_H
