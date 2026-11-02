/* The cars' engine sounds (see engine.h). */
#include "audio/engine.h"
#include "audio/snd.h"
#include "game/camera.h"
#include "game/gstate.h"
#include "game/race.h"
#include "game/effects.h"
#include "game/timer.h"
#include "game/replay.h"
#include "crt.h"
#include "game/mem.h"
#include "fixmath.h"
#include "exe.h"
#include "pending.h"
#include "sim/car.h"
#include <stdio.h>

enum { FS, RS, FM, RM, WHINE, FT, RT, CHATTER, DET1, DET2, DET3, DET4, DETIDLE, NSOUNDS };
static const char *const names[NSOUNDS] = {
    /* the formats at 0x51f48c ... 0x51f394, in the loader's order */
    "fs-%s", "rs-%s", "fm-%s", "rm-%s", "whine", "ft-%s", "rt-%s", "chatter", "det1", "det2", "det3", "det4", "detidle",
};

static int16_t base[4];                    /* 0x58ddb4: each car's first sound */
static BflSlot archives[4];                /* 0x58df48 */
static int32_t v_fs[4], v_rs[4], v_fm[4], v_rm[4], v_whine[4], v_ft[4], v_rt[4], v_det[4];
                                           /* 0x58dde8, 0x58ddac, 0x58ddf0, 0x58dde0, 0x58dd80, 0x58dd90, 0x58ddd0, 0x58ddc0 */
static int32_t thr_s[4], thr[4];           /* 0x58de00, 0x58de08: the throttle share eased */
static int32_t gear_now[4], gear_was[4];   /* 0x58df88, 0x58dd68 */
static int high_gear[4], high_revs[4], chattered[4];   /* 0x58df68, 0x58df70, 0x58df60 */
static int32_t master;                     /* DAT_0058dda8 */
/* the backfires and the idle (0x479360): the last change of the pattern and its length in 1/100 s, a
   bang to stop when it ends, the pause between runs, the lift-off run, the voice's two uses */
static uint32_t bf_at[4], bf_len[4], bf_start[4];   /* 0x58dd60, 0x58df28, 0x58dda0 */
static int bf_end[4], bf_quiet[4], bf_on[4], det_on[4], idle_on[4];
                                           /* 0x58ddf8, 0x58dd98, 0x58df78, 0x58df20, 0x58dd70 */
/* the inside and outside mixes (0x47a710): fs, rs, fm, rm, ft, rt */
static int32_t k_fs, k_rs, k_fm, k_rm, k_ft, k_rt;   /* 0x58df38, 0x51f274, 0x58df3c, 0x51f278, 0x58df40, 0x51f27c */

/* A band of revs (0x51ea60 slow, 0x51eab8 medium, 0x51ec50 fast): steps over [lo, hi], a frequency
   table (u16) and a volume table (percent bytes) after the header in the exe (set by 0x478dc0) */
typedef struct { int n, lo, hi; uint32_t freq, vol; } Band;
static Band band_at(uint32_t a, uint32_t freq, uint32_t vol)
{
    Band b = {(int)exe_u32(a), (int)exe_u32(a + 4), (int)exe_u32(a + 8), freq, vol};
    return b;
}
static bool in_band(int r, const Band *b) { return b->lo <= r && r <= b->hi; }   /* 0x427ab0 */

/* 0x427ad0: the volume between the band's two nearest steps, in percent */
static int band_vol(int r, const Band *b)
{
    int span = b->hi - b->lo, at = r - b->lo, i = b->n * at / span, w = span / b->n;
    const uint8_t *v = exe_data(b->vol, (size_t)b->n + 2);
    if (0 < w) {
        int t = at % w * 100 / w;
        return (100 - t) * v[i] / 100 + v[i + 1] * t / 100;
    }
    return v[i];
}

