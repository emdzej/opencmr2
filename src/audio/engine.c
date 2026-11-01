/* The cars' engine sounds (see engine.h). */
#include "audio/engine.h"
#include "audio/snd.h"
#include "game/camera.h"
#include "game/gstate.h"
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

/* 0x4bb0a0: a vector's length (over 512 first when an axis is beyond 100 units) */
static int32_t length(const int32_t v[3])
{
    bool far = false;
    for (int i = 0; i < 3; i++) far |= v[i] > 0x640000 || v[i] < -0x640000;
    if (!far) return (int32_t)Fx_Sqrt((uint32_t)fx_mul(v[0], v[0]) + (uint32_t)fx_mul(v[1], v[1]) + (uint32_t)fx_mul(v[2], v[2]));
    int32_t s[3] = {v[0] / 512, v[1] / 512, v[2] / 512};
    return (int32_t)(Fx_Sqrt((uint32_t)fx_mul(s[0], s[0]) + (uint32_t)fx_mul(s[1], s[1]) + (uint32_t)fx_mul(s[2], s[2])) << 9);
}

/* 0x427d50: full within 2 units of the camera, silent beyond 100 */
static int32_t distance_fade(int car, int player)
{
    const Car *c = Car_Get(car);
    int32_t cam[3], v[3];
    Camera_Position(player, cam);
    /* (0x423d70: the followed frame's position, the car's orientation for a player) */
    v[0] = CI32(c, 0x30) - cam[0], v[1] = CI32(c, 0x34) - cam[1], v[2] = CI32(c, 0x38) - cam[2];
    int32_t d = length(v);
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
    if (Voice_Playing(*h)) Voice_Stop(*h);
    *h = -1;
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

/* 0x479360's engine loops: the slow band (fs, rs), the medium band off the throttle (fm, rm), the
   fast band on it (ft, rt, looped from the class's offset), the chatter when the throttle closes
   after a gear above 1 and 5,000 revs */
static void mix(int car, int player, int rpm2, int rpm, int gear, int throttle)
{
    const Car *c = Car_Get(car);
    Band slow = band_at(0x51ea60, 0x51ea74, 0x51ea9e), mid = band_at(0x51eab8, 0x51eacc, 0x51ebca),
         fast = band_at(0x51ec50, 0x51ec64, 0x51ed62);
    gear_now[car] = gear;
    if (1 < gear) high_gear[car] = 1;
    if (5000 < rpm2) high_revs[car] = 1;
    int32_t fade = distance_fade(car, player);
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
        uint32_t f = band_freq(rpm2, &mid);
        PENDING_NOTE(0x427e20, "Engine_Doppler (the replay)");
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
    if (!gm8(0x539ed8)) PENDING_NOTE(0x479360, "Engine_Mix (the backfires, the detonations, the idle)");
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
        uint32_t f = band_freq(rpm2, &fast);
        int32_t v = (thr[car] * b / 100 << 16) / 100;
        Voice_SetFrequency(loop_voice(&v_ft[car], car, FT, exe_u32(0x51f2a0 + 4 * cls)), f);
        Voice_SetVolume(v_ft[car], fx_mul(fade, fx_mul(master, fx_mul(v, k_ft))));
        Voice_SetFrequency(loop_voice(&v_rt[car], car, RT, exe_u32(0x51f2d8 + 4 * cls)), f);
        Voice_SetVolume(v_rt[car], fx_mul(fade, fx_mul(master, fx_mul(v, k_rt))));
    }
    PENDING_NOTE(0x47a3d0, "Car_SurfaceSounds");
    gear_was[car] = gear_now[car];
    (void)rpm;
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
    mix(car, player, rpm2, rpm1, CI8(c, 0xb1e), throttle);
}
