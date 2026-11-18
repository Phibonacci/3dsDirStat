// fsops.c - file operations on the SD card (libctru)
#include "fsops.h"

#include <stdio.h>
#include <string.h>

#define PATH_MAX16 1024

// Absolute UTF-16 path of `e` ("/a/b/c"), NUL terminated. Returns its length
// in code units, or -1 if it does not fit.
static int path16_of(const DirEntry *e, u16 *out, size_t cap)
{
    const DirEntry *chain[128];
    int depth = 0;
    for (; e && e->parent; e = e->parent) {
        if (depth == 128) return -1;
        chain[depth++] = e;
    }
    size_t n = 0;
    for (int i = depth - 1; i >= 0; i--) {
        if (n + 1 >= cap) return -1;
        out[n++] = '/';
        const char *name = chain[i]->name;
        ssize_t w = utf8_to_utf16(out + n, (const uint8_t *)name, cap - n - 1);
        if (w < 0 || n + (size_t)w >= cap - 1) return -1;
        n += (size_t)w;
    }
    if (n == 0) return -1;     // the root itself is never deleted
    out[n] = 0;
    return (int)n;
}

Result fs_delete_entry(const DirEntry *e)
{
    static u16 path[PATH_MAX16 + 1];
    int len = path16_of(e, path, sizeof path / sizeof path[0]);
    if (len < 0) return -1;

    Result rc = fsInit();
    if (R_FAILED(rc)) return rc;
    FS_Archive arc = 0;
    rc = FSUSER_OpenArchive(&arc, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, NULL));
    if (R_SUCCEEDED(rc)) {
        FS_Path p = { PATH_UTF16, (u32)((len + 1) * sizeof(u16)), path };
        rc = e->is_dir ? FSUSER_DeleteDirectoryRecursively(arc, p)
                       : FSUSER_DeleteFile(arc, p);
        FSUSER_CloseArchive(arc);
    }
    fsExit();
    return rc;
}
