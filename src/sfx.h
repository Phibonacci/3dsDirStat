// sfx.h - synthesized UI sounds (platform independent)
//
// Short, soft interface sounds generated at start-up (no audio files): a light
// tick for cursor moves, rising / falling chimes for entering / leaving a
// folder, etc. Kept free of libctru so the sounds can be rendered on a PC.
#ifndef SFX_H
#define SFX_H

#include <stdbool.h>
#include <stdint.h>

#define SFX_RATE 32000   // samples per second, mono 16-bit

typedef enum {
    SFX_MOVE,     // cursor moved to another item
    SFX_BUMP,     // nothing to do (end of list, can't open, ...)
    SFX_OPEN,     // entered a folder
    SFX_BACK,     // went back to the parent folder
    SFX_PAGE,     // switched between folder list and file types
    SFX_DIALOG,   // a question dialog opened
    SFX_CANCEL,   // dialog cancelled / closed
    SFX_WARN,     // warning / error dialog
    SFX_DELETE,   // something was deleted
    SFX_DONE,     // scan finished
    SFX_COUNT,    // sounds used by the app
    // Not loaded by the app: the HOME Menu banner jingle (assets/jingle.wav).
    SFX_JINGLE = SFX_COUNT,
    SFX_ALL
} Sfx;

// Number of sample frames of sound `s`, its sample rate and channel count.
// The app's sounds are mono at SFX_RATE; only the banner jingle differs.
int  sfx_length(Sfx s);
int  sfx_rate(Sfx s);
int  sfx_channels(Sfx s);

// Render sound `s` into `out` (sfx_length(s) frames, channels interleaved).
// Returns false if out of memory (the mix buffer is allocated while rendering).
bool sfx_render(Sfx s, int16_t *out);

// Short name, for debugging / exporting.
const char *sfx_name(Sfx s);

#endif // SFX_H