/* 0x427b70: the frequency the same way */
static uint32_t band_freq(int r, const Band *b)
{
    int span = b->hi - b->lo, at = r - b->lo, i = b->n * at / span, w = span / b->n;
    const uint8_t *f = exe_data(b->freq, 2 * (size_t)b->n + 4);
    uint32_t f0 = (uint32_t)(f[2 * i] | f[2 * i + 1] << 8), f1 = (uint32_t)(f[2 * i + 2] | f[2 * i + 3] << 8);
    if (0 < w) {
        int t = at % w * 100 / w;
        return f1 * (uint32_t)t / 100 + f0 * (uint32_t)(100 - t) / 100;
    }
    return f0;
}

/* 0x427d50: full within 2 units of the camera, silent beyond 100 */
int32_t Engine_DistanceFade(int car, int player)
{
    const Car *c = Car_Get(car);
    int32_t cam[3], v[3];
    Camera_Position(player, cam);
    /* (0x423d70: the followed frame's position, the car's orientation for a player) */
    v[0] = CI32(c, 0x30) - cam[0], v[1] = CI32(c, 0x34) - cam[1], v[2] = CI32(c, 0x38) - cam[2];
    int32_t d = Vec_Length(v);
    if (d < 0x20000) return 0x10000;
    if (0x640000 < d) return 0;
    return 0x10000 - (int32_t)(((int64_t)(d - 0x20000) << 16) / 0x620000);
}

static int32_t pct(int v) { return (v << 16) / 100; }

static int32_t loop_voice(int32_t *h, int car, int which, uint32_t start)
{
    if (!Voice_Playing(*h)) *h = Voice_Play(base[car] + which, 0, 0x5622, start, true, false);
    return *h;
}

static void stop_voice(int32_t *h)
{
    if (Voice_Playing(*h)) Voice_Stop(*h), *h = -1;   /* a handle that ended is kept, as the original */
}

void Engine_Volume(int32_t sfx_percent) { master = fx_mul((sfx_percent << 16) / 100, 0x5555); }   /* 0x427c10 */

void Engine_Reset(void)
{
    Snd_SetMaster(0x10000);
    for (int i = 0; i < 4; i++) {
        thr[i] = thr_s[i] = 0;
        gear_was[i] = -1;
        v_fs[i] = v_rs[i] = v_fm[i] = v_rm[i] = v_whine[i] = v_ft[i] = v_rt[i] = v_det[i] = -1;
        high_gear[i] = high_revs[i] = chattered[i] = 0;
        bf_end[i] = bf_quiet[i] = idle_on[i] = det_on[i] = 0;
    }
}

bool Engine_Load(int players)
{
    bool ok = true;
    for (int n = 0; n < players && n < 4; n++) {
        /* the player's car (0x4086b0), its class (0x40ee90): the engine (0x51f044) and the names'
           suffix (0x51f15c), 20 bytes a class */
        int car = (int)(gm32(0x531350 + 0x30u * (uint32_t)n) & 0x3f);
        uint32_t cls = exe_u32(0x516b40 + 4u * (uint32_t)car);
        const char *engine = exe_str(0x51f044 + cls * 20), *suffix = exe_str(0x51f15c + cls * 20);
        base[n] = (int16_t)Snd_Count();
        char p[96];
        snprintf(p, sizeof p, "Sounds\\car\\%s.bfl", engine);
        Bfl_Unmount(&archives[n]);
        Bfl_Mount(&archives[n], p);
        for (int k = 0; k < NSOUNDS; k++) {
            char name[48], w[80];
            snprintf(name, sizeof name, names[k], suffix);
            snprintf(w, sizeof w, "Sounds\\car\\%s\\%s.wav", engine, name);
            ok &= Snd_LoadNamed(w, &archives[n]);
        }
    }
    return ok;
}

/* 0x47a380: the throttle share eased, up 20 and down 10 a frame at most */
static void ease_throttle(int t, int car)
{
    int was = thr_s[car];
    if (20 < t - was) t = was + 20;
    else if (10 < was - t) t = was - 10;
    if (t < 0) t = 0;
    thr_s[car] = thr[car] = t;
}

