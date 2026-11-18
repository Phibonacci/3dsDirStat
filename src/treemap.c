// treemap.c - squarified treemap layout
//
// Implements the squarified treemap algorithm (Bruls, Huizing & van Wijk,
// "Squarified Treemaps", 2000). Weights (file sizes) are normalized so that the
// sum of the children's areas equals the parent rectangle's area, then the
// rectangle is filled with rows of items chosen to keep aspect ratios close to
// square.
#include "treemap.h"

#include <stdlib.h>

typedef struct {
    DirEntry *e;
    float     area;
} Cell;

static int cmp_area_desc(const void *a, const void *b)
{
    float da = ((const Cell *)a)->area;
    float db = ((const Cell *)b)->area;
    if (da < db) return 1;
    if (da > db) return -1;
    return 0;
}

static void set_rect(DirEntry *e, float x, float y, float w, float h)
{
    e->rx = x; e->ry = y; e->rw = w; e->rh = h;
}

// Worst aspect ratio (>= 1) in a row of total area `s` whose largest and
// smallest items are `rmax` / `rmin`, laid along a side of length `side`
// (Bruls et al., eq. for worst(R, w)).
static float worst(float s, float rmax, float rmin, float side)
{
    float s2 = s * s, w2 = side * side;
    float a = w2 * rmax / s2;
    float b = s2 / (w2 * rmin);
    return a > b ? a : b;
}

// Lay out one row as a strip along the SHORT side of `r` and return what is
// left of `r`. Lengths along the strip are proportional to the items' areas, so
// the strip is filled exactly; the last row also takes all remaining space so
// rounding errors never leave a gap.
static Rect place_row(Rect r, const Cell *row, int len, float s, int last)
{
    if (r.w >= r.h) {
        // Wide: a column on the left, items stacked top to bottom.
        float t = last ? r.w : s / r.h;
        float y = r.y;
        for (int j = 0; j < len; j++) {
            float h = j == len - 1 ? r.y + r.h - y : r.h * row[j].area / s;
            set_rect(row[j].e, r.x, y, t, h);
            y += h;
        }
        r.x += t;
        r.w -= t;
    } else {
        // Tall: a row at the top, items side by side.
        float t = last ? r.h : s / r.w;
        float x = r.x;
        for (int j = 0; j < len; j++) {
            float w = j == len - 1 ? r.x + r.w - x : r.w * row[j].area / s;
            set_rect(row[j].e, x, r.y, w, t);
            x += w;
        }
        r.y += t;
        r.h -= t;
    }
    if (r.w < 0.0f) r.w = 0.0f;
    if (r.h < 0.0f) r.h = 0.0f;
    return r;
}

// Fill rectangle `r` with the cells (sorted by area, largest first). Each row
// grows while adding the next item does not make its worst aspect ratio worse,
// which keeps cells close to square.
static void squarified(Rect r, Cell *items, int n)
{
    // Zero-sized items (empty files/folders) get an empty rectangle.
    int m = n;
    while (m > 0 && items[m - 1].area <= 0.0f) m--;
    for (int i = m; i < n; i++) set_rect(items[i].e, r.x, r.y, 0, 0);

    int i = 0;
    while (i < m) {
        float side = r.w < r.h ? r.w : r.h;
        if (side <= 0.0f) {
            for (; i < m; i++) set_rect(items[i].e, r.x, r.y, 0, 0);
            break;
        }
        int   start = i;
        float s     = items[i].area;
        float cur   = worst(s, items[start].area, items[i].area, side);
        for (i++; i < m; i++) {
            float ns = s + items[i].area;
            float nw = worst(ns, items[start].area, items[i].area, side);
            if (nw > cur) break;
            s   = ns;
            cur = nw;
        }
        r = place_row(r, items + start, i - start, s, i == m);
    }
}

void layout_tree(DirEntry *root, float px, float py, float pw, float ph,
                 int depth, float minArea)
{
    if (!root) return;
    // Children only get fresh rectangles below; mark them all unexpanded first
    // so nothing descends into rectangles left over from an earlier layout.
    for (int i = 0; i < root->nchildren; i++)
        root->children[i]->expanded = 0;
    if (depth <= 0 || pw <= 0.0f || ph <= 0.0f) return;

    float rectWeight = 0.0f;
    for (int i = 0; i < root->nchildren; i++)
        rectWeight += (float)root->children[i]->size;
    if (rectWeight <= 0.0f) return;

    float area = pw * ph;
    float factor = area / rectWeight;

    Cell *cells = malloc((size_t)root->nchildren * sizeof(Cell));
    if (!cells) return;
    for (int i = 0; i < root->nchildren; i++) {
        cells[i].e    = root->children[i];
        cells[i].area = (float)root->children[i]->size * factor;
    }
    qsort(cells, (size_t)root->nchildren, sizeof(Cell), cmp_area_desc);
    squarified((Rect){px, py, pw, ph}, cells, root->nchildren);
    free(cells);

    if (depth > 1) {
        for (int i = 0; i < root->nchildren; i++) {
            DirEntry *c = root->children[i];
            if (c->is_dir && c->size > 0 &&
                c->rw * c->rh >= minArea) {
                c->expanded = 1;
                layout_tree(c, c->rx, c->ry, c->rw, c->rh, depth - 1, minArea);
            }
        }
    }
}

static int pt_in(DirEntry *e, float px, float py)
{
    return px >= e->rx && px <= e->rx + e->rw &&
           py >= e->ry && py <= e->ry + e->rh;
}

DirEntry *find_entry_at(DirEntry *root, float px, float py, int depth)
{
    if (depth <= 0 || !root) return NULL;

    DirEntry *best = NULL;
    for (int i = 0; i < root->nchildren; i++) {
        DirEntry *c = root->children[i];
        if (pt_in(c, px, py)) {
            best = c;
            if (c->expanded) {
                DirEntry *inner = find_entry_at(c, px, py, depth - 1);
                if (inner) best = inner;
            }
            break;
        }
    }
    return best;
}

void for_each_drawn(DirEntry *root, int depth, float minArea,
                    void (*visit)(DirEntry *, int, void *), void *userdata)
{
    if (depth <= 0 || !root) return;
    for (int i = 0; i < root->nchildren; i++) {
        DirEntry *c = root->children[i];
        visit(c, depth, userdata);
        (void)minArea;
        if (c->expanded)
            for_each_drawn(c, depth - 1, minArea, visit, userdata);
    }
}
