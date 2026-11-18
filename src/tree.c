// tree.c - filesystem tree data structure implementation
#define _POSIX_C_SOURCE 200809L
#include "tree.h"
#include "colors.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static void free_entry(DirEntry *e)
{
    if (!e) return;
    free(e->name);
    free(e->ext);
    free(e->children);
    free(e);
}

static DirEntry *new_entry(const char *name)
{
    DirEntry *e = calloc(1, sizeof(DirEntry));
    if (!e) return NULL;

    e->name = strdup(name ? name : "");
    char extbuf[64];
    size_t elen = tree_extension(e->name, extbuf, sizeof extbuf);
    e->ext = malloc(elen + 1);
    if (!e->name || !e->ext) {
        free_entry(e);
        return NULL;
    }
    memcpy(e->ext, extbuf, elen + 1);
    e->category = -1;
    return e;
}

int tree_init(Tree *t)
{
    memset(t, 0, sizeof(*t));

    t->capnodes = 64;
    t->nodes    = malloc(t->capnodes * sizeof(DirEntry *));
    // Virtual root representing the scan root (SD:/). It owns the top level.
    t->root     = new_entry("");
    if (!t->nodes || !t->root) {
        free(t->nodes);
        free_entry(t->root);
        memset(t, 0, sizeof(*t));
        return -1;
    }
    t->root->is_dir = 1;
    t->nodes[t->nnodes++] = t->root;
    return 0;
}

void tree_free(Tree *t)
{
    for (int i = 0; i < t->nnodes; i++)
        free_entry(t->nodes[i]);
    free(t->nodes);
    memset(t, 0, sizeof(*t));
}

DirEntry *tree_add_child(Tree *t, DirEntry *parent, const char *name,
                         int is_dir, uint64_t size)
{
    if (!parent) parent = t->root;

    // Grow both arrays before creating the entry so a failure leaves the tree
    // unchanged and nothing leaks.
    if (parent->nchildren == parent->cap) {
        int ncap = parent->cap ? parent->cap * 2 : 16;
        DirEntry **nc = realloc(parent->children, ncap * sizeof(DirEntry *));
        if (!nc) return NULL;
        parent->children = nc;
        parent->cap      = ncap;
    }
    if (t->nnodes == t->capnodes) {
        int ncap = t->capnodes ? t->capnodes * 2 : 256;
        DirEntry **nn = realloc(t->nodes, ncap * sizeof(DirEntry *));
        if (!nn) return NULL;
        t->nodes    = nn;
        t->capnodes = ncap;
    }

    DirEntry *e = new_entry(name);
    if (!e) return NULL;
    e->is_dir = is_dir;
    e->size   = size;
    e->nfiles = is_dir ? 0 : 1;
    e->parent = parent;

    parent->children[parent->nchildren++] = e;
    t->nodes[t->nnodes++] = e;
    return e;
}

// Extract the lowercased extension (without a leading dot) of a file name.
size_t tree_extension(const char *name, char *out, size_t n)
{
    if (n == 0) return 0;
    out[0] = '\0';
    if (!name) return 0;

    const char *dot = strrchr(name, '.');
    // Require a dot that is not the first character and not the last one,
    // i.e. ".hidden" or "Makefile" have no extension.
    if (!dot || dot == name || dot[1] == '\0') return 0;

    size_t len = strlen(dot + 1);
    if (len >= n) len = n - 1;
    for (size_t i = 0; i < len; i++)
        out[i] = (char)tolower((unsigned char)dot[i + 1]);
    out[len] = '\0';
    return len;
}

void tree_compute_sizes(Tree *t)
{
    // Nodes are created parent-before-children, so walking in reverse order
    // guarantees every descendant is added to its ancestor before the ancestor.
    for (int i = t->nnodes - 1; i >= 1; i--) {
        DirEntry *e = t->nodes[i];
        if (e->parent) {
            e->parent->size   += e->size;
            e->parent->nfiles += e->nfiles;
        }
    }
}

static int cmp_size_desc(const void *a, const void *b)
{
    const DirEntry *x = *(const DirEntry *const *)a;
    const DirEntry *y = *(const DirEntry *const *)b;
    if (x->size != y->size) return x->size < y->size ? 1 : -1;
    return strcmp(x->name, y->name);
}

void tree_sort(Tree *t)
{
    for (int i = 0; i < t->nnodes; i++) {
        DirEntry *e = t->nodes[i];
        if (e->nchildren > 1)
            qsort(e->children, (size_t)e->nchildren, sizeof(DirEntry *), cmp_size_desc);
    }
}

// Category of the largest descendant file of a directory.
static int largest_file_category(DirEntry *e)
{
    if (!e->is_dir)
        return category_of_ext(e->ext);

    DirEntry *best = NULL;
    for (int i = 0; i < e->nchildren; i++) {
        if (!best || e->children[i]->size > best->size)
            best = e->children[i];
    }
    if (!best)
        return CAT_DIR;
    return largest_file_category(best);
}

void tree_classify(Tree *t)
{
    for (int i = 0; i < t->nnodes; i++)
        t->nodes[i]->category = largest_file_category(t->nodes[i]);
}
