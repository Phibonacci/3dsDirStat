// treemap.h - squarified treemap layout (platform independent)
#ifndef TREEMAP_H
#define TREEMAP_H

#include "tree.h"

typedef struct {
    float x, y, w, h;
} Rect;

// Layout the subtree rooted at `root` (its children) inside the pixel rectangle
// (px,py,pw,ph). Directories are subdivided recursively down to `depth` levels
// and only when their rectangle is at least `minArea` pixels, otherwise they are
// left as flat blocks. Each placed entry receives an absolute pixel rectangle in
// rx,ry,rw,rh relative to (px,py), and `expanded` tells whether its children
// were laid out too.
void layout_tree(DirEntry *root, float px, float py, float pw, float ph,
                 int depth, float minArea);

// Find the deepest entry whose rectangle contains (px,py), searching at most
// `depth` levels below `root`. Returns NULL if nothing matches. Used for hit
// testing the touch/stylus position.
DirEntry *find_entry_at(DirEntry *root, float px, float py, int depth);

// Iterate every entry that would be drawn for a given view (descending into
// directories that layout_tree expanded) calling `visit(entry, depth, userdata)` for each. Used by the
// renderer and by tests to enumerate the drawn set.
void for_each_drawn(DirEntry *root, int depth, float minArea,
                    void (*visit)(DirEntry *, int, void *), void *userdata);

#endif // TREEMAP_H
