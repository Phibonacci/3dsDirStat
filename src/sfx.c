// sfx.c - synthesized UI sounds
//
// Every sound is a small list of "voices": soft bell-like tones (a sine plus a
// quieter inharmonic partial, fast attack, exponential decay, optional pitch
// glide), low-passed noise bursts for swooshes and "knocks" (a few fast-decaying
// inharmonic modes plus a tiny click: a small wooden block landing). Mixed,
// then gently limited.
#include "sfx.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265f

typedef enum { V_TONE, V_NOISE, V_KNOCK } VoiceType;

typedef struct {
    VoiceType type;
    float start;      // seconds
    float dur;        // seconds
    float f0, f1;     // tone: start/end frequency (Hz); noise: cutoff start/end
    float amp;
    float decay;      // exponential decay time constant (seconds)
    float partial;    // tone: level of the bell partial (2.76 x f)
    float pan;        // -1 left .. +1 right (stereo sounds only)
} Voice;

typedef struct {
    const char *name;
    float       len;     // seconds
    int         n;
    Voice       v[10];
    int         rate;    // 0 = SFX_RATE
    int         stereo;  // 1 = interleaved stereo output
} SoundDef;

// Note frequencies.
#define C6 1046.5f
#define E6 1318.5f
#define G6 1568.0f
#define A6 1760.0f
#define C7 2093.0f

static const SoundDef kSounds[SFX_ALL] = {
    [SFX_MOVE] = { "move", 0.05f, 1, {
        { V_TONE, 0.000f, 0.05f, C7, C7, 0.30f, 0.010f, 0.10f } } },
    [SFX_BUMP] = { "bump", 0.09f, 2, {
        { V_TONE,  0.000f, 0.08f, 240, 180, 0.45f, 0.025f, 0.05f },
        { V_NOISE, 0.000f, 0.03f, 900, 400, 0.10f, 0.010f, 0 } } },
    [SFX_OPEN] = { "open", 0.26f, 2, {
        { V_TONE, 0.000f, 0.16f, C6, C6, 0.32f, 0.050f, 0.18f },
        { V_TONE, 0.050f, 0.20f, G6, G6, 0.30f, 0.065f, 0.18f } } },
    [SFX_BACK] = { "back", 0.26f, 2, {
        { V_TONE, 0.000f, 0.16f, G6, G6, 0.30f, 0.050f, 0.18f },
        { V_TONE, 0.050f, 0.20f, C6, C6, 0.32f, 0.065f, 0.18f } } },
    [SFX_PAGE] = { "page", 0.16f, 2, {
        { V_NOISE, 0.000f, 0.12f, 1500, 6000, 0.22f, 0.050f, 0 },
        { V_TONE,  0.030f, 0.12f, E6, E6, 0.18f, 0.040f, 0.15f } } },
    [SFX_DIALOG] = { "dialog", 0.24f, 2, {
        { V_TONE, 0.000f, 0.14f, E6, E6, 0.28f, 0.045f, 0.18f },
        { V_TONE, 0.035f, 0.20f, A6, A6, 0.26f, 0.060f, 0.18f } } },
    [SFX_CANCEL] = { "cancel", 0.12f, 1, {
        { V_TONE, 0.000f, 0.12f, 880, 660, 0.32f, 0.035f, 0.12f } } },
    [SFX_WARN] = { "warn", 0.34f, 4, {
        { V_TONE, 0.000f, 0.14f, 392, 392, 0.36f, 0.060f, 0.0f },
        { V_TONE, 0.000f, 0.14f, 1176, 1176, 0.06f, 0.040f, 0.0f },
        { V_TONE, 0.130f, 0.20f, 330, 330, 0.36f, 0.080f, 0.0f },
        { V_TONE, 0.130f, 0.20f, 990, 990, 0.06f, 0.050f, 0.0f } } },
    [SFX_DELETE] = { "delete", 0.30f, 2, {
        { V_NOISE, 0.000f, 0.26f, 7000, 250, 0.28f, 0.090f, 0 },
        { V_TONE,  0.000f, 0.16f, 196, 140, 0.35f, 0.050f, 0.0f } } },
    [SFX_DONE] = { "done", 0.50f, 4, {
        { V_TONE, 0.000f, 0.25f, C6, C6, 0.24f, 0.080f, 0.18f },
        { V_TONE, 0.070f, 0.25f, E6, E6, 0.24f, 0.080f, 0.18f },
        { V_TONE, 0.140f, 0.25f, G6, G6, 0.24f, 0.080f, 0.18f },
        { V_TONE, 0.210f, 0.29f, C7, C7, 0.26f, 0.100f, 0.18f } } },
    // HOME Menu jingle: soft wooden blocks stacking up, each one a little
    // higher and lighter, panned left / right, then a faint chime. Stereo at
    // the 3DS's native 32728 Hz, like system banner sounds.
    [SFX_JINGLE] = { .name = "jingle", .len = 1.90f, .n = 8, .rate = 32728, .stereo = 1, .v = {
        { V_KNOCK, 0.000f, 0.30f, 392, 392, 0.55f, 0.070f, 0, -0.35f },
        { V_KNOCK, 0.240f, 0.30f, 440, 440, 0.48f, 0.062f, 0,  0.35f },
        { V_KNOCK, 0.430f, 0.28f, 523, 523, 0.42f, 0.056f, 0, -0.20f },
        { V_KNOCK, 0.585f, 0.26f, 587, 587, 0.38f, 0.050f, 0,  0.20f },
        { V_KNOCK, 0.640f, 0.10f, 587, 587, 0.08f, 0.020f, 0,  0.20f },  // settle
        { V_TONE,  0.780f, 1.10f, E6, E6, 0.09f, 0.340f, 0.20f, -0.15f },
        { V_TONE,  0.830f, 1.05f, A6, A6, 0.07f, 0.320f, 0.20f,  0.15f },
        { V_TONE,  0.880f, 1.00f, C7, C7, 0.05f, 0.300f, 0.20f,  0.00f } } },
};