static int32_t shift_at[4], shift_end[4], whine_lvl[4];   /* 0x58dd88, 0x58de10, 0x58dd78 */

/* Car_RecordFlag 0x41f3d0: the car's replay (0x537f3c) playing */
static bool replaying(int car)
{
    const Replay *r = Race_Replay(car);
    return r && Replay_Playing(r);
}

static uint32_t norm_len(int32_t v[3])
{
    return Fx_Sqrt((uint32_t)fx_mul(v[0], v[0]) + (uint32_t)fx_mul(v[1], v[1]) + (uint32_t)fx_mul(v[2], v[2]));
}

/* the inline normalising: by 2^32 over the length, zero when it is 0 */
static void normalise(int32_t v[3])
{
    uint32_t len = norm_len(v);
    if (!len) {
        v[0] = v[1] = v[2] = 0;
        return;
    }
    int32_t inv = (int32_t)(0x100000000LL / (int32_t)len);
    for (int k = 0; k < 3; k++) v[k] = fx_mul(v[k], inv);
}

/* Engine_Doppler 0x427e20: a replayed car's frequency f shifted by its closing speed on the camera
   (the first player's view, the second's when two players watch the replay, 0x41f3a0). Only within 100
   units: the camera's step move less the car's velocity (+0x408), at most 50 long, taken along the
   line from the car (+0x2d0) to the camera; its length over 13.24 makes the factor 1 + v (coming
   closer) or 1 - v (going away), f divided by it unless that is half or less (coming closer the
   frequency is then kept, going away divided only above half). Halved and doubled around the
   division, so the result is even. */
static uint32_t Engine_Doppler(int p, int car, uint16_t f)
{
    int v = Gs_SelBits14() > 1 && Race_State(0) == 10 ? 1 : p;
    const Car *c = Car_Get(car);
    int32_t cp[3], d[3], rel[3], u[3];
    Camera_Position(v, cp);
    for (int k = 0; k < 3; k++) d[k] = cp[k] - CI32(c, 0x2d0 + 4u * (uint32_t)k);
    if (Vec_Length(d) >= 0x640000) return f;
    Camera_Velocity(p, rel);
    for (int k = 0; k < 3; k++) rel[k] -= CI32(c, 0x408 + 4u * (uint32_t)k);
    if (Vec_Length(rel) > 0x320000) {
        normalise(rel);
        for (int k = 0; k < 3; k++) rel[k] = fx_mul(rel[k], 0x320000);
    }
    for (int k = 0; k < 3; k++) u[k] = rel[k];
    normalise(d);
    normalise(u);
    int32_t dot = fx_mul(d[2], u[2]) + fx_mul(d[0], u[0]) + fx_mul(d[1], u[1]);
    for (int k = 0; k < 3; k++) rel[k] = fx_mul(rel[k], dot);
    int32_t h = (int32_t)(f >> 1) << 16;
    if (dot > 0) {
        int32_t q = (int32_t)(((int64_t)Vec_Length(rel) << 16) / 0xd3d70) + 0x10000;
        if (q <= 0x8000) return (uint32_t)(h >> 16) << 1;
        return (uint32_t)((int32_t)(((int64_t)h << 16) / q) >> 16) << 1;
    }
    if (dot < 0) {
        int32_t q = 0x10000 - (int32_t)(((int64_t)Vec_Length(rel) << 16) / 0xd3d70);
        if (0x8000 < q) h = (int32_t)(((int64_t)h << 16) / q);
        return (uint32_t)(h >> 16) << 1;
    }
    return f;
}

/* a band frequency for car, shifted while its replay plays */
static uint32_t shifted(int car, uint32_t f) { return replaying(car) ? Engine_Doppler(car, car, (uint16_t)f) & 0xffff : f; }

