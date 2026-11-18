// sound.h - playback of the UI sounds through the DSP (libctru ndsp)
#ifndef SOUND_H
#define SOUND_H

#include "sfx.h"

#include <stdbool.h>

// Start the DSP and prepare every sound. Returns false (and stays silent) when
// audio is unavailable, e.g. without sdmc:/3ds/dspfirm.cdc on real hardware.
bool sound_init(void);
void sound_fini(void);

// Play a sound (no-op when audio is unavailable).
void sound_play(Sfx s);

#endif // SOUND_H
