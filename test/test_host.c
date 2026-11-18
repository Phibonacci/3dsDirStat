// test_host.c - host side verification of the pure logic (treemap, sizes, colors).
// Build with: gcc -std=c11 -Wall -Isrc test/test_host.c src/treemap.c src/tree.c
//               src/colors.c src/sizes.c -lm -o test_host
#include "../src/treemap.h"
#include "../src/tree.h"
#include "../src/colors.h"
#include "../src/sizes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s\n", (msg)); g_failures++; } \
} while (0)

// Enumerate drawn entries and accumulate, per parent, area + containment checks.
typedef struct {
    DirEntry *parent;
    float     total_area;
    float     expect_area;   // parent rect area
    int       ok;
} ParentCheck;

static ParentCheck g_checks[128];
static int         g_nchecks = 0;

static void record(DirEntry *e, int depth, void *ud)
{
    (void)ud; (void)depth;
    // Find the nearest ancestor that is a layout parent (its children include e).
    for (int c = 0; c < g_nchecks; c++) {
        if (g_checks[c].parent == e->parent) {
            g_checks[c].total_area += e->rw * e->rh;
            // containment within parent rect
            if (e->rx < e->parent->rx - 0.5f || e->ry < e->parent->ry - 0.5f ||
                e->rx + e->rw > e->parent->rx + e->parent->rw + 0.5f ||
                e->ry + e->rh > e->parent->ry + e->parent->rh + 0.5f) {
                g_checks[c].ok = 0;
            }
            return;
        }
    }
    // e->parent not yet seen: register it.
    ParentCheck *pc = &g_checks[g_nchecks++];
    pc->parent = e->parent;
    pc->expect_area = e->parent->rw * e->parent->rh;
    pc->total_area = e->rw * e->rh;
    pc->ok = 1;
}

// Build a synthetic tree with known sizes so we can assert on the layout.
static void build_known(Tree *t)
{
    DirEntry *root = t->root;

    DirEntry *d1 = tree_add_child(t, root, "Games", 1, 0);
    tree_add_child(t, d1, "Nest", 1, 0);
    DirEntry *nest = d1->children[0];
    tree_add_child(t, nest, "data.bin", 0, 300);
    tree_add_child(t, nest, "icon.png", 0, 100);
    tree_add_child(t, nest, "sound.ogg", 0, 50);
    tree_add_child(t, d1, "meta.txt", 0, 20);

    DirEntry *d2 = tree_add_child(t, root, "Saves", 1, 0);
    tree_add_child(t, d2, "save1.sav", 0, 4000);
    tree_add_child(t, d2, "save2.sav", 0, 3000);

    DirEntry *d3 = tree_add_child(t, root, "Photos", 1, 0);
    tree_add_child(t, d3, "a.jpg", 0, 2000);
    tree_add_child(t, d3, "b.jpg", 0, 1500);
    tree_add_child(t, d3, "c.png", 0, 500);

    tree_add_child(t, root, "readme.txt", 0, 10);
    tree_add_child(t, root, "home.3dsx", 0, 800000); // ~ 0.76 MB homebrew
}

static void check_parent(ParentCheck *pc)
{
    // Area coverage within a small tolerance (floating point).
    float ratio = pc->expect_area ? pc->total_area / pc->expect_area : 0.0f;
    CHECK(pc->ok, "child rects stay inside parent rect");
    CHECK(ratio > 0.999f && ratio < 1.001f,
          "child rects exactly cover parent rect area");
}

static void test_layout(void)
{
    printf("[layout]\n");
    Tree t;
    tree_init(&t);
    build_known(&t);
    tree_compute_sizes(&t);
    tree_classify(&t);

    // Sanity: directory sizes accumulated correctly.
    CHECK(t.root->children[0]->size == 470, "Games size = 470");
    CHECK(t.root->children[1]->size == 7000, "Saves size = 7000");
    CHECK(t.root->children[2]->size == 4000, "Photos size = 4000");
    CHECK(t.root->size == 811480ULL, "root size = sum of all files");
    CHECK(t.root->nfiles == 11, "root counts 11 files");
    CHECK(t.root->children[0]->nfiles == 4, "Games counts 4 files");

    tree_sort(&t);
    int sorted = 1;
    for (int i = 0; i < t.nnodes; i++) {
        DirEntry *e = t.nodes[i];
        for (int j = 1; j < e->nchildren; j++)
            if (e->children[j - 1]->size < e->children[j]->size) sorted = 0;
    }
    CHECK(sorted, "children sorted by size, largest first");
    CHECK(strcmp(t.root->children[0]->name, "home.3dsx") == 0, "largest entry first");

    layout_tree(t.root, 0, 0, 800, 400, 4, 400.0f);

    // The virtual root's own rectangle is the viewport (layout_tree only sets
    // children rects). Set it so containment/coverage checks use the right area.
    t.root->rx = 0; t.root->ry = 0; t.root->rw = 800; t.root->rh = 400;

    g_nchecks = 0;
    for_each_drawn(t.root, 4, 400.0f, record, NULL);
    for (int i = 0; i < g_nchecks; i++)
        check_parent(&g_checks[i]);

    tree_free(&t);
}

static void test_sizes(void)
{
    printf("[sizes]\n");
    char buf[64];
    format_size(0, buf, sizeof buf);
    CHECK(strcmp(buf, "0 B") == 0, "0 -> 0 B");
    format_size(1023, buf, sizeof buf);
    CHECK(strcmp(buf, "1023 B") == 0, "1023 -> 1023 B");
    format_size(1536, buf, sizeof buf);
    CHECK(strcmp(buf, "1.50 KB") == 0, "1536 -> 1.50 KB");
    format_size(1610612736ULL, buf, sizeof buf);
    CHECK(strcmp(buf, "1.50 GB") == 0, "1.5GB formatted");
}

static void test_colors(void)
{
    printf("[colors]\n");
    CHECK(category_of_ext("3ds") == CAT_3DS, ".3ds -> CAT_3DS");
    CHECK(category_of_ext("CIA") == CAT_CIA, ".CIA -> CAT_CIA");
    CHECK(category_of_ext("jpg") == CAT_IMG, ".jpg -> CAT_IMG");
    CHECK(category_of_ext("mp3") == CAT_AUD, ".mp3 -> CAT_AUD");
    CHECK(category_of_ext("zip") == CAT_ARCH, ".zip -> CAT_ARCH");
    CHECK(category_of_ext("Makefile") == CAT_OTHER, "Makefile -> CAT_OTHER");
    CHECK(category_of_ext(".hidden") == CAT_OTHER, ".hidden -> CAT_OTHER");
    CHECK(category_of_ext("") == CAT_OTHER, "empty -> CAT_OTHER");
    // Colors must be distinct per category.
    uint32_t seen[CAT_COUNT];
    memset(seen, 0xFF, sizeof seen);
    for (int i = 0; i < CAT_COUNT; i++) {
        uint32_t c = category_color(i);
        for (int j = 0; j < i; j++)
            CHECK(seen[j] != c, "colors distinct");
        seen[i] = c;
    }
}

int main(void)
{
    test_layout();
    test_sizes();
    test_colors();

    if (g_failures) {
        printf("\n%d check(s) FAILED\n", g_failures);
        return 1;
    }
    printf("\nAll checks passed\n");
    return 0;
}