const char *sfx_name(Sfx s) { return kSounds[s].name; }

int sfx_rate(Sfx s) { return kSounds[s].rate ? kSounds[s].rate : SFX_RATE; }

int sfx_channels(Sfx s) { return kSounds[s].stereo ? 2 : 1; }

int sfx_length(Sfx s) { return (int)(kSounds[s].len * sfx_rate(s)); }

// Small deterministic noise generator (same sound every time).
static uint32_t g_rng = 0x12345678u;
static float noise(void)
{
    g_rng = g_rng * 1664525u + 1013904223u;
    return (float)(int32_t)g_rng / 2147483648.0f;
}

// Mix one voice into `mix` (`ch` interleaved channels, `n` frames).
static void add_voice(const Voice *v, float *mix, int n, int ch, int rate)
{
    // Inharmonic modes of a small wooden block: ratio, level, decay factor.
    static const float kModes[4][3] = {
        { 1.00f, 1.00f, 1.00f }, { 2.32f, 0.45f, 0.55f },
        { 4.25f, 0.22f, 0.35f }, { 6.63f, 0.10f, 0.25f },
    };
    // Equal-power pan.
    float gl = 1.0f, gr = 1.0f;
    if (ch == 2) {
        float a = (v->pan + 1.0f) * 0.25f * PI;
        gl = cosf(a) * 1.41421356f;
        gr = sinf(a) * 1.41421356f;
    }
    int s0 = (int)(v->start * rate);
    int len = (int)(v->dur * rate);
    float phase = 0.0f, phase2 = 0.0f, lp = 0.0f;
    float mph[4] = { 0 };
    for (int i = 0; i < len && s0 + i < n; i++) {
        float t = (float)i / rate;
        float k = (float)i / len;
        // 2 ms attack and 5 ms fade out at the very end (no clicks), times an
        // exponential decay (knocks decay per mode instead).
        float edge = 1.0f;
        if (t < 0.002f) edge = t / 0.002f;
        float tail = (float)(len - i) / (0.005f * rate);
        if (tail < 1.0f) edge *= tail;
        float env = edge * expf(-t / v->decay);

        float f = v->f0 + (v->f1 - v->f0) * k;
        float x;
        if (v->type == V_TONE) {
            phase  += 2.0f * PI * f / rate;
            phase2 += 2.0f * PI * f * 2.76f / rate;
            if (phase  > 2.0f * PI) phase  -= 2.0f * PI;
            if (phase2 > 2.0f * PI) phase2 -= 2.0f * PI;
            // The bell partial fades faster than the fundamental.
            x = sinf(phase) + v->partial * sinf(phase2) * expf(-t / (v->decay * 0.4f));
        } else if (v->type == V_KNOCK) {
            // Slight pitch drop right after impact gives the "thock".
            float bend = 1.0f + 0.04f * expf(-t / 0.008f);
            x = 0.0f;
            for (int m = 0; m < 4; m++) {
                mph[m] += 2.0f * PI * f * kModes[m][0] * bend / rate;
                if (mph[m] > 2.0f * PI) mph[m] -= 2.0f * PI;
                // Higher modes die faster.
                x += kModes[m][1] * sinf(mph[m]) * expf(-t / (v->decay * kModes[m][2]));
            }
            // Contact click: a few ms of low-passed noise.
            float a = 1.0f - expf(-2.0f * PI * 2500.0f / rate);
            lp += a * (noise() - lp);
            x = x * 0.6f + lp * 2.0f * expf(-t / 0.0015f);
            env = edge;                // decay is already in the modes
        } else {
            // One-pole low-pass with a gliding cutoff: a soft "swoosh".
            float a = 1.0f - expf(-2.0f * PI * f / rate);
            lp += a * (noise() - lp);
            x = lp * 1.8f;
        }
        float y = v->amp * env * x;
        if (ch == 2) {
            mix[(s0 + i) * 2]     += y * gl;
            mix[(s0 + i) * 2 + 1] += y * gr;
        } else {
            mix[s0 + i] += y;
        }
    }
}

bool sfx_render(Sfx s, int16_t *out)
{
    const SoundDef *d = &kSounds[s];
    int n = sfx_length(s), ch = sfx_channels(s);
    float *mix = calloc((size_t)n * ch, sizeof(float));
    if (!mix) return false;
    g_rng = 0x12345678u;
    for (int i = 0; i < d->n; i++)
        add_voice(&d->v[i], mix, n, ch, sfx_rate(s));
    for (int i = 0; i < n * ch; i++) {
        float x = mix[i];
        x = x / (1.0f + fabsf(x) * 0.3f);  // gentle limiter, no hard clipping
        int v = (int)(x * 32767.0f);
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        out[i] = (int16_t)v;
    }
    free(mix);
    return true;
}
