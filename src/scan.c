// scan.c - SD card scanning with the modern libctru FSUSER/FSDIR API
#include "scan.h"
#include "tree.h"

#include <3ds.h>

#include <string.h>

#define PROGRESS_EVERY 32
#define UTF16_NAME_LEN 0x106
// Maximum length (UTF-16 code units, excluding the terminator) of a scanned
// path. Directories whose path would exceed this are listed but not entered.
#define PATH_MAX16 1024

// Shared scan state. The scan runs on a single thread and is not re-entrant,
// so the large buffers live here instead of on the stack: Azahar gives the main
// thread only 16 KB of stack (Kernel::DEFAULT_STACK_SIZE) and ignores the
// .3dsx header's stack size field.
typedef struct {
    FS_Archive    archive;
    Tree         *tree;
    int           maxDepth;
    scan_progress cb;
    void         *userdata;
    ScanStatus    st;
    int           cancelled;
    int           oom;

    // Path of the directory being read, as a UTF-16 absolute path ("/a/b").
    // Each recursion level appends "/<name>" before descending and truncates
    // it back to its own length afterwards, so one buffer serves every level.
    u16                path[PATH_MAX16 + 1];
    size_t             pathLen;
    FS_DirectoryEntry  entry;
    char               name[UTF16_NAME_LEN * 3 + 1]; // UTF-8 of entry.name
} ScanCtx;

static ScanCtx g_scan;

// Convert a NUL-terminated UTF-16 name into a NUL-terminated UTF-8 string.
// utf16_to_utf8's `len` is the OUTPUT capacity; it never writes more than that
// but returns the full length the conversion needed, which can be larger.
static void utf16_to_c(char *out, size_t outn, const u16 *in)
{
    if (outn == 0) return;
    ssize_t w = utf16_to_utf8((uint8_t *)out, in, outn - 1);
    if (w < 0) w = 0;
    if ((size_t)w > outn - 1) w = (ssize_t)(outn - 1);
    out[w] = '\0';
}

static size_t u16len(const u16 *s, size_t max)
{
    size_t n = 0;
    while (n < max && s[n] != 0) n++;
    return n;
}

// Report progress every PROGRESS_EVERY entries; the callback may cancel.
static void progress(ScanCtx *c)
{
    if ((c->st.files + c->st.dirs) % PROGRESS_EVERY != 0 || !c->cb) return;
    c->st.name = c->name;
    if (c->cb(&c->st, c->userdata)) c->cancelled = 1;
}

static FS_Path current_path(const ScanCtx *c)
{
    FS_Path p;
    p.type = PATH_UTF16;
    p.size = (u32)((c->pathLen + 1) * sizeof(u16));
    p.data = c->path;
    return p;
}

static void scan_sub(ScanCtx *c, DirEntry *parent, int depth)
{
    Handle dirHandle = 0;
    if (FSUSER_OpenDirectory(&dirHandle, c->archive, current_path(c)) != 0)
        return;

    // Read one entry per syscall. `c->entry` and `c->name` are shared with the
    // recursive calls, so everything needed from them is consumed before
    // descending.
    u32 n;
    while (!c->cancelled && !c->oom &&
           FSDIR_Read(dirHandle, &n, 1, &c->entry) == 0 && n > 0) {
        utf16_to_c(c->name, sizeof c->name, c->entry.name);
        if (strcmp(c->name, ".") == 0 || strcmp(c->name, "..") == 0) continue;

        bool is_dir = (c->entry.attributes & FS_ATTRIBUTE_DIRECTORY) != 0;

        if (is_dir) {
            DirEntry *de = tree_add_child(c->tree, parent, c->name, 1, 0);
            if (!de) { c->oom = 1; break; }
            c->st.dirs++;
            progress(c);
            if (depth >= c->maxDepth) continue;

            size_t base = c->pathLen;
            size_t sep  = (base == 1) ? 0 : 1;   // root is "/", no extra slash
            size_t nlen = u16len(c->entry.name, UTF16_NAME_LEN);
            if (base + sep + nlen > PATH_MAX16) continue;

            if (sep) c->path[base] = '/';
            memcpy(&c->path[base + sep], c->entry.name, nlen * sizeof(u16));
            c->pathLen = base + sep + nlen;
            c->path[c->pathLen] = 0;

            scan_sub(c, de, depth + 1);

            c->pathLen = base;
            c->path[base] = 0;
        } else {
            if (!tree_add_child(c->tree, parent, c->name, 0, c->entry.fileSize)) {
                c->oom = 1;
                break;
            }
            c->st.files++;
            c->st.bytes += c->entry.fileSize;
            progress(c);
        }
    }
    FSDIR_Close(dirHandle);
}

int scan_sd(Tree *t, int maxDepth, scan_progress cb, void *userdata)
{
    Result rc = fsInit();
    if (rc != 0) return -1;

    ScanCtx *c = &g_scan;
    memset(c, 0, sizeof *c);
    c->tree     = t;
    c->maxDepth = maxDepth;
    c->cb       = cb;
    c->userdata = userdata;

    rc = FSUSER_OpenArchive(&c->archive, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, NULL));
    if (rc != 0) {
        fsExit();
        return -1;
    }

    // Azahar rejects PATH_EMPTY for directories ("Invalid path [Empty]"), so the
    // archive root is addressed explicitly as "/".
    c->path[0] = '/';
    c->path[1] = 0;
    c->pathLen = 1;
    if (cb) {
        c->st.name = "";
        if (cb(&c->st, userdata)) c->cancelled = 1;
    }
    if (!c->cancelled)
        scan_sub(c, t->root, 0);

    FSUSER_CloseArchive(c->archive);
    fsExit();

    if (c->oom) return -3;

    tree_compute_sizes(t);
    tree_sort(t);
    tree_classify(t);

    if (c->cancelled) return -2;
    return 0;
}
