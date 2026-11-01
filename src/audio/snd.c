/* The original's sound layer (see snd.h). */
#include "audio/snd.h"
#include "audio/dsound.h"
#include "pending.h"
#include <math.h>
#include <string.h>

static Dsb *sounds[200];      /* 0x5a23ec */
static int nsounds;           /* DAT_006e0ef0 */
static bool on;               /* DAT_006e0eec */
static int32_t master = 0x10000;   /* DAT_005210f4 */

typedef struct {
    int16_t slot;
    int32_t handle;
    uint16_t id, freq;
    int32_t vol;
    bool loop, three_d;
    uint32_t start;
    Dsb *buf;                 /* +0x1c */
    bool dup;                 /* +0x2c: a duplicate of the sound's buffer, freed with the voice */
} Voice;
static Voice *voices[32];     /* 0x6e0d6c */
static Voice pool[32];

void Snd_Init(void) { on = true; }
int Snd_Count(void) { return nsounds; }

static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t u32(const uint8_t *p) { return (uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* 0x4a1f50: the WAV's 44-byte header read at fixed offsets ("WAVE" at 8, channels 0x16, rate 0x18,
   bits 0x22, data size 0x28, data at 0x2c) into a buffer for sound nsounds */
static bool snd_load(const char *name, const BflSlot *arc)
{
    uint8_t found = 0;
    uint32_t size = 0;
    const uint8_t *w = Bfl_FindFile(arc, name, &found, &size, false);
    if (!w) return false;
    if (size >= 0x2c && !memcmp(w + 8, "WAVE", 4)) {
        uint32_t n = u32(w + 0x28);
        if (n > size - 0x2c) n = size - 0x2c;
        Dsb *b = Dsb_Create(u32(w + 0x18), u16(w + 0x22), u16(w + 0x16), n);
        if (!b) return false;
        Dsb_Write(b, 0, w + 0x2c, n);
        sounds[nsounds] = b;
    }
    return true;
}

bool Snd_LoadNamed(const char *name, const BflSlot *arc)
{
    if (!on || nsounds >= 200) return false;
    if (!snd_load(name, arc)) return false;   /* (the original's "Failed to load" message) */
    nsounds++;
    return true;
}

void Snd_Truncate(int from)
{
    if (!on) return;
    Voices_StopAll();
    for (int i = from; i < nsounds; i++) Dsb_Release(sounds[i]), sounds[i] = NULL;   /* 0x4a1d10 */
    if (from < nsounds) nsounds = from;
}

static int find(int32_t h)   /* 0x4b7ab0 */
{
    if (h < 0) return -1;
    int s = h & 0x3f;
    return s < 32 && voices[s] && voices[s]->handle == h ? s : -1;
}

/* 0x4a25f0: the volume (times the master, over 65536) as DirectSound's attenuation */
static void apply_volume(Voice *v)
{
    int64_t a = llrint((double)((float)master * (float)v->vol * (1.0f / 65536.0f)));
    int32_t l = a > 0 ? (int32_t)llrint(log((double)(int32_t)a) * -10.0) : 0;
    int32_t d = (int32_t)llrint(log(65536.0) * 10.0);
    Dsb_SetVolume(v->buf, -10000 - l * 10000 / d);
}

static void apply_freq(Voice *v)   /* 0x4a2690: 100 to 100,000 (as the u16 holds it) */
{
    if (v->freq < 100) v->freq = 100;
    Dsb_SetFrequency(v->buf, v->freq);
}

/* 0x4a22c0: the sound's buffer, duplicated when another voice plays the same sound */
static bool start_voice(Voice *v)
{
    Dsb *s = sounds[v->id];
    if (!s) return false;
    bool shared = false;
    for (int i = 0; i < 32; i++)
        if (voices[i] && voices[i] != v && voices[i]->id == v->id) shared = true;
    if (shared) v->buf = Dsb_Duplicate(s), v->dup = true;
    else v->buf = s, v->dup = false;
    if (!v->buf) return false;
    if (v->loop && v->start) PENDING_NOTE(0x4a24a0, "Voice_LoopFrom (a loop from an offset)");
    apply_volume(v);
    apply_freq(v);
    Dsb_SetPosition(v->buf, 0);   /* 0x4a23f0 */
    Dsb_Play(v->buf, v->loop && !v->start);
    return true;
}

int32_t Voice_Play(int id, int32_t vol, uint32_t freq, uint32_t start, bool loop, bool three_d)
{
    int s = -1;
    for (int i = 0; i < 32; i++)
        if (!voices[i]) { s = i; break; }
    if (s < 0 || id < 0 || id >= 200) return -1;
    Voice *v = &pool[s];
    memset(v, 0, sizeof *v);
    v->id = (uint16_t)id;
    v->slot = (int16_t)s;
    v->handle = (int32_t)((uint32_t)(id & 0xffff) << 8 | (uint32_t)s);
    v->loop = loop;
    v->three_d = three_d;
    v->start = start;
    v->vol = vol;
    v->freq = (uint16_t)freq;
    voices[s] = v;
    if (!start_voice(v)) return voices[s] = NULL, -1;
    Voice_SetVolume(v->handle, vol);
    Voice_SetFrequency(v->handle, freq);
    return v->handle;
}

bool Voice_Playing(int32_t h)
{
    int s = find(h);
    return s >= 0 && Dsb_Playing(voices[s]->buf);
}

static void stop(int s)   /* 0x4a26f0 */
{
    Voice *v = voices[s];
    Dsb_Stop(v->buf);
    if (v->dup) Dsb_Release(v->buf);
    voices[s] = NULL;
}

void Voice_Stop(int32_t h)
{
    int s = find(h);
    if (s >= 0) stop(s);
}

void Voices_StopAll(void)
{
    for (int i = 0; i < 32; i++)
        if (voices[i]) stop(i);
}

void Snd_SetMaster(int32_t vol)
{
    if (vol >= 0 && vol <= 0x10000) master = vol;
    for (int i = 0; i < 32; i++)
        if (voices[i]) apply_volume(voices[i]);
}

void Voice_SetVolume(int32_t h, int32_t vol)
{
    int s = find(h);
    if (s < 0 || voices[s]->vol == vol) return;
    voices[s]->vol = vol;
    apply_volume(voices[s]);
}

void Voice_SetFrequency(int32_t h, uint32_t hz)
{
    int s = find(h);
    if (s < 0) return;
    voices[s]->freq = (uint16_t)hz;
    apply_freq(voices[s]);
}

int32_t Voice_Volume(int32_t h)
{
    int s = find(h);
    return s >= 0 ? voices[s]->vol : 0;
}
