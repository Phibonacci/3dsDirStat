// treemap.c - squarified treemap layout
//
// Implements the squarified treemap algorithm (Bruls, Huizing & van Wijk,
// "Squarified Treemaps", 2000). Weights (file sizes) are normalized so that the
// sum of the children's areas equals the parent rectangle's area, then the
// rectangle is filled with rows of items chosen to keep aspect ratios close to
// square.
#include "treemap.h"

#include <stdlib.h>
#include <math.h>

static float fmaxf_local(float a, float b) { return a > b ? a : b; }
static float fminf_local(float a, float b) { return a < b ? a : b; }

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

// Worst aspect ratio introduced by a row of total area `s` and max item area `w`
// inside a rectangle with squared long side `r2` and short side `rshort`.
static float worst_area(float s, float w, float r2, float rshort)
{
    float a = s + w;
    float b = s - w;
    return (2.0f * a * rshort) / (b * r2 + w * w);
}

// Assign rectangles to a finalized row of `len` cells inside rectangle `r`.
static void place_row(Rect r, Cell *row, int len)
{
    float s = 0.0f;
    for (int j = 0; j < len; j++) s += row[j].area;

    if (len <= 0 || s <= 0.0f) {
        // Degenerate: give each cell an equal slice so nothing is lost.
        if (r.w >= r.h) {
            float slice = r.h / (float)len;
            for (int j = 0; j < len; j++) {
                Cell *c = &row[j];
                c->e->rx = r.x; c->e->ry = r.y + (float)j * slice;
                c->e->rw = r.w; c->e->rh = slice;
            }
        } else {
            float slice = r.w / (float)len;
            for (int j = 0; j < len; j++) {
                Cell *c = &row[j];
                c->e->rx = r.x + (float)j * slice; c->e->ry = r.y;
                c->e->rw = slice; c->e->rh = r.h;
            }
        }
        return;
    }

    if (r.w >= r.h) {
        // Horizontal strip: full width, height proportional to row area.
        float thickness = s / r.w;
        float x = r.x;
        for (int j = 0; j < len; j++) {
            Cell *c = &row[j];
            float w_ = c->area / thickness;
            c->e->rx = x;   c->e->ry = r.y;
            c->e->rw = w_;  c->e->rh = thickness;
            x += w_;
        }
    } else {
        // Vertical strip: full height, width proportional to row area.
        float thickness = s / r.h;
        float y = r.y;
        for (int j = 0; j < len; j++) {
            Cell *c = &row[j];
            float h_ = c->area / thickness;
            c->e->rx = r.x; c->e->ry = y;
            c->e->rw = thickness; c->e->rh = h_;
            y += h_;
        }
    }
}

// Fill rectangle `r` with the (already descending sorted) cells using squaring.
static void squarified(Rect r, Cell *items, int n)
{
    if (n <= 0 || r.w <= 0.0f || r.h <= 0.0f) return;

    Cell *row = malloc((size_t)n * sizeof(Cell));
    if (!row) return;
    int    rowLen = 0;
    int    i = 0;

    while (i < n) {
        float s = 0.0f, mx = 0.0f;
        for (int j = 0; j < rowLen; j++) {
            s += row[j].area;
            if (row[j].area > mx) mx = row[j].area;
        }
        float r2     = fmaxf_local(r.w, r.h) * fmaxf_local(r.w, r.h);
        float rshort = fminf_local(r.w, r.h);
        float curWorst = rowLen == 0 ? 1e30f : worst_area(s, mx, r2, rshort);

        float na   = items[i].area;
        float addWorst = worst_area(s + na, fmaxf_local(mx, na), r2, rshort);

        if (rowLen == 0 || addWorst <= curWorst) {
            row[rowLen++] = items[i];
            i++;
        } else {
            place_row(r, row, rowLen);
            if (r.w >= r.h) {
                float t = s / r.w;
                r.y += t; r.h -= t;
            } else {
                float t = s / r.h;
                r.x += t; r.w -= t;
            }
            rowLen = 0;
        }
    }
    if (rowLen > 0)
        place_row(r, row, rowLen);

    free(row);
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
