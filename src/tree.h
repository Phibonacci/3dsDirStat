// tree.h - filesystem tree data structure (platform independent)
// Holds the scanned directory tree, computes directory sizes and color
// categories. No libctru dependency so it can be unit tested on a host.
#ifndef TREE_H
#define TREE_H

#include <stdint.h>
#include <stddef.h>

typedef struct DirEntry DirEntry;

struct DirEntry {
    char     *name;        // owned, NUL terminated
    char     *ext;         // owned, lowercased extension without a dot, "" if none
    int       is_dir;
    uint64_t  size;        // bytes: files keep their own size, dirs filled by tree_compute_sizes
    uint32_t  nfiles;      // files in this subtree (1 for a file), see tree_compute_sizes
    DirEntry *parent;

    DirEntry **children;   // owned
    int        nchildren;
    int        cap;

    // Treemap layout: pixel rectangle relative to the viewport origin.
    float      rx, ry, rw, rh;
    int        expanded;   // set by layout_tree: children have valid rectangles
    int        category;   // color category index (see colors.h)
};

typedef struct {
    DirEntry *root;        // virtual root representing the scan root (e.g. SD:/)
    DirEntry **nodes;      // owned, flat list in creation order (parent before children)
    int       nnodes;
    int       capnodes;
} Tree;

// Returns 0 on success, -1 if memory ran out (`t` is then zeroed).
int    tree_init(Tree *t);
void   tree_free(Tree *t);

// Append a node as a child of `parent` (may be the virtual root).
// Returns the created node, or NULL if memory ran out (the tree is unchanged).
DirEntry *tree_add_child(Tree *t, DirEntry *parent, const char *name,
                         int is_dir, uint64_t size);

// Accumulate every node's size and file count into its parent. After this,
// directories hold the total size and number of files of their whole subtree. Must be called after the tree is built.
void   tree_compute_sizes(Tree *t);

// Sort every directory's children by size, largest first (ties by name).
void   tree_sort(Tree *t);

// Remove `e` and its whole subtree from the tree and free it (after it has been
// deleted from the card). Ancestors lose its size and file count, are
// re-sorted among their siblings and re-classified. `e` must not be the root.
void   tree_remove(Tree *t, DirEntry *e);

// The ancestor of `e` that sits directly in the root (e itself if it does),
// or NULL for the root.
const DirEntry *tree_top_level(const DirEntry *e);

// Assign a color category to every entry. Files use their extension; directories
// inherit the category of their largest descendant file so categories group nicely.
void   tree_classify(Tree *t);

// Fill `out` (length `n`) with the lowercased extension of `name` without a dot.
// Returns the number of characters written (0 if there is no usable extension).
size_t tree_extension(const char *name, char *out, size_t n);

#endif // TREE_H
