// debug.h - scripted input + screenshot harness (only active in SCRIPT builds)
//
// Build with `make SCRIPT=1`. At start-up the app reads sdmc:/logs/script.txt,
// one command per line, keyed on the interactive frame number:
//
//     <frame> key <A|B|X|Y|L|R|UP|DOWN|LEFT|RIGHT|START|SELECT>
//     <frame> tap <x> <y>        touch the bottom screen for one frame
//     <frame> shot <name>        dump both LCDs to sdmc:/logs/<name>_{top,bot}.ppm
//     <frame> quit
//     scanshot <name>            dump the screens while the scan is running
//
// Screenshots read the real LCD framebuffers, so they show exactly what the
// console displays (unlike an offscreen render target).
#ifndef DEBUG_H
#define DEBUG_H

#include <3ds.h>
#include <stdbool.h>

typedef struct {
    u32 down, held, repeat;
    touchPosition touch;
} Input;

#ifdef SCRIPT
void dbg_init(void);
void dbg_input(Input *in);      // merge scripted events for this frame
void dbg_frame_done(void);      // after the frame is rendered: shots, frame++
void dbg_scan_frame_done(void); // after a scan progress frame is rendered
bool dbg_quit(void);
#else
static inline void dbg_init(void) {}
static inline void dbg_input(Input *in) { (void)in; }
static inline void dbg_frame_done(void) {}
static inline void dbg_scan_frame_done(void) {}
static inline bool dbg_quit(void) { return false; }
#endif

#endif // DEBUG_H
