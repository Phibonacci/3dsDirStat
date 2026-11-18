// ui.h - application state and main loop (libctru)
#ifndef UI_H
#define UI_H

#include "tree.h"
#include "colors.h"

#include <stdbool.h>
#include <stdint.h>

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
} ui_state;

// Scan the SD card (with a progress screen) then run the interactive loop.
// `maxDepth` is the scan recursion depth. Returns 0 when the user quits.
int ui_run(int maxDepth);

#endif // UI_H