/* 0x47a3d0: the gearbox whine by the speed (the band 0x51ede8: 193 steps over 0-160). A gear up
   stops the fast loops (ft, rt) and dips the whine for one second of stage time, from a moment
   now or (one time in four, by rand) a second later, by the curve 0x51f310 (23 steps); the whine
   comes up in tenths while the throttle is open, and is off in neutral, at 160 or over, on the
   0x539ed8 cars (none) and while its voice is not playing. (The record's +0x14 and +0x18, never set,
   would keep the volume.) */
static void whine(int car, int player, int speed, int gear, int throttle)
{
    Band b = band_at(0x51ede8, 0x51edfc, 0x51ef80);
    if (gear_was[car] < gear) {
        stop_voice(&v_rt[car]);
        stop_voice(&v_ft[car]);
        shift_end[car] = Race_StageTime(car);
    } else
        shift_end[car] = -1;
    if (gear != gear_was[car] && gear_was[car] < gear)
        shift_at[car] = (crt_rand() % 4 == 0) ? Race_StageTime(car) + 100 : Race_StageTime(car);   /* (C's remainder of a non-negative value, as the mask) */
    int dip = 0;
    if (0 < gear && gear < 4) {
        uint32_t t = (uint32_t)(Race_StageTime(car) - shift_at[car]);
        dip = t < 100 ? (int)exe_u32(0x51f310 + 4 * ((t * 0x17) / 100)) : 0;
    }
    if (speed < (int)exe_u32(0x51edf0) && gear != 0) {
        if (throttle == 100) {
            whine_lvl[car] += 10;
            if (100 < whine_lvl[car]) whine_lvl[car] = 100;
        } else
            whine_lvl[car] = 0;
        if (!gm8(0x539ed8) && in_band(speed, &b)) {   /* 0x427aa0 */
            if (!Voice_Playing(v_whine[car])) return;
            Voice_SetFrequency(v_whine[car], shifted(car, band_freq(speed, &b)));
            int v = band_vol(speed, &b);
            if (0 < gear && gear < 4) v -= dip;
            if (v < 0) v = 10;
            v = whine_lvl[car] * v / 100;
            v = v < 0 ? 0 : v > 100 ? 100 : v;
            Voice_SetVolume(v_whine[car], fx_mul(Engine_DistanceFade(car, player), fx_mul(master, fx_mul((v << 16) / 100, gms32(0x51f280)))));
            return;
        }
    }
    if (Voice_Playing(v_whine[car])) Voice_SetVolume(v_whine[car], 0), Voice_SetFrequency(v_whine[car], 0x2b11);
}

/* 0x4792c0, 0x479310: the voice stopped if it plays the idle, the detonations */
static void stop_idle(int car)
{
    if (!idle_on[car]) return;
    stop_voice(&v_det[car]);
    idle_on[car] = 0;
}

static void stop_det(int car)
{
    if (!det_on[car]) return;
    stop_voice(&v_det[car]);
    det_on[car] = 0;
}

static int32_t det_volume(int car, int player)
{
    return fx_mul(Engine_DistanceFade(car, player), fx_mul(master, fx_mul(gms32(0x51f294), 0x10000)));
}

/* 0x479360's backfires, with the revs clamped (rpm) and the time in 1/100 s:
   - the pattern changes when its length runs out: a bang that ran is stopped; under 4,000 revs every
     other change is a pause (10 under 3,001, less above), otherwise a run of 5 to 14;
   - a change up to gears 2-6 in a car that backfires (0x40ee80, the class's flag at 0x516c40) bangs
     with a chance growing with the gear (90, 75, 50, 20 then 10 again, 10 in sixth); with option 5 so
     does any boost left (+0xa84);
   - closing the throttle above 5,000 revs (3,000 in neutral) starts a lift-off run of 10 to 19, which
     ends under 3,000 revs (2,800) or on the throttle;
   - a bang plays one of det1-4 and the exhaust's flame (0x45b550) for a quarter of the new length; the
     run plays them while it is not pausing, with a flame three times in four at its very start;
   - otherwise with the throttle closed under 3,000 revs (2,800 in neutral) the idle loop, louder as
     the revs fall. */
