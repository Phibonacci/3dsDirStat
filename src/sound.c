// sound.c - playback of the UI sounds through the DSP (libctru ndsp)
#include "sound.h"

#include <3ds.h>

#include <string.h>

#define NCH 4                 // DSP channels used, so sounds can overlap

static bool        g_dsp;         // ndspInit succeeded
static bool        g_ok;          // ready to play
static int16_t    *g_pcm[SFX_COUNT];
static int         g_len[SFX_COUNT];
static ndspWaveBuf g_wb[NCH];
static int         g_next;

bool sound_init(void)
{
    if (R_FAILED(ndspInit())) return false;
    g_dsp = true;
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);

    for (int s = 0; s < SFX_COUNT; s++) {
        g_len[s] = sfx_length((Sfx)s);
        g_pcm[s] = linearAlloc((size_t)g_len[s] * sizeof(int16_t));  // DSP-visible
        if (!g_pcm[s]) { sound_fini(); return false; }
        if (!sfx_render((Sfx)s, g_pcm[s])) { sound_fini(); return false; }
        DSP_FlushDataCache(g_pcm[s], (u32)g_len[s] * sizeof(int16_t));
    }
    for (int ch = 0; ch < NCH; ch++) {
        ndspChnReset(ch);
        ndspChnSetInterp(ch, NDSP_INTERP_LINEAR);
        ndspChnSetRate(ch, SFX_RATE);
        ndspChnSetFormat(ch, NDSP_FORMAT_MONO_PCM16);
        float mix[12] = { 0 };
        mix[0] = mix[1] = 0.8f;            // front left / right
        ndspChnSetMix(ch, mix);
    }
    g_ok = true;
    return true;
}

void sound_fini(void)
{
    if (g_ok) {
        for (int ch = 0; ch < NCH; ch++) ndspChnWaveBufClear(ch);
    }
    for (int s = 0; s < SFX_COUNT; s++) {
        if (g_pcm[s]) linearFree(g_pcm[s]);
        g_pcm[s] = NULL;
    }
    if (g_dsp) ndspExit();
    g_dsp = false;
    g_ok  = false;
}

void sound_play(Sfx s)
{
    if (!g_ok || (unsigned)s >= SFX_COUNT) return;
    // Prefer an idle channel; otherwise take the oldest one round-robin.
    int ch = -1;
    for (int i = 0; i < NCH; i++) {
        int c = (g_next + i) % NCH;
        if (!ndspChnIsPlaying(c)) { ch = c; break; }
    }
    if (ch < 0) ch = g_next;
    g_next = (ch + 1) % NCH;

    ndspChnWaveBufClear(ch);
    memset(&g_wb[ch], 0, sizeof g_wb[ch]);
    g_wb[ch].data_vaddr = g_pcm[s];
    g_wb[ch].nsamples   = (u32)g_len[s];
    ndspChnWaveBufAdd(ch, &g_wb[ch]);
}
