// sfxwav.c - render 3dsDirStat's synthesized sounds to WAV files (host tool)
//
//   cc -Isrc tools/sfxwav.c src/sfx.c -lm -o sfxwav
//   ./sfxwav jingle assets/jingle.wav     one sound
//   ./sfxwav all <dir>                    every sound, for listening
#include "sfx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void put32(FILE *f, uint32_t v) { fputc(v & 0xFF, f); fputc((v >> 8) & 0xFF, f);
                                          fputc((v >> 16) & 0xFF, f); fputc(v >> 24, f); }
static void put16(FILE *f, uint16_t v) { fputc(v & 0xFF, f); fputc(v >> 8, f); }

static int write_wav(Sfx s, const char *path)
{
    int n = sfx_length(s), ch = sfx_channels(s), rate = sfx_rate(s);
    uint32_t bytes = (uint32_t)n * ch * 2;
    int16_t *pcm = malloc(bytes);
    if (!pcm || !sfx_render(s, pcm)) { free(pcm); return -1; }
    FILE *f = fopen(path, "wb");
    if (!f) { free(pcm); return -1; }
    fwrite("RIFF", 1, 4, f); put32(f, 36 + bytes);
    fwrite("WAVEfmt ", 1, 8, f); put32(f, 16);
    put16(f, 1); put16(f, (uint16_t)ch);                  // PCM
    put32(f, (uint32_t)rate); put32(f, (uint32_t)rate * ch * 2);
    put16(f, (uint16_t)(ch * 2)); put16(f, 16);
    fwrite("data", 1, 4, f); put32(f, bytes);
    for (int i = 0; i < n * ch; i++) put16(f, (uint16_t)pcm[i]);   // little-endian
    fclose(f);
    free(pcm);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s <sound|all> <out.wav|dir>\n", argv[0]);
        return 2;
    }
    for (int s = 0; s < SFX_ALL; s++) {
        char path[1024];
        if (strcmp(argv[1], "all") == 0)
            snprintf(path, sizeof path, "%s/%02d_%s.wav", argv[2], s, sfx_name((Sfx)s));
        else if (strcmp(argv[1], sfx_name((Sfx)s)) == 0)
            snprintf(path, sizeof path, "%s", argv[2]);
        else
            continue;
        if (write_wav((Sfx)s, path) != 0) { perror(path); return 1; }
        printf("%s\n", path);
    }
    return 0;
}
