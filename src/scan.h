// scan.h - SD card scanning (libctru)
#ifndef SCAN_H
#define SCAN_H

#include "tree.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint64_t    files;   // files seen so far
    uint64_t    dirs;    // folders seen so far
    uint64_t    bytes;   // total size of the files seen so far
    const char *name;    // most recently visited entry
} ScanStatus;

// Called regularly during the scan (cheap to call: throttle drawing yourself).
// Return true to cancel the scan.
typedef bool (*scan_progress)(const ScanStatus *st, void *userdata);

// Scan the SD card into `t`. `maxDepth` is the maximum directory recursion depth
// (0 = top level only). `cb`/`userdata` receive scan progress and may cancel the
// scan. On success the tree has its sizes computed, children sorted by size and
// categories assigned. Returns 0 on success, -1 on FS error, -2 if the
// user cancelled, -3 if memory ran out (the tree is then partial and must be
// discarded).
int scan_sd(Tree *t, int maxDepth, scan_progress cb, void *userdata);

#endif // SCAN_H
