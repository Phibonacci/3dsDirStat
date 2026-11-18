// ui.h - application state and main loop (libctru)
#ifndef UI_H
#define UI_H

#include "tree.h"
#include "colors.h"

#include <stdbool.h>
#include <stdint.h>

// Modal dialog shown over the bottom screen.
typedef enum {
    DLG_NONE,
    DLG_DELETE,     // "Delete ...?" confirmation
    DLG_SYSTEM,     // entry belongs to "Nintendo 3DS": use System Settings
    DLG_ERROR,      // deletion failed
    DLG_BUSY,       // deletion in progress
} DialogKind;

typedef struct {
    Tree     *tree;
    DirEntry *dir;          // folder currently shown
    int       sel;          // selected child of `dir` (index), -1 if empty
    int       scroll;       // first list row shown on the bottom screen
    bool      legend;       // bottom screen shows the file-type legend
    bool      running;

    // Bytes per file category inside `dir`, for the legend.
    uint64_t  catBytes[CAT_COUNT];

    // Stylus tracking for tap / drag-to-scroll on the bottom screen.
    bool      touching;
    bool      dragged;
    int       touchX, touchY;   // last position while held
    int       touchY0;          // position where the touch started
    int       scroll0;          // scroll when the touch started

    // Dialog state.
    DialogKind dlg;
    DirEntry  *dlgEntry;        // entry the dialog is about
    long       dlgError;        // FS result code for DLG_ERROR
    char       dlgName[256];    // its name (the entry may be gone by then)
} ui_state;

// True for "Nintendo 3DS" at the SD root and everything inside it: console
// managed data (software, saves, extra data) that must be removed from
// System Settings > Data Management, never deleted here.
bool ui_is_system_data(const DirEntry *e);

// True for entries a custom-firmware console needs to start ("boot.firm",
// "luma/" at the SD root). Deleting them is allowed but warned about.
bool ui_is_boot_file(const DirEntry *e);

// Scan the SD card (with a progress screen) then run the interactive loop.
// `maxDepth` is the scan recursion depth. Returns 0 when the user quits.
int ui_run(int maxDepth);

#endif // UI_H
