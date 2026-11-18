// colors.h - file category -> color mapping (platform independent)
#ifndef COLORS_H
#define COLORS_H

#include <stdint.h>

// Color categories. CAT_DIR is used for the virtual root / empty folders.
enum {
    CAT_NONE   = 0,
    CAT_3DS    = 1,   // .3ds .3dsx .cia contents (.app/.tmd) .elf ...
    CAT_CIA    = 2,   // .cia               (installable packages)
    CAT_NCA    = 3,   // .nca .nro          (3DS binaries / saves)
    CAT_SAVE   = 4,   // .sav .save         (save data)
    CAT_IMG    = 5,   // images
    CAT_AUD    = 6,   // audio
    CAT_VID    = 7,   // video
    CAT_DOC    = 8,   // documents / text / data
    CAT_ARCH   = 9,   // archives
    CAT_BIN    = 10,  // generic binaries / data blobs
    CAT_OTHER  = 11,  // anything else
    CAT_DIR    = 12,
    CAT_COUNT
};

// Category index for a lowercased extension (no dot). Unknown -> CAT_OTHER.
int category_of_ext(const char *ext);

// RGB color (0xRRGGBB) for a category.
uint32_t category_color(int category);

// Human readable category name for the legend.
const char *category_name(int category);

#endif // COLORS_H
