/* The fades (see fade.h). */
#include "sim/fade.h"
#include "fixmath.h"
#include "game/mem.h"

static struct {
    int32_t dur;      /* 0x53a0ec */
    int32_t t, prev;  /* 0x53a04c, 0x53a02c */
    FadeFn fn;        /* 0x53a08c */
    int mode;         /* 0x53a0ac */
    int32_t level;    /* 0x53a06c */
    int32_t a, b, c;  /* 0x53a20c, 0x53a0cc, 0x53a00c */
} F[8];

bool Fade_Active(int ch) { return F[ch & 7].mode != 0; }
bool Fade_Rising(int ch) { return Fade_Active(ch) && 0 < F[ch & 7].t; }
int32_t Fade_LevelOf(int ch) { return F[ch & 7].level; }
int32_t Fade_Arg(int ch) { return F[ch & 7].a; }

void Fade_Start(int ch, int32_t dur, FadeFn fn, int mode, int32_t a, int32_t b, int32_t c, bool force)
{
    ch &= 7;
    if (Fade_Active(ch)) {
        if (!force) return;
        if (F[ch].fn) F[ch].fn(ch);
    }
    F[ch].dur = dur;
    F[ch].c = c;
    F[ch].t = mode == 2 ? -dur : 0;
    F[ch].fn = fn;
    F[ch].mode = mode;
    if ((mode == 0 || mode == 2) && fn) fn(ch);
    if (mode == 0 || mode == 4) F[ch].level = 0;
    F[ch].a = a;
    F[ch].b = b;
}

void Fade_OutIn(int ch, FadeFn fn, int32_t a, int32_t b, int32_t c, bool force)
{
    Fade_Start(ch, 0xc8000, fn, 3, a, b, c, force);
}

void Fade_Tick(int ch, bool paused)
{
    ch &= 7;
    if (!Fade_Active(ch) || (F[ch].b && paused)) return;
    if (F[ch].mode == 4) {
        F[ch].mode = 0;
        return;
    }
    F[ch].prev = F[ch].t;
    F[ch].t += gms32(0x519c8c);
    if (F[ch].prev < 0 && F[ch].t >= 0) {
        F[ch].mode = 4;
        F[ch].level = 0;
        return;
    }
    if (F[ch].dur <= F[ch].t) {
        if (F[ch].mode == 3) F[ch].t = F[ch].prev = -F[ch].dur;
        else F[ch].mode = 0, F[ch].level = 0x10000;
        if (F[ch].fn) F[ch].fn(ch);
    }
}

void Fade_Level(int ch, int32_t frac, bool paused)
{
    ch &= 7;
    if (!Fade_Active(ch) || (F[ch].b && paused)) return;
    int32_t v = fx_mul(0x10000 - frac, F[ch].prev) + fx_mul(frac, F[ch].t);
    if (v < 0) v = -v;
    F[ch].level = (int32_t)(((int64_t)v << 16) / F[ch].dur);
}
