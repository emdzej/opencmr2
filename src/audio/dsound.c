/* The port's DirectSound (see dsound.h). */
#include "audio/dsound.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { MAX_BUFFERS = 512 };

typedef struct {
    int16_t *pcm;      /* the data as 16-bit frames (mono or stereo interleaved) */
    uint32_t frames;
    int refs;
} Data;

struct Dsb {
    Data *d;
    int channels, bits;
    uint32_t rate, freq;
    double pos;        /* in frames */
    bool playing, loop, used;
    int32_t vol, pan;
};

static Dsb bufs[MAX_BUFFERS];

static Dsb *alloc_buf(void)
{
    for (int i = 0; i < MAX_BUFFERS; i++)
        if (!bufs[i].used) {
            memset(&bufs[i], 0, sizeof bufs[i]);
            bufs[i].used = true;
            return &bufs[i];
        }
    return NULL;
}

Dsb *Dsb_Create(uint32_t rate, int bits, int channels, uint32_t size)
{
    Dsb *b = alloc_buf();
    if (!b) return NULL;
    b->d = calloc(1, sizeof *b->d);
    if (!b->d) return b->used = false, NULL;
    b->d->refs = 1;
    b->bits = bits == 8 ? 8 : 16;
    b->channels = channels == 2 ? 2 : 1;
    b->d->frames = size / (uint32_t)(b->bits / 8 * b->channels);
    b->d->pcm = calloc((size_t)b->d->frames * (size_t)b->channels + 1, sizeof(int16_t));
    b->rate = b->freq = rate;
    return b;
}

Dsb *Dsb_Duplicate(Dsb *s)
{
    if (!s) return NULL;
    Dsb *b = alloc_buf();
    if (!b) return NULL;
    *b = *s;
    b->used = true;
    b->playing = false;
    b->pos = 0;
    b->d->refs++;
    return b;
}

void Dsb_Release(Dsb *b)
{
    if (!b || !b->used) return;
    if (--b->d->refs == 0) {
        free(b->d->pcm);
        free(b->d);
    }
    b->used = false;
}

void Dsb_Write(Dsb *b, uint32_t at, const void *data, uint32_t n)
{
    if (!b) return;
    const uint8_t *p = data;
    uint32_t bps = (uint32_t)(b->bits / 8), total = b->d->frames * (uint32_t)b->channels;
    uint32_t s0 = at / bps;
    for (uint32_t i = 0; i < n / bps && s0 + i < total; i++) {
        int16_t v = b->bits == 8 ? (int16_t)((p[i] - 128) << 8) : (int16_t)(p[2 * i] | p[2 * i + 1] << 8);
        b->d->pcm[s0 + i] = v;
    }
}

void Dsb_Play(Dsb *b, bool loop) { if (b) b->playing = true, b->loop = loop; }
void Dsb_Stop(Dsb *b) { if (b) b->playing = false; }
void Dsb_SetPosition(Dsb *b, uint32_t at) { if (b) b->pos = (double)(at / (uint32_t)(b->bits / 8 * b->channels)); }
bool Dsb_Playing(const Dsb *b) { return b && b->playing; }
uint32_t Dsb_Position(const Dsb *b) { return b ? (uint32_t)b->pos * (uint32_t)(b->bits / 8 * b->channels) : 0; }
void Dsb_SetVolume(Dsb *b, int32_t v) { if (b) b->vol = v < -10000 ? -10000 : v > 0 ? 0 : v; }
void Dsb_SetPan(Dsb *b, int32_t p) { if (b) b->pan = p < -10000 ? -10000 : p > 10000 ? 10000 : p; }
void Dsb_SetFrequency(Dsb *b, uint32_t hz) { if (b) b->freq = hz < 100 ? 100 : hz > 100000 ? 100000 : hz; }

static float db_gain(int32_t att) { return att <= -10000 ? 0.0f : powf(10.0f, (float)att / 2000.0f); }

void Dsb_Mix(float *out, unsigned frames)
{
    memset(out, 0, (size_t)frames * 2 * sizeof *out);
    for (int i = 0; i < MAX_BUFFERS; i++) {
        Dsb *b = &bufs[i];
        if (!b->used || !b->playing || !b->d->frames) continue;
        float g = db_gain(b->vol);
        float gl = g * (b->pan > 0 ? db_gain(-b->pan) : 1.0f), gr = g * (b->pan < 0 ? db_gain(b->pan) : 1.0f);
        double step = (double)b->freq / 44100.0;
        const int16_t *s = b->d->pcm;
        uint32_t n = b->d->frames;
        for (unsigned f = 0; f < frames; f++) {
            uint32_t k = (uint32_t)b->pos;
            if (k >= n) {
                if (!b->loop) { b->playing = false; b->pos = 0; break; }
                b->pos -= n;
                k = (uint32_t)b->pos;
                if (k >= n) k = 0, b->pos = 0;
            }
            /* linear between the two nearest frames */
            uint32_t k2 = k + 1 < n ? k + 1 : (b->loop ? 0 : k);
            float t = (float)(b->pos - k), l, r;
            if (b->channels == 2) {
                l = (s[2 * k] + (s[2 * k2] - s[2 * k]) * t) * (1.0f / 32768.0f);
                r = (s[2 * k + 1] + (s[2 * k2 + 1] - s[2 * k + 1]) * t) * (1.0f / 32768.0f);
            } else
                l = r = (s[k] + (s[k2] - s[k]) * t) * (1.0f / 32768.0f);
            out[2 * f] += l * gl;
            out[2 * f + 1] += r * gr;
            b->pos += step;
        }
    }
    for (unsigned f = 0; f < frames * 2; f++) out[f] = out[f] > 1.0f ? 1.0f : out[f] < -1.0f ? -1.0f : out[f];
}