static void backfires(int car, int player, const Car *c, int rpm, int gear, int throttle, uint32_t now)
{
    if (bf_len[car] < now - bf_at[car]) {
        if (bf_end[car]) stop_det(car), bf_end[car] = 0;
        if (bf_quiet[car]) bf_quiet[car] = 0;
        else if (rpm < 4000) {
            bf_quiet[car] = 1;
            bf_at[car] = now;
            bf_len[car] = (uint32_t)(rpm < 3001 ? 10 : rpm < 5000 ? (3000 - rpm) * 10 / 2000 + 10 : 0);
            goto timed;
        }
        bf_at[car] = now;
        bf_len[car] = (uint32_t)(crt_rand() % 10 + 5);
    }
timed:;
    bool bang = false, fires = exe_u32(0x516c40 + 4u * (uint32_t)(int32_t)CI8(c, 0xb1b)) != 0;
    int g = gear_now[car];
    if (fires && 2 <= g && g <= 6 && g > gear_was[car]) {
        switch (g) {
        case 2: bang = crt_rand() % 100 >= 10; break;
        case 3: bang = crt_rand() % 100 > 24; break;
        case 4: bang = crt_rand() % 100 > 49; break;
        case 5: if (crt_rand() % 100 > 79) bang = true;
            /* falls through: the original draws again for fifth */
        case 6: if (crt_rand() % 100 > 89) bang = true; break;
        }
    }
    if (Gs_Option(5) && CI32(c, 0xa84) > 0) bang = true;
    int hi = gear == 0 ? 3000 : 5000, lo = gear == 0 ? 0xaf0 : 3000;
    if (!bf_on[car]) {
        if (fires && throttle == 0 && hi < rpm) {
            bf_on[car] = 1;
            bf_at[car] = bf_start[car] = now;
            bf_quiet[car] = 0;
            bf_len[car] = (uint32_t)(crt_rand() % 10 + 10);
        }
    } else if (rpm < lo || throttle != 0) bf_on[car] = 0;

    if (bf_on[car] || bang) {
        stop_idle(car);
        if (bang) {
            if (!Voice_Playing(v_det[car])) {
                v_det[car] = Voice_Play(base[car] + crt_rand() % 4 + DET1, 0, 0x5622, 0, false, false);
                bf_at[car] = now;
                det_on[car] = bf_end[car] = 1;
                bf_len[car] = (uint32_t)(crt_rand() % 10 + 5);
                Effects_Backfire(car, (int)(bf_len[car] >> 2));
            }
            Voice_SetVolume(v_det[car], det_volume(car, player));
        } else if (!bf_quiet[car]) {
            if (!Voice_Playing(v_det[car])) {
                v_det[car] = Voice_Play(base[car] + crt_rand() % 4 + DET1, 0, 0x5622, 0, false, false);
                det_on[car] = 1;
            }
            uint32_t at = bf_at[car], len = bf_len[car];
            Voice_SetVolume(v_det[car], det_volume(car, player));
            /* the lengths are 5 and up here; the original divides unguarded */
            if (len && (now * 100 - at * 100) / len == 0 && crt_rand() % 100 < 75) Effects_Backfire(car, crt_rand() % 2 + 2);
        } else if (!Voice_Playing(v_det[car])) v_det[car] = -1;
        else Voice_SetVolume(v_det[car], 0);
    } else if (throttle == 0) {
        stop_det(car);
        if (rpm < lo) {
            if (!idle_on[car]) {
                v_det[car] = Voice_Play(base[car] + DETIDLE, 0, 0x5622, 0, true, false);
                idle_on[car] = 1;
            }
            int32_t v = ((((0xaf0 - rpm) * 100) / 0x15e0 + 0x32) << 16) / 100;
            Voice_SetVolume(v_det[car], fx_mul(Engine_DistanceFade(car, player), fx_mul(master, fx_mul(v, gms32(0x51f294)))));
        }
    } else stop_idle(car);
}

