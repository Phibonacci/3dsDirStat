// fsops.h - file operations on the SD card (libctru)
#ifndef FSOPS_H
#define FSOPS_H

#include "tree.h"

#include <3ds.h>

// Delete the file or folder (with everything in it) that `e` stands for.
// Returns 0 on success or the FS error code. A folder that fails half-way may
// be partly deleted.
Result fs_delete_entry(const DirEntry *e);

#endif // FSOPS_H
