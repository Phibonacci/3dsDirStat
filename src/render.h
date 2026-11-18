// render.h - screen drawing (citro2d)
//
// Visual style: light "system settings" look - striped light-grey background,
// grey gradient title bars, large rounded white buttons, a pulsing blue
// selection cursor and a bottom bar with on-screen buttons.
#ifndef RENDER_H
#define RENDER_H

#include "ui.h"
#include "scan.h"

// Screen geometry (pixels). The top LCD is 400x240, the bottom one 320x240.
#define TOP_W     400
#define TOP_H     240
#define BOT_W     320
#define BOT_H     240

// Top screen: title bar, treemap panel, description panel.
#define TOP_HDR_H 22
// The map sits in a rounded panel; MAP_PAD keeps its square corners (and the
// selection ring, 2 px outside the cells) clear of the panel's rounded corners.
#define MAP_PAD   6.0f
#define MAP_X     (4.0f + MAP_PAD)
#define MAP_Y     (TOP_HDR_H + 3.0f + MAP_PAD)
#define MAP_W     (TOP_W - 8.0f - 2 * MAP_PAD)
#define MAP_H     160.0f
#define DESC_Y    (MAP_Y + MAP_H + MAP_PAD + 4.0f)

// Bottom screen: title bar, list of buttons, bottom bar.
#define BOT_HDR_H 22
#define LIST_Y    (BOT_HDR_H + 4)
#define ROW_H     31          // button height + gap
#define ROW_BTN_H 28
#define LIST_ROWS 6
#define FOOTER_Y  (BOT_H - 28)

// Bottom bar buttons (tap targets). The middle one is "Delete" on the folder
// list and "Rescan" on the file-types page.
enum { FB_NONE = -1, FB_BACK = 0, FB_MID, FB_TYPES, FB_COUNT };
int render_footer_button_at(int x, int y);

// Dialog buttons: two-button dialogs have Cancel + Confirm, one-button
// dialogs only Confirm ("OK").
enum { DB_NONE = -1, DB_CANCEL = 0, DB_CONFIRM };
int render_dialog_button_at(const ui_state *s, int x, int y);

// Message screen buttons.
enum { MB_NONE = -1, MB_RETRY = 0, MB_CLOSE };
int render_message_button_at(int x, int y);

void render_init(void);
void render_fini(void);

// Draw both screens for the interactive view.
void render_frame(const ui_state *s);

// Draw the scan progress screens. `ms` is the time since the scan started.
void render_scan(const ScanStatus *st, uint64_t ms);

// Draw the "scan stopped" screen with its two buttons.
void render_message(const char *title, const char *body);

#endif // RENDER_H