/* 0x479360's engine loops: the slow band (fs, rs), the medium band off the throttle (fm, rm), the
   fast band on it (ft, rt, looped from the class's offset), the chatter when the throttle closes
   after a gear above 1 and 5,000 revs */
static void mix(int car, int player, int rpm2, int rpm, int speed, int gear, int throttle)
{
    uint32_t now = Timer_Centis();
    const Car *c = Car_Get(car);
    Band slow = band_at(0x51ea60, 0x51ea74, 0x51ea9e), mid = band_at(0x51eab8, 0x51eacc, 0x51ebca),
         fast = band_at(0x51ec50, 0x51ec64, 0x51ed62);
    gear_now[car] = gear;
    if (1 < gear) high_gear[car] = 1;
    if (5000 < rpm2) high_revs[car] = 1;
    int32_t fade = Engine_DistanceFade(car, player);
    if (!in_band(rpm2, &slow)) {
        Voice_SetFrequency(v_fs[car], 0x2b11), Voice_SetVolume(v_fs[car], 0);
        Voice_SetFrequency(v_rs[car], 0x2b11), Voice_SetVolume(v_rs[car], 0);
        stop_voice(&v_fs[car]);
        stop_voice(&v_rs[car]);
    } else {
        int32_t b = pct(band_vol(rpm2, &slow));
        uint32_t f = band_freq(rpm2, &slow);
        Voice_SetFrequency(loop_voice(&v_fs[car], car, FS, 0), f);
        Voice_SetVolume(v_fs[car], fx_mul(fade, fx_mul(master, fx_mul(b, k_fs))));
        Voice_SetFrequency(loop_voice(&v_rs[car], car, RS, 0), f);
        Voice_SetVolume(v_rs[car], fx_mul(fade, fx_mul(master, fx_mul(b, k_rs))));
    }
    if (!in_band(rpm2, &mid)) {
        Voice_SetFrequency(v_fm[car], 0x2b11), Voice_SetVolume(v_fm[car], 0);
        Voice_SetFrequency(v_rm[car], 0x2b11), Voice_SetVolume(v_rm[car], 0);
    } else {
        int32_t b = pct(band_vol(rpm2, &mid));
        int32_t off = 0x10000 - (int32_t)(((int64_t)CI32(c, 0x79c) << 16) / CI32(c, 0x788));
        uint32_t f = shifted(car, band_freq(rpm2, &mid));
        if (Voice_Playing(v_fm[car])) {
            Voice_SetFrequency(v_fm[car], f);
            Voice_SetVolume(v_fm[car], fx_mul(off, fx_mul(fade, fx_mul(master, fx_mul(b, k_fm)))));
        }
        if (Voice_Playing(v_rm[car])) {
            Voice_SetFrequency(v_rm[car], f);
            Voice_SetVolume(v_rm[car], fx_mul(off, fx_mul(fade, fx_mul(master, fx_mul(b, k_rm)))));
        }
    }
    ease_throttle(throttle, car);
    if (!gm8(0x539ed8)) backfires(car, player, c, rpm, gear, throttle, now);   /* not in a replay (0x427aa0) */
    if (!in_band(rpm2, &fast)) {
        stop_voice(&v_ft[car]);
        stop_voice(&v_rt[car]);
    } else if (thr[car] == 0) {
        stop_voice(&v_ft[car]);
        stop_voice(&v_rt[car]);
        if (high_gear[car] && high_revs[car] && !chattered[car]) {
            Voice_Play(base[car] + CHATTER, fx_mul(fade, fx_mul(master, fx_mul(gms32(0x51f290), 0x10000))), 0x5622, 0, false, false);
            chattered[car] = 1;
            high_gear[car] = high_revs[car] = 0;
        }
    } else {
        int b = band_vol(rpm2, &fast);
        chattered[car] = 0;
        uint32_t cls = exe_u32(0x516b40 + 4u * (gm32(0x531350 + 0x30u * (uint32_t)car) & 0x3f));
        uint32_t f = shifted(car, band_freq(rpm2, &fast));
        int32_t v = (thr[car] * b / 100 << 16) / 100;
        Voice_SetFrequency(loop_voice(&v_ft[car], car, FT, exe_u32(0x51f2a0 + 4 * cls)), f);
        Voice_SetVolume(v_ft[car], fx_mul(fade, fx_mul(master, fx_mul(v, k_ft))));
        Voice_SetFrequency(loop_voice(&v_rt[car], car, RT, exe_u32(0x51f2d8 + 4 * cls)), f);
        Voice_SetVolume(v_rt[car], fx_mul(fade, fx_mul(master, fx_mul(v, k_rt))));
    }
    whine(car, player, speed, gear, throttle);   /* 0x47a3d0 */
    gear_was[car] = gear_now[car];
}

