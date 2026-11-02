/* The Bink decoder (src/bink.c) on FrontEnd\Videos\cm.bik: every frame decodes, the video's planes (Y,
   U, V of each frame, as yuv420p) hash to OPENCMR2_BINK_VIDEO when set (tools/bink-ref.sh prints the
   reference decoder's), and the audio gives the movie's length in samples. Skips (exit 0) without the
   data or binkw32.dll. */
#include "bink.h"
#include "exe.h"
#include "vfs.h"
#include "vfs_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static uint32_t crc_more(uint32_t crc, const uint8_t *p, size_t n)
{
    static uint32_t table[256];
    if (!table[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
    uint32_t c = crc ^ 0xffffffffu;
    while (n--) c = table[(c ^ *p++) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffffu;
}

int main(void)
{
    char err[200];
    if (!vfs_mount_default() || !vfs_exists("FrontEnd/Videos/cm.bik") || !Bink_LoadTables(err, sizeof err)) {
        puts("bink_test: no data, skipped");
        return 0;
    }
    CHECK(Bink_Open("FrontEnd/Videos/nothere.bik", 0) == NULL);
    Bink *b = Bink_Open("FrontEnd/Videos/cm.bik", 0);
    CHECK(b != NULL);
    if (!b) return 1;
    CHECK(Bink_Width(b) == 640 && Bink_Height(b) == 480 && Bink_Frames(b) == 549);
    uint32_t num, den;
    Bink_Rate(b, &num, &den);
    CHECK(num == 25 && den == 1);
    CHECK(Bink_AudioRate(b) == 22050 && Bink_AudioChannels(b) == 2);
    uint32_t crc = 0;
    long samples = 0;
    int bad = 0;
    static int16_t pcm[1 << 16];
    while (Bink_Frame(b) <= Bink_Frames(b)) {
        if (!Bink_DoFrame(b)) bad++;
        for (int p = 0; p < 3; p++) {
            int st;
            const uint8_t *q = Bink_Plane(b, p, &st);
            crc = crc_more(crc, q, (size_t)st * (size_t)(p ? 240 : 480));
        }
        samples += Bink_TakeAudio(b, pcm, 1 << 15);
        Bink_NextFrame(b);
    }
    Bink_Close(b);
    CHECK(bad == 0);
    CHECK(samples == 484800);
    printf("bink_test: cm.bik video crc %08x, %ld audio frames\n", crc, samples);
    const char *want = getenv("OPENCMR2_BINK_VIDEO");
    if (want) CHECK(strtoul(want, NULL, 16) == crc);
    if (fails) return 1;
    puts("bink_test: ok");
    return 0;
}
