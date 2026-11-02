/* The Bink movies' player (see movie.h). */
#include "fe/movie.h"
#include "audio/dsound.h"
#include "bink.h"
#include "game/ctlcfg.h"
#include "game/input.h"
#include "game/timer.h"
#include "render/d3d7.h"
#include "render/tex.h"
#include "render/twod.h"
#include <stdlib.h>
#include <string.h>

enum { RING_SECONDS = 2 };

static Bink *bink;          /* DAT_00831ad0 */
static bool playing;        /* DAT_00831c68 */
static Tex frame_tex;       /* the BinkBuffer's surface */
static uint8_t *rgba;
static uint32_t start_ms;
static bool shown;
static Dsb *sound;
static uint32_t ring, wpos;   /* the sound buffer's size and the next byte to write */
static int16_t *pcm;

bool Movie_Playing(void) { return playing; }

bool Movie_Open(const char *path, int track)
{
    Movie_Close();
    bink = Bink_Open(path, track);   /* BinkOpen(path, 0x8004000): the track set first */
    if (!bink) return false;
    int w = Bink_Width(bink), h = Bink_Height(bink);
    rgba = calloc((size_t)w * (size_t)h, 4);
    memset(&frame_tex, 0, sizeof frame_tex);
    frame_tex.surface = D3dTex_Create(w, h, rgba);
    frame_tex.width = (int16_t)w, frame_tex.height = (int16_t)h;
    int rate = Bink_AudioRate(bink), ch = Bink_AudioChannels(bink);
    if (rate && ch) {
        ring = (uint32_t)(rate * ch * 2 * RING_SECONDS);
        sound = Dsb_Create((uint32_t)rate, 16, ch, ring);
        pcm = malloc((size_t)ring);
        wpos = 0;
    }
    start_ms = Time_Get();
    shown = false;
    playing = true;
    return true;
}

/* the decoded sound into the buffer behind the play cursor; playing (looped) from the first */
static void stream_sound(void)
{
    if (!sound) return;
    int ch = Bink_AudioChannels(bink), frames = (int)(ring / (uint32_t)(2 * ch));
    int n = Bink_TakeAudio(bink, pcm, frames / 2);
    uint32_t bytes = (uint32_t)n * (uint32_t)(2 * ch);
    const uint8_t *src = (const uint8_t *)pcm;
    while (bytes) {
        uint32_t k = ring - wpos < bytes ? ring - wpos : bytes;
        Dsb_Write(sound, wpos, src, k);
        wpos = (wpos + k) % ring, src += k, bytes -= k;
    }
    if (n && !Dsb_Playing(sound)) Dsb_Play(sound, true);
}

bool Movie_Step(void)
{
    if (!playing || !bink) return false;
    Input_PollAll();   /* 0x49eab0 */
    InputRecord *r = Input_Record(0);
    Ctl_Merge(0, r);   /* 0x40bd60 */
    if (r->pressed & 0x10) return false;
    uint32_t num, den;
    Bink_Rate(bink, &num, &den);
    if (!num) num = 1;
    /* BinkWait: frame k (from 1) is due (k - 1) frame times after the start */
    uint32_t now = Time_Get() - start_ms;
    if (Bink_Frame(bink) <= Bink_Frames(bink) && (uint64_t)(Bink_Frame(bink) - 1) * den * 1000 <= (uint64_t)now * num) {
        Bink_DoFrame(bink);   /* BinkDoFrame */
        stream_sound();
        Bink_CopyRGBA(bink, rgba, Bink_Width(bink) * 4);   /* BinkCopyToBuffer */
        frame_tex.surface->dirty = true;
        shown = true;
        Bink_NextFrame(bink);   /* one frame a call, as the original blits each */
    }
    return Bink_Frame(bink) <= Bink_Frames(bink);
}

void Movie_Draw(void)
{
    if (!bink || !shown) return;
    int w, h;
    D3d_ScreenSize(&w, &h);
    int16_t sw = 0x280, sh = 0x1e0;
    if (w >= 0x640) sw = 0x500, sh = 0x3c0;
    else if (w >= 0x400) sw = 0x400, sh = 0x300;
    Rect16 src = {0, 0, frame_tex.width, frame_tex.height};
    Rect16 dst = {(int16_t)((w - sw) / 2), (int16_t)((h - sh) / 2), sw, sh};   /* (screen - size) x 0.5 (0x5113b8) */
    static const uint8_t white[4] = {0xff, 0xff, 0xff, 0xff};
    Sprite_Add(&src, &dst, &frame_tex, 1, 0, NULL, NULL, white, 8);
}

void Movie_Close(void)
{
    playing = false;
    if (sound) Dsb_Stop(sound), Dsb_Release(sound);
    sound = NULL;
    free(pcm);
    pcm = NULL;
    if (frame_tex.surface) D3dTex_Free(frame_tex.surface);   /* its pixels with it */
    else free(rgba);
    frame_tex.surface = NULL;
    rgba = NULL;
    Bink_Close(bink);   /* BinkClose */
    bink = NULL;
}