/* 0x478c40: on the pause every engine voice of the players' cars (and the super special's second car
   with one player, not in mode 3 or a network game) at volume 0; the two voices of option 0x13
   (0x58ddc8) stop, which the port does not play yet (see Engine_Update) */
void Engine_Pause(void)
{
    uint32_t n = Gs_SelBits14();
    n += n == 1 && Gs_SelBit25() && !Gs_ModeBit19() && Gs_Mode() != 3;
    for (uint32_t i = 0; i < n && i < 4; i++) {
        int32_t *v[] = {&v_fm[i], &v_rm[i], &v_fs[i], &v_rs[i], &v_whine[i], &v_det[i], &v_ft[i], &v_rt[i]};
        for (int k = 0; k < 8; k++)
            if (Voice_Playing(*v[k])) Voice_SetVolume(*v[k], 0);
    }
}

void Engine_Update(int car, int player)
{
    /* the mix by the camera: inside (kinds 1-3) the front sounds louder, outside the rear */
    int k = Camera_Kind(player);
    if (k == 1 || k == 2 || k == 3) k_fs = 0xcccc, k_rs = 0x6666, k_fm = 0xc000, k_rm = 0x6000, k_ft = 0x10000, k_rt = 0x9999;
    else k_fs = 0x6666, k_rs = 0xcccc, k_fm = 0x6000, k_rm = 0xc000, k_ft = 0x9999, k_rt = 0x10000;
    loop_voice(&v_fm[car], car, FM, 0);
    loop_voice(&v_rm[car], car, RM, 0);
    if (!gm8(0x539ed8)) loop_voice(&v_whine[car], car, WHINE, 0);
    if (gm32(0x52e890) & 0x80000) PENDING_NOTE(0x47a710, "Engine_Update (option 0x13's sounds)");
    const Car *c = Car_Get(car);
    /* the revs as rpm (6,500 a unit through +0x798), the speed, the throttle as 0 or 100 */
    int rpm = fx_mul(fx_mul(CI32(c, 0x7ac), CI32(c, 0x798)), 0x19640000) >> 16;
    int rpm1 = rpm < 2000 ? 2000 : rpm > 0x2134 ? 0x2134 : rpm;
    int rpm2 = rpm + 2000;
    rpm2 = rpm2 < 2000 ? 2000 : rpm2 > 0x2134 ? 0x2134 : rpm2;
    int64_t t = ((int64_t)CI32(c, 0x79c) * 0x640000) >> 16;
    int throttle = ((t << 16) / CI32(c, 0x788)) > 0x4ffff ? 100 : 0;
    /* the speed: +0x778 by 0x431168 over 0x9ef9, 0 to 260 */
    int32_t m = (int32_t)(((int64_t)CI32(c, 0x778) * 0x431168) >> 16);
    int speed = (int32_t)(((int64_t)m << 16) / 0x9ef9) >> 16;
    speed = speed < 0 ? 0 : speed > 0x104 ? 0x104 : speed;
    mix(car, player, rpm2, rpm1, speed, CI8(c, 0xb1e), throttle);
}
