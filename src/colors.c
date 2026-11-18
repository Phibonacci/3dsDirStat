// colors.c - file category -> color mapping
#include "colors.h"

#include <string.h>
#include <ctype.h>

int category_of_ext(const char *ext)
{
    // Normalize to lowercase so matching is case-insensitive regardless of
    // whether the caller already lowercased the extension.
    char lower[64];
    if (!ext || !*ext) return CAT_OTHER;
    size_t i = 0;
    for (; ext[i] && i < sizeof(lower) - 1; i++)
        lower[i] = (char)tolower((unsigned char)ext[i]);
    lower[i] = '\0';

    #define MATCH(s) (strcmp(lower, (s)) == 0)
    if (MATCH("3ds") || MATCH("3dsx") || MATCH("elf") || MATCH("rom") ||
        MATCH("app") || MATCH("tmd") || MATCH("cxi") || MATCH("cci") ||
        MATCH("firm"))        return CAT_3DS;
    if (MATCH("cia"))                                                          return CAT_CIA;
    if (MATCH("nca") || MATCH("nro"))                                         return CAT_NCA;
    if (MATCH("sav") || MATCH("save"))                                         return CAT_SAVE;

    if (MATCH("png") || MATCH("jpg") || MATCH("jpeg") || MATCH("bmp") ||
        MATCH("gif") || MATCH("webp") || MATCH("tiff") || MATCH("bmp"))       return CAT_IMG;

    if (MATCH("mp3") || MATCH("wav") || MATCH("ogg") || MATCH("flac") ||
        MATCH("aac") || MATCH("opus") || MATCH("wma"))                        return CAT_AUD;

    if (MATCH("mp4") || MATCH("avi") || MATCH("mkv") || MATCH("mov") ||
        MATCH("wmv") || MATCH("m4v"))                                          return CAT_VID;

    if (MATCH("pdf") || MATCH("txt") || MATCH("md") || MATCH("rst") ||
        MATCH("doc") || MATCH("docx") || MATCH("xls") || MATCH("xlsx") ||
        MATCH("ppt") || MATCH("pptx") || MATCH("json") || MATCH("xml") ||
        MATCH("csv") || MATCH("log") || MATCH("ini") || MATCH("cfg") ||
        MATCH("yml") || MATCH("yaml") || MATCH("html") || MATCH("htm"))       return CAT_DOC;

    if (MATCH("zip") || MATCH("rar") || MATCH("7z") || MATCH("tar") ||
        MATCH("gz") || MATCH("bz2") || MATCH("xz") || MATCH("lz") ||
        MATCH("iso") || MATCH("wbfs"))                                         return CAT_ARCH;

    if (MATCH("dll") || MATCH("exe") || MATCH("so") || MATCH("bin") ||
        MATCH("dat") || MATCH("img") || MATCH("nsp") || MATCH("nsz"))         return CAT_BIN;

    #undef MATCH
    return CAT_OTHER;
}

typedef struct { int cat; uint32_t color; } ColorEntry;

static const ColorEntry kColors[CAT_COUNT] = {
    { CAT_NONE,   0x606060 },
    { CAT_3DS,    0x3B82F6 }, // blue
    { CAT_CIA,    0x8B5CF6 }, // violet
    { CAT_NCA,    0x0E7490 }, // dark cyan
    { CAT_SAVE,   0x14B8A6 }, // teal
    { CAT_IMG,    0x22C55E }, // green
    { CAT_AUD,    0xEC4899 }, // pink
    { CAT_VID,    0xEF4444 }, // red
    { CAT_DOC,    0xFDE68A }, // pale yellow
    { CAT_ARCH,   0xF97316 }, // orange
    { CAT_BIN,    0xA8A29E }, // light stone
    { CAT_OTHER,  0x64748B }, // slate
    { CAT_DIR,    0x334155 }, // slate-700
};

uint32_t category_color(int category)
{
    if (category < 0 || category >= CAT_COUNT) return kColors[CAT_OTHER].color;
    return kColors[category].color;
}

const char *category_name(int category)
{
    switch (category) {
        case CAT_3DS:   return "3DS titles & apps";
        case CAT_CIA:   return "Packages (.cia)";
        case CAT_NCA:   return "Switch (.nca/.nro)";
        case CAT_SAVE:  return "Save data";
        case CAT_IMG:   return "Images";
        case CAT_AUD:   return "Audio";
        case CAT_VID:   return "Video";
        case CAT_DOC:   return "Documents";
        case CAT_ARCH:  return "Archives";
        case CAT_BIN:   return "Binaries";
        case CAT_OTHER: return "Other";
        case CAT_DIR:   return "Directory";
        default:        return "Unknown";
    }
}
