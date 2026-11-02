/* The race's process flow (game/race.c) on fra02 with the 205 at full throttle, with a minimal host:
   the loads, the line, the five-second countdown on the stage clock, the stage, the timer from the go. */
#include "c3d.h"
#include "exe.h"
#include "bfl.h"
#include "fixmath.h"
#include "game/effects.h"
#include "game/gstate.h"
#include "game/mem.h"
#include "game/race.h"
#include "game/shadow.h"
#include "render/polyq.h"
#include "game/skids.h"
#include "game/results.h"
#include "game/select.h"
#include "game/opponents.h"
#include "platform_host.h"
#include "audio/dsound.h"
#include "audio/snd.h"
#include "audio/codriver.h"
#include "audio/crash.h"
#include "sim/damage.h"
#include "sim/parts.h"
#include "game/lights.h"
#include "world/glow.h"
#include "world/particles.h"
#include "render/sprite3d.h"
#include <math.h>
#include "game/camera.h"
#include "game/hud.h"
#include "game/sectors.h"
#include "game/car_load.h"
#include "world/tod.h"
#include "game/stage_load.h"
#include "game/profile.h"
#include "game/timer.h"
#include "game/lamps.h"
#include "render/dlight.h"
#include "world/relight.h"
#include "render/d3d7.h"
#include "render/mesh.h"
#include "sim/car.h"
#include "sim/drive.h"
#include "sim/progress.h"
#include "sim/sim.h"
#include "sim/pose.h"
#include "sim/fade.h"
#include "sim/scenery.h"
#include "sim/knock.h"
#include "sim/aerials.h"
#include "sim/walls.h"
#include "sim/contact.h"
#include "game/balls.h"
#include "render/twod.h"
#include "vfs.h"
#include "vfs_host.h"
#include "world/fin.h"
#include "world/obj.h"
#include "world/section.h"
#include "world/stage.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static int stage_loads, car_loads;
static int32_t iabs32(int32_t v) { return v < 0 ? -v : v; }

static bool load_stage(void)
{
    stage_loads++;
    return Stage_Load("fra02", "Game\\Tracks\\France\\fra02hi.bfl", "Game\\Tracks\\France\\frahit.bfl");
}

static bool load_cars(void)
{
    car_loads++;
    if (!Car_Load(0)) return false;
    Sim_InitLists(1);   /* after the cars, as Cars_Load 0x456d90 does */
    Damage_SetDents(0, 0);   /* the scripted drive's wall hit would bend the car; the damage has its own check */
    Damage_CarsLoaded();
    Frame_SetRate(0x190000);   /* 25 frames a second, Cars_Load 0x466630 */
    Car_ProgressStart(Car_Get(0));
    return true;
}

static int press_camera;
static void input(int player, uint32_t *held, uint32_t *pressed)
{
    (void)player;
    *held = ACT_THROTTLE;
    *pressed = press_camera ? ACT_CAMERA : 0;
    press_camera = 0;
}

/* the lens texture's pixels of an alpha (Lens_Step keys them by the lamps' levels) */
static int lens_ff_stage = -1;
static int lens_alpha(uint8_t a)
{
    Tex *t = Lens_Tex(0);
    if (!t || !t->surface) return -1;
    int n = 0;
    for (int i = 0; i < t->surface->width * t->surface->height; i++) n += t->surface->rgba[4 * i + 3] == a;
    return n;
}

static Obj camera;
static Obj *host_camera(int player) { (void)player; return &camera; }
static int skids_laid, skids_stamped;
/* the shadows: the frames drawn, the farthest a wheel's skid centre sat from the car (x, z), the points */
static int shadow_frames, shadow_points;
static double shadow_far;
static void draw(int player, int state)
{
    (void)state;
    Codriver_Notes(player);   /* the overlays' speech each frame (the 2D drawing is the GPU tests') */
    for (int i = 0; i < g_all.n; i++) Car_GroundLight(i, player);   /* as play.c */
    Lamps_Lights(player);
    if (Shadow_Get(0)) {
        Shadow_Draw(Car_Get(0), player);   /* as play.c; the queue emptied again */
        Poly_Init();
        const uint8_t *r = Shadow_Get(0);
        const int32_t *pos = &Pose_Out(0)[12];
        for (int w = 0; w < 4; w++) {
            int32_t f[3], b[3];
            memcpy(f, r + 0x198 + 12 * w, sizeof f), memcpy(b, r + 0x1c8 + 12 * w, sizeof b);
            double dx = ((f[0] + b[0]) / 2 - pos[0]) / 65536.0, dz = ((f[2] + b[2]) / 2 - pos[2]) / 65536.0;
            double d = sqrt(dx * dx + dz * dz);
            if (d > shadow_far && shadow_frames < 1000) shadow_far = d;   /* (later the test moves the car and loosens its parts) */
        }
        shadow_frames++;
        shadow_points = (int16_t)(r[0x292] | r[0x293] << 8);
    }
    Skids_Centres(player);   /* as play.c, without the drawing */
    Skids_Add(player);
    int laid = 0, stamped = 0;
    for (uint32_t i = 0; i < 800; i++)
        if (gm8(0x548220 + i)) laid++, stamped += gm32(0x549da0 + 0x28 * i + 4) != 0;
    if (laid > skids_laid) skids_laid = laid, skids_stamped = stamped;
}
static const RaceHost host = {load_stage, load_cars, NULL, input, NULL, draw, host_camera};

int main(void)
{
    char err[128];
    if (!vfs_mount_default() || !exe_init(err, sizeof err)) {
        printf("race_test: no game data, skipped\n");
        return 0;
    }
    Math_InitTables();
    CHECK(Mem_Init());
    D3d_Open(640, 480, 16);
    VB_CreatePool();
    memset(&g_world, 0, sizeof g_world);
    g_world.kind = 3;
    Options_Defaults();
    Profiles_Init();
    Snd_Init();
    Player_SetProfile(0, 0);
    Profile_Create(0);   /* the front end's player set-up: profile 0, the 205 */
    gw32(0x531350, (gm32(0x531350) & ~0x3fu) | (uint32_t)Car_IdByName("205"));
    Tod_Select("fra02");   /* the front end's selection: one player, the mode's flags (0x408020) */
    gw32(0x52afb4, (gm32(0x52afb4) & 0xfffe1fffu) | 1u << 13);
    Sel_ModeFlags();
    Race_Begin(&host);
    CHECK(Game_Mode() == 3);
    int seen[14] = {0}, frame = 0, go = -1, fin = -1;
    for (; frame < 60 * 20 && Game_Mode() == 3; frame++) {
        Timer_Sample((uint32_t)((uint64_t)frame * 1000 / 60));
        Race_Frame();
        int st = Race_State(0);
        if (st >= 0 && st < 14 && !seen[st]) {
            seen[st] = frame + 1;
            printf("frame %4d: state %d, stage time %d, clock %d\n", frame, st, Race_StageTime(0), Race_Clock());
        }
        if (st == 8 && go < 0) go = frame;
        /* the start lights (0x463410, 0x463bd0): lit by the countdown's steps, the glows queued as
           sprites; at the go a different set (the greens) */
        static int lit_count = -1, lit_go = -1;
        if (st == 7 && frame - seen[7] == 200 && lit_count < 0) {
            lit_count = 0;
            for (int i = 0; i < Lights_Count(); i++) lit_count += Lights_Level(i) == 0x10000;
        }
        if (st == 8 && frame - go == 30 && lit_go < 0) {
            lit_go = 0;
            for (int i = 0; i < Lights_Count(); i++) lit_go += Lights_Level(i) == 0x10000;
            printf("start lights: %d, %d lit in the countdown, %d after the go; %d glows\n", Lights_Count(), lit_count, lit_go, Glow_Used());
            CHECK(Lights_Count() >= 7 && lit_count > 0 && lit_go > 0 && Glow_Used() >= Lights_Count());
        }
        if (st == 8 && frame - go == 30) lens_ff_stage = lens_alpha(0xff);
        if (st == 8 && frame - go == 60) {
            CHECK(Camera_Kind(0) == 4);
            press_camera = 1;   /* the camera key: from the chase (4) to the far chase (5), blended */
        }
        if (st == 8 && frame - go == 120) {
            printf("camera kind after the key: %d\n", Camera_Kind(0));
            CHECK(Camera_Kind(0) == 5);
        }
        if (getenv("RACE_DBG") && st >= 7 && frame % 30 == 0) {
            Car *c = Car_Get(0);
            printf("  pos %.1f %.1f %.1f b43 %d a98 %08x f%d st%d gear %d b84 %d b20 %d b24 %d b1f %d b48 %d revs %.3f thr %d hb %d b9c %d km/h %.1f\n",
                   CI32(c, 0x2d0) / 65536.0, CI32(c, 0x2d4) / 65536.0, CI32(c, 0x2d8) / 65536.0, c->s[0xb43], (unsigned)CI32(c, 0xa98), frame, st, CI8(c, 0xb1e), CI32(c, 0xb84), CI8(c, 0xb20), CI8(c, 0xb24), CI8(c, 0xb1f), CI32(c, 0xb48),
                   CI32(c, 0x7a4) / 65536.0, c->s[0x1d2], CI32(c, 0x1d8), CI32(c, 0xb9c), CI32(c, 0x778) / 65536.0 * 90);
        }
        if (st == 8 && fin < 0 && frame - go == 180) {
            Car *c = Car_Get(0);
            printf("camera at (%.1f, %.1f, %.1f), car at (%.1f, %.1f, %.1f)\n", camera.local.m[12] / 65536.0,
                   camera.local.m[13] / 65536.0, camera.local.m[14] / 65536.0, CI32(c, 0x2d0) / 65536.0, CI32(c, 0x2d4) / 65536.0,
                   CI32(c, 0x2d8) / 65536.0);
            printf("3 s after the go: stage time %d, %.1f km/h, %.1f%% of the stage\n", Race_StageTime(0),
                   CI32(c, 0x778) / 65536.0 * 90, Car_StageShare(c) * 100.0 / 65536);
            /* the poses (Car_PoseInterp): the front left wheel's matrix turns from frame to frame */
            static int32_t last[12];
            Obj *w = c->wheel[0];
            CHECK(w != NULL);
            if (w) {
                int diff = 0;
                for (int q = 0; q < 12; q++) diff += w->local.m[q] != last[q], last[q] = w->local.m[q];
                Timer_Sample((uint32_t)((uint64_t)(frame + 2) * 1000 / 60));
                Race_Frame();
                diff = 0;
                for (int q = 0; q < 12; q++) diff += w->local.m[q] != last[q];
                printf("wheel 0: %d of 12 terms changed over a step\n", diff);
                CHECK(diff > 0);
            }
            /* the aerials (0x484e00): the 205 has its .cin's whips, swinging back in the air at speed */
            {
                int na = Aerials_Count(0);
                int32_t sw[2] = {0, 0}, most = 0;
                for (int k = 0; k < na; k++) {
                    Aerials_Down(0, k, sw);
                    int32_t m = (sw[0] < 0 ? -sw[0] : sw[0]) + (sw[1] < 0 ? -sw[1] : sw[1]);
                    if (m > most) most = m;
                }
                CHECK(na == 0 || most > 0);
            }
            printf("HUD race time %d\n", Times_Race());
            /* the engine (0x47a710): its 13 sounds loaded (after the collisions' 21, the co-driver's 59 and
               the HUD's 5), the mixer not silent */
            static float pcm[1470 * 2];
            Dsb_Mix(pcm, 1470);
            double e = 0;
            for (int q = 0; q < 1470 * 2; q++) e += pcm[q] * pcm[q];
            printf("engine: %d sounds, rms %.4f\n", Snd_Count(), sqrt(e / (1470 * 2)));
            CHECK(Snd_Count() == 21 + 59 + 5 + 13 && e > 0);
            CHECK(Times_Race() >= 290 && Times_Race() <= 310);
            CHECK(Race_StageTime(0) >= 290 && Race_StageTime(0) <= 310 && CI32(c, 0x778) > 0x8000);
            fin = frame;
        }
    }
    CHECK(stage_loads == 1 && car_loads == 1);
    /* the co-driver: three, two, one, go, then the calls the car passed (spoken in the overlays) */
    for (int q = 0; q < 4; q++) Race_DrawOverlays(0);
    printf("co-driver: %d words queued, the car at point %d\n", Codriver_Said(), Car_CatIndex(Car_Get(0)));
    CHECK(Codriver_Said() > 4);
    /* the overlays: the time panel's box and the recovery's fade (0x428680), two triangles each, and
       the split bar (0x4143f0): a box per split (the passed part apart) and marks between them */
    if (Game_Mode() == 3) {
        CW32(Car_Get(0), 0xbf8, 1);
        for (int k = 0; k < 6; k++, frame++) {
            Timer_Sample((uint32_t)((uint64_t)frame * 1000 / 60));
            Race_Frame();
        }
        Tri_Init();
        int before = Tri_Queued();
        Race_DrawOverlays(0);
        printf("fade: level %.3f, %d triangles queued\n", Fade_LevelOf(0) / 65536.0, Tri_Queued() - before);
        CHECK(Fade_LevelOf(0) > 0 && Tri_Queued() - before >= 4 + 2 * Sectors_Count());
    }
    CHECK(seen[5] && seen[7] && seen[8] && seen[5] < seen[7] && seen[7] < seen[8]);
    /* the countdown: five seconds of 40 ms steps from its first frame, 300 frames at 60 Hz */
    CHECK(seen[8] - seen[7] >= 299 && seen[8] - seen[7] <= 302 && fin > 0);
    /* knocking down (0x46fe70): a movable object hit by car 0 at 80 km/h (0.9 units a step) is lowered out of
       sight for it, a copy of its mesh tips or is flung and comes to rest on the ground */
    {
        Car *c = Car_Get(0), keep = *c;
        int nm = Scenery_Movables();
        printf("knock: %d movable entries\n", nm);
        CHECK(nm > 0);
        if (nm > 0) {
            Scenery *sc = Scenery_Movable(0);
            int32_t y0 = sc->pos[1], p0[3], p[3];
            CW32(c, 0x408, 0xe666), CW32(c, 0x40c, 0), CW32(c, 0x410, 0), CW32(c, 0x778, 0xe666);
            Scenery_Knock(0, Section_Find(sc->pos), 0);
            int st = Knock_State(0, p0), k = 0, moving = 0;
            CHECK(Knock_Count() == 1 && !Scenery_UpFor(0, 0) && sc->pos[1] == y0 - 0x3e80000 && st > 0);
            for (; k < 400 && Knock_State(0, p) != 0; k++) Knock_Step(), moving++;
            Knock_State(0, p);
            printf("knock: state %d, still after %d steps, moved (%.2f, %.2f, %.2f)\n", st, moving,
                   (p[0] - p0[0]) / 65536.0, (p[1] - p0[1]) / 65536.0, (p[2] - p0[2]) / 65536.0);
            CHECK(moving > 0 && k < 400);
            Knock_Restore();
            CHECK(Knock_Count() == 0 && Scenery_UpFor(0, 0) && sc->pos[1] == y0);
        }
        *c = keep;
    }
    /* the scenery (0x4878a0): the car put with a tree's trunk 0.3 units ahead of its centre is pushed
       out of it, the trunk then outside its flat box */
    {
        Car *c = Car_Get(0);
        int boxes, n = Scenery_CollCount(&boxes), tree = -1;
        int32_t tp[3], tr = 0;
        bool tb = true;
        uint32_t tf = 0;
        for (int i = 0; i < n && tree < 0; i++)
            if (Scenery_CollAt(i, tp, &tr, &tb, &tf) && !tb && !(tf & 0x4000) && tr >= 0x4000) tree = i;
        printf("scenery: %d fixed volumes, %d boxes; a trunk of radius %.2f\n", n, boxes, tr / 65536.0);
        CHECK(n > 100 && tree >= 0);
        if (tree >= 0) {
            int32_t fx = CI32(c, 0x360), fz = CI32(c, 0x368), at[3] = {tp[0] - fx_mul(fx, 0x4ccc), tp[1] + 0x8000, tp[2] - fx_mul(fz, 0x4ccc)};
            int32_t d[3] = {at[0] - CI32(c, 0x2d0), at[1] - CI32(c, 0x2d4), at[2] - CI32(c, 0x2d8)};
            for (uint32_t k = 0; k < 3; k++) {
                CW32(c, 0x2d0 + 4 * k, CI32(c, 0x2d0 + 4 * k) + d[k]);
                CW32(c, 0x2e8 + 4 * k, CI32(c, 0x2e8 + 4 * k) + d[k]);
                for (uint32_t i = 0; i < 8; i++) CW32(c, 0x270 + 12 * i + 4 * k, CI32(c, 0x270 + 12 * i + 4 * k) + d[k]);
            }
            CW16(c, 0xb00, (uint16_t)Section_Find(at));
            for (uint32_t i = 0; i < 3; i++) CW16(c, 0xb02 + 2 * i, 0xffff);
            CW32(c, 0xc18, 0);
            int32_t v0 = CI32(c, 0x408), v2 = CI32(c, 0x410);
            int16_t list[1] = {0};
            Sim_Collide(list, 1);
            int32_t ex = tp[0] - CI32(c, 0x2d0), ez = tp[2] - CI32(c, 0x2d8);
            int32_t *b = Walls_CarBox(c);
            int32_t ef = fx_mul(b[B_FWD], ex) + fx_mul(b[B_FWD + 2], ez);
            printf("scenery: the car moved %.2f back, the trunk %.2f ahead (half length %.2f), velocity (%.2f, %.2f) to (%.2f, %.2f)\n",
                   (fx_mul(fx, at[0] - CI32(c, 0x2d0)) + fx_mul(fz, at[2] - CI32(c, 0x2d8))) / 65536.0, ef / 65536.0, b[B_EXT_F] / 65536.0,
                   v0 / 65536.0, v2 / 65536.0, CI32(c, 0x5c4) / 65536.0, CI32(c, 0x5cc) / 65536.0);
            CHECK(ef >= b[B_EXT_F] + tr - 0x100);
            /* a hit's sound (Car_Damage 0x466ef0): an impulse of 0.55 on the wheels plays COLLIS2 (0x418ba0:
               the collisions load first, so id 5) in a slot, heard, the car's +0xb46 raised to 4, the view
               shaken (0x4241d0); off the wheels a roll (0x418c30: ROLL1-4, ids 10-13) */
            int32_t keep[3], pt[3] = {CI32(c, 0x2d0), CI32(c, 0x2d4), CI32(c, 0x2d8)}, up[3] = {0, 0x10000, 0};
            for (uint32_t i = 0; i < 3; i++) keep[i] = CI32(c, 0x5c4 + 4 * i);
            CW32(c, 0x5c4, 0x8ccc), CW32(c, 0x5c8, 0), CW32(c, 0x5cc, 0);
            c->s[0xb46] = 0;
            gw32(0x588a88, Timer_Centis() - 11);   /* the trunk's hit (strength 0) was the last, just now */
            Car_Damage(c, pt, up, 0, 0, false);
            int32_t h = -1;
            for (int i = 0; i < 4; i++)
                if (Crash_Voice(i) >> 8 == 5) h = Crash_Voice(i);
            printf("crash: COLLIS2's voice %d at volume %.3f, +0xb46 %d, shake %.2f\n", h,
                   h < 0 ? 0.0 : Voice_Volume(h) / 65536.0, c->s[0xb46], Camera_ShakeLevel(0, 10) / 65536.0);
            CHECK(h >= 0 && Voice_Playing(h) && Voice_Volume(h) > 0 && c->s[0xb46] == 4 && Camera_ShakeLevel(0, 10) > 0);
            int32_t up_was = CI32(c, 0xb74), roll = -1;
            CW32(c, 0xb74, 0);
            Car_Damage(c, pt, up, 0, 0, false);
            for (int i = 0; i < 4; i++)
                if (Crash_Voice(i) >> 8 >= 10 && Crash_Voice(i) >> 8 <= 13) roll = Crash_Voice(i) >> 8;
            printf("crash: off the wheels sound %d\n", roll);
            CHECK(roll >= 10);
            CW32(c, 0xb74, up_was);
            for (uint32_t i = 0; i < 3; i++) CW32(c, 0x5c4 + 4 * i, keep[i]);
            /* a fixed box (0x487f60) 2 by 2 by 2, square to the car, its near face 0.5 units into the
               front of the car's box: pushed back until the faces meet */
            b[B_VALID] = 0;
            b = Walls_CarBox(c);
            int32_t ext = b[B_EXT_F], ux = b[B_FWD], uz = b[B_FWD + 2];
            int32_t bp[3] = {CI32(c, 0x2d0) + fx_mul(ux, ext + 0x8000), CI32(c, 0x2d4) - 0x8000, CI32(c, 0x2d8) + fx_mul(uz, ext + 0x8000)};
            CW32(c, 0x2e8, CI32(c, 0x2d0)), CW32(c, 0x2ec, CI32(c, 0x2d4)), CW32(c, 0x2f0, CI32(c, 0x2d8));
            int16_t bx[6] = {(int16_t)(ux >> 9), (int16_t)(uz >> 9), 0x100, 0, 0x80, 0x80};
            bool hitbox = Scenery_CollTestBox(c, bp, bx, 0);
            int32_t gap = fx_mul(ux, bp[0] - CI32(c, 0x2d0)) + fx_mul(uz, bp[2] - CI32(c, 0x2d8)) - ext - 0x10000;
            printf("scenery: a box %s, the faces %.3f apart after the push\n", hitbox ? "hit" : "missed", gap / 65536.0);
            CHECK(hitbox && iabs32(gap) < 0x800);
            /* car to car (0x48a1f0): car 0 copied into slot 1 half a unit into its nose, car 0 at 10
               units a second, car 1 still: each takes half the push (the faces meet), and the hit
               is inelastic with equal masses (the impulses equal and opposite along the axis) */
            {
                Car keep0 = *c, *d = Car_Get(1), keep1 = *d;
                *d = *c;
                CW8(d, 0xb1a, 1);
                int32_t sh = 2 * ext - 0x8000, s3[3] = {fx_mul(ux, sh), 0, fx_mul(uz, sh)};
                for (uint32_t k = 0; k < 3; k++) {
                    CW32(d, 0x2d0 + 4 * k, CI32(d, 0x2d0 + 4 * k) + s3[k]);
                    for (uint32_t i = 0; i < 8; i++) CW32(d, 0x270 + 12 * i + 4 * k, CI32(d, 0x270 + 12 * i + 4 * k) + s3[k]);
                    CW32(c, 0x2e8 + 4 * k, CI32(c, 0x2d0 + 4 * k)), CW32(d, 0x2e8 + 4 * k, CI32(d, 0x2d0 + 4 * k));
                    CW32(c, 0x5c4 + 4 * k, 0), CW32(d, 0x5c4 + 4 * k, 0), CW32(c, 0x420 + 4 * k, 0), CW32(d, 0x420 + 4 * k, 0);
                    CW32(d, 0x408 + 4 * k, 0);
                }
                CW32(c, 0x408, fx_mul(ux, 0xa0000)), CW32(c, 0x40c, 0), CW32(c, 0x410, fx_mul(uz, 0xa0000));
                Walls_BoxRec(0)[B_VALID] = 0, Walls_BoxRec(1)[B_VALID] = 0;
                int16_t two[2] = {0, 1};
                Cars_Collide(two, 2);
                int32_t sep = fx_mul(ux, CI32(d, 0x2d0) - CI32(c, 0x2d0)) + fx_mul(uz, CI32(d, 0x2d8) - CI32(c, 0x2d8)) - 2 * ext;
                int32_t ja = fx_mul(ux, CI32(c, 0x5c4)) + fx_mul(uz, CI32(c, 0x5cc)), jb = fx_mul(ux, CI32(d, 0x5c4)) + fx_mul(uz, CI32(d, 0x5cc));
                printf("cars: the boxes %.3f apart after the push, impulses %.3f and %.3f along the axis, knocked %d %d\n",
                       sep / 65536.0, ja / 65536.0, jb / 65536.0, CI32(c, 0xc00), CI32(d, 0xc00));
                CHECK(iabs32(sep) < 0x800 && ja < 0 && jb > 0 && iabs32(ja + jb) < 0x100);
                *c = keep0, *d = keep1;
                Walls_BoxRec(0)[B_VALID] = 0, Walls_BoxRec(1)[B_VALID] = 0;
            }
            /* option 0's balls (0x47d510-0x47e1e0): one fired from car 1 (a copy of car 0, still) at
               5 a step rolls ahead on the ground; one fired from car 1 a unit behind car 0 is inside car
               0's box: the car knocked, the ball gone */
            {
                Car keep0 = *c, *d = Car_Get(1), keep1 = *d;
                *d = *c;
                CW8(d, 0xb1a, 1);
                for (uint32_t k = 0; k < 3; k++) CW32(d, 0x408 + 4 * k, 0), CW32(c, 0x5c4 + 4 * k, 0), CW32(c, 0x5d0 + 4 * k, 0);
                CW32(d, 0x408, fx_mul(ux, 0x50000)), CW32(d, 0x410, fx_mul(uz, 0x50000));
                CW32(c, 0xc00, 0);
                Balls_Setup();
                Balls_Fire(1);
                int32_t p0[3], p1[3], pz[3];
                bool fired = Balls_At(0, p0);
                for (int i = 0; i < 10; i++) Balls_Step();
                bool rolled = Balls_At(0, p1);
                int32_t ahead = fx_mul(ux, p1[0] - p0[0]) + fx_mul(uz, p1[2] - p0[2]);
                printf("balls: fired %d, %.3f ahead after 10 steps\n", fired, ahead / 65536.0);
                CHECK(fired && rolled && ahead > 0);
                Balls_Setup();
                CW32(d, 0x408, 0), CW32(d, 0x410, 0);
                CW32(d, 0x408, fx_mul(ux, 0x8000)), CW32(d, 0x410, fx_mul(uz, 0x8000));
                int32_t back1[3] = {-ux, 0, -uz};   /* the ball starts just past the nose: car 1 a unit behind */
                for (uint32_t k = 0; k < 3; k++) {
                    CW32(d, 0x2d0 + 4 * k, CI32(d, 0x2d0 + 4 * k) + back1[k]);
                    for (uint32_t q = 0; q < 8; q++) CW32(d, 0x270 + 12 * q + 4 * k, CI32(d, 0x270 + 12 * q + 4 * k) + back1[k]);
                }
                Balls_Fire(1);
                Walls_BoxRec(0)[B_VALID] = 0;
                Balls_Hit(c, Walls_CarBox(c));
                bool gone = !Balls_At(0, pz);
                printf("balls: the hit %s, impulse (%.3f, %.3f, %.3f), knocked %d\n", gone ? "taken" : "missed",
                       CI32(c, 0x5c4) / 65536.0, CI32(c, 0x5c8) / 65536.0, CI32(c, 0x5cc) / 65536.0, CI32(c, 0xc00));
                CHECK(gone && CI32(c, 0xc00) == 1 && CI32(c, 0x5c8) > 0);
                Balls_Setup();
                *c = keep0, *d = keep1;
                Walls_BoxRec(0)[B_VALID] = 0, Walls_BoxRec(1)[B_VALID] = 0;
            }
            /* the damage (0x4669f0, 0x466ef0): with dents on the set-up fills the second model's slots;
               a flat hit of 0.5 on the nose logs an entry, dents the meshes (the crumple, +0x284) and
               raises the cells; Damage_Reset with that entry carried replays it to the same crumple,
               with none carried puts the meshes back; a round hit (a trunk) dents too */
            Damage_SetDents(1, 0);
            Damage_Clear(0, g_all.n);
            Damage_ResetLogs();
            Damage_Setup(false, false, g_all.car, g_all.n);
            const uint8_t *dr = Damage_Get(0);
            uint8_t *lg = Damage_Log(0);
            int32_t nose[3] = {CI32(c, 0x2d0) + fx_mul(ux, ext), CI32(c, 0x2d4), CI32(c, 0x2d8) + fx_mul(uz, ext)};
            int32_t back[3] = {-ux, 0, -uz}, rel[3] = {nose[0] - CI32(c, 0x2d0), 0, nose[2] - CI32(c, 0x2d8)}, loc[3];
            Walls_RotT(c, loc, rel);
            for (uint32_t i = 0; i < 3; i++) CW32(c, 0x5dc + 4 * i, loc[i]), keep[i] = CI32(c, 0x5c4 + 4 * i);
            int32_t crumple0 = DI32(dr, 0x284);
            CW32(c, 0x5c4, fx_mul(back[0], 0x8000)), CW32(c, 0x5c8, 0), CW32(c, 0x5cc, fx_mul(back[2], 0x8000));
            Car_Damage(c, nose, back, 0, 0, false);
            int32_t cells = 0, crumple1 = DI32(dr, 0x284);
            for (uint32_t i = 0; i < 9; i++) cells += DI32(dr, 0x21c + 4 * i);
            printf("damage: %d slots, %d hit logged (strength %d/255), cells %.3f, crumple %.4f (was %.4f), engine %.3f\n",
                   DI32(dr, 0x45c), lg[0x20a], lg[0x106], cells / 65536.0, crumple1 / 65536.0, crumple0 / 65536.0, DI32(dr, 0x404) / 65536.0);
            CHECK(DI32(dr, 0x45c) > 0 && lg[0x20a] == 1 && cells > 0 && crumple0 == 0 && crumple1 > 0);
            memcpy(lg, lg + 0x106, 0x106);
            Damage_Reset(c);
            int32_t crumple2 = DI32(dr, 0x284);
            lg[0x104] = 0;
            Damage_Reset(c);
            int32_t crumple3 = DI32(dr, 0x284);
            CW32(c, 0x5c4, 0x6000), CW32(c, 0x5c8, 0), CW32(c, 0x5cc, 0);
            Car_Damage(c, nose, back, 0x8000, 1, false);
            int32_t crumple4 = DI32(dr, 0x284);
            printf("damage: replayed crumple %.4f, none carried %.4f, a round hit %.4f\n", crumple2 / 65536.0, crumple3 / 65536.0, crumple4 / 65536.0);
            CHECK(crumple2 == crumple1 && crumple3 == 0 && crumple4 > 0);
            /* the finish (0x469a80) keeps the panels as 255ths and stops the logging; the store
               (0x469b50) carries the log to the next stage's set-up, which replays it */
            Damage_Finish(0);
            int logged = lg[0x20a], panel = lg[0x24c + 23];
            Car_Damage(c, nose, back, 0x8000, 1, false);
            Damage_Store(0);
            Damage_Setup(false, false, g_all.car, g_all.n);
            int carried = lg[0x104];
            int32_t crumple5 = DI32(dr, 0x284);
            printf("damage: %d logged at the finish (none after), engine panel %d/255, %d carried, crumple %.4f\n",
                   logged, panel, carried, crumple5 / 65536.0);
            CHECK(logged == 2 && lg[0x20a] == 2 && carried == 2 && crumple5 > 0);
            /* the misfire (0x46b4e0): a broken engine (panel 23 at 1.0) cuts out for up to 12 steps,
               then runs for up to 250 */
            uint8_t *dw = Damage_Get(0);
            uint8_t one[4] = {0, 0, 1, 0}, zero[4] = {0};
            memcpy(dw + 0x29c, one, 4), memcpy(dw + 0x40c, zero, 4), memcpy(dw + 0x4cc, zero, 4);
            Damage_Misfire(c);
            int32_t cut = DI32(dr, 0x40c), cutting = DI32(dr, 0x4cc);
            int steps = 0;
            while (DI32(dr, 0x40c) > 0 && steps < 100) Damage_Misfire(c), steps++;
            Damage_Misfire(c);
            int32_t run = DI32(dr, 0x40c);
            printf("damage: cuts out for %.2f steps (%d counted), then runs for %.2f\n", cut / 65536.0, steps, run / 65536.0);
            CHECK(cutting == 1 && cut >= 0 && cut <= 12 * 0x10000 && DI32(dr, 0x4cc) == 0 &&
                  run >= fx_mul(0x3333, 50 << 16) && run <= fx_mul(0x3333, 300 << 16));
            /* the loose parts (0x480bb0): a part whose panel passes its threshold (0.5; 0.2 for type
               3) is taken loose at the step; type 1 (0x4816f0) at speed wobbles its drawn matrix
               (0x484d30) off rest and, past 0.8, swings free of one end (the 205's class 11 only
               wobbles, so class 0 here); Damage_Reset lets every part go with its rest matrix back */
            memset(gm(0x52ebc0), 0, 0x148 * 4);
            Damage_ResetLogs();
            Damage_Setup(false, false, g_all.car, g_all.n);
            {
                static const uint32_t pan[4] = {0x16, 0x14, 0x15, 0x12};
                int have = 0, loose = 0;
                for (int k = 0; k < 4; k++) have |= (Parts_Obj(0, k) != NULL) << k;
                CarList one = {{0}, 1};
                uint8_t cls = c->s[0xb1b];
                c->s[0xb1b] = 0;
                Parts_Step(&one);
                for (int k = 0; k < 4; k++) loose |= Parts_Active(0, k) << k;
                CHECK(loose == 0);
                for (int k = 0; k < 4; k++) memcpy(dw + 0x240 + 4 * pan[k], (uint8_t[4]){0, 0x90, 0, 0}, 4);
                Parts_Step(&one);
                for (int k = 0; k < 4; k++) loose |= Parts_Active(0, k) << k;
                int32_t speed = CI32(c, 0x778);
                CW32(c, 0x778, 0x30000);
                Obj *o1 = Parts_Obj(0, 1);
                int32_t moved = 0, moved_k[4] = {0, 0, 0, 0};
                for (int i = 0; i < 25; i++) {
                    /* braking hard for the first steps: the flap (type 2, 0x484310) and the hatch
                       (type 3, 0x483570) swing with the car's deceleration */
                    if (i < 5) CW32(c, 0x414, CI32(c, 0x408) + fx_mul(CI32(c, 0x360), 0x2000)),
                               CW32(c, 0x41c, CI32(c, 0x410) + fx_mul(CI32(c, 0x368), 0x2000));
                    Parts_Step(&one);
                    Parts_Interp(0x10000);
                    for (int k = 0; k < 4; k++) {
                        Obj *ok = Parts_Obj(0, k);
                        for (int j = 0; ok && j < 16; j++) {
                            int32_t d = ok->local.m[j] - ok->rest.m[j];
                            if ((d < 0 ? -d : d) > moved_k[k]) moved_k[k] = d < 0 ? -d : d;
                        }
                    }
                }
                moved = moved_k[1];
                printf("parts: moved %.4f %.4f %.4f %.4f\n", moved_k[0] / 65536.0, moved_k[1] / 65536.0, moved_k[2] / 65536.0, moved_k[3] / 65536.0);
                memcpy(dw + 0x240 + 4 * pan[1], (uint8_t[4]){0, 0xf0, 0, 0}, 4);
                Parts_Step(&one);
                uint8_t f1 = Parts_Flags(0, 1);
                CW32(c, 0x778, speed);
                c->s[0xb1b] = cls;
                Damage_Reset(c);
                int still = 0;
                for (int k = 0; k < 4; k++) still |= Parts_Active(0, k) << k;
                bool rest = !o1 || memcmp(&o1->local, &o1->rest, sizeof o1->local) == 0;
                printf("parts: objects %#x, loose %#x, type 1 moved %.4f, flags %#x, after the reset %#x (at rest %d)\n",
                       have, loose, moved / 65536.0, f1, still, rest);
                CHECK(have & 2);
                CHECK(!(have & 8) || moved_k[3] > 0);
                CHECK(!(have & 4) || moved_k[2] > 0);
                CHECK(loose == have && moved > 0 && (f1 & 0x13) == 0x13 && still == 0 && rest);
                for (int k = 0; k < 4; k++) memset(dw + 0x240 + 4 * pan[k], 0, 4);
            }
            memset(gm(0x52ebc0), 0, 0x148 * 4);
            Damage_ResetLogs();
            Damage_Setup(false, false, g_all.car, g_all.n);
            Damage_SetDents(0, 0);
            for (uint32_t i = 0; i < 3; i++) CW32(c, 0x5c4 + 4 * i, keep[i]);
        }
    }
    /* the sectors (0x4581d0): the car walked along the centre line passes each split and finishes */
    {
        Car *c = Car_Get(0);
        int splits = 0, at = -1;
        for (int i = Car_CatIndex(c); i < Cat_Points() && at < 0; i++) {
            int32_t p[3];
            Cat_Point(i, p);
            CW32(c, 0x2d0, p[0]), CW32(c, 0x2d8, p[2]);
            Car_Progress(c);
            Sectors_Step();
            splits += Sectors_Split(0);
            if (Sectors_Finished(0)) at = i;
        }
        printf("sectors: %d split points, %d passed, finished at point %d of %d\n", Sectors_Count(), splits, at,
               Cat_FinishPoint());
        CHECK(at > 0 && splits >= 1);
        /* past the finish: state 9 for four seconds, then the replay with the results' pages; Enter
           through the title, times, splits and board, up (round) to "quit", Enter: the fade, the
           results state, the front end */
        int f2 = 0;
        int s9 = 0, brake_lit = 0, lens_ff_brake = -1;
        int32_t brake_lens = 0, brake_glow = 0;
        for (; f2 < 900 && Race_State(0) != 10; f2++, frame++) {
            Timer_Sample((uint32_t)((uint64_t)frame * 1000 / 60));
            Race_Frame();
            if (Race_State(0) == 9 && ++s9 == 100) {
                /* the brake lamps (kinds 0 and 1) after the finish, the car braking to a stop */
                for (int k = 0; k < Lamps_Count(0); k++) brake_lit += Lamps_On(0, k), brake_glow = Lamps_On(0, k) ? Lamps_Glow(0, k) : brake_glow;
                Lens_Get(0, &brake_lens, NULL, 1);
                lens_ff_brake = lens_alpha(0xff);
            }
        }
        printf("results after %d frames: state %d, active %d\n", f2, Race_State(0), Results_Active());
        printf("lamps: %d, %d lit braking after the finish at %.3f, lens %.3f, its opaque pixels %d (%d on the stage)\n",
               Lamps_Count(0), brake_lit, brake_glow / 65536.0, brake_lens / 65536.0, lens_ff_brake, lens_ff_stage);
        CHECK(Lamps_Count(0) > 4 && brake_lit == 2 && brake_glow == fx_mul(0xff00, 0xcccc) && brake_lens == 0xff00 &&
              lens_ff_brake > lens_ff_stage);
        /* the computer drivers' times from fra02.tm<difficulty>: in order, a few minutes */
        printf("computer drivers: %d, fastest %d, 2nd %d, slowest %d (1/100 s)\n", Opp_Computers(), Opp_Time(0), Opp_Time(1),
               Opp_Time(Opp_Computers() - 1));
        CHECK(Opp_Computers() == 15 && Opp_Time(0) > 6000 && Opp_Time(0) <= Opp_Time(1) &&
              Opp_Time(1) <= Opp_Time(Opp_Computers() - 1));
        CHECK(Race_State(0) == 10 && Results_Active());
        int pages = 0;
        for (; pages < 8 && !Results_AtChoices(); pages++)
            for (int i = 0; i < 40; i++, frame++) {
                plat_host_key(0x1c, i < 3);
                Timer_Sample((uint32_t)((uint64_t)frame * 1000 / 60));
                Race_Frame();
            }
        printf("the choices after %d pages (a new record %d)\n", pages, gm8(0x817410));
        CHECK(Results_AtChoices());
        static const int keys[] = {0xc8, 0x1c};
        for (int k = 0; k < 2; k++)
            for (int i = 0; i < 40; i++, frame++) {
                plat_host_key(keys[k], i < 3);
                Timer_Sample((uint32_t)((uint64_t)frame * 1000 / 60));
                Race_Frame();
            }
        for (int i = 0; i < 200 && Game_Mode() == 3; i++, frame++) {
            Timer_Sample((uint32_t)((uint64_t)frame * 1000 / 60));
            Race_Frame();
        }
        printf("after the results' quit: mode %u\n", Game_Mode());
        CHECK(Game_Mode() == 0);
        /* the particle types the race defined (30 of 32), driven by hand: exhaust smoke fades out
           (0x1e by 3 a step, dying at 0) and is drawn; gravel thrown down bounces to a stop on its ground (kept 0xb333, under 0x1999 it rests) */
        int defined = 0;
        for (int i = 0; i < 32; i++) defined += PType_Get(i) && (PType_Get(i)->flags & 1);
        Particles_KillAll();
        int32_t from[3] = {0, 0x1000, 0}, down[3] = {0x1000, -0x2000, 0x1000}, still[3] = {0};
        Particle_Emit(0, from, still, 0, NULL, NULL, 0, NULL, 1);
        Particle_Emit(0x1d, from, down, 0, NULL, NULL, 0, NULL, 1);
        Particle *smoke = NULL, *stone = NULL;
        for (int i = 0; i < 400; i++) {
            Particle *p = Particle_Get(i);
            if (p && p->alive && p->type == PType_Get(0)) smoke = p;
            if (p && p->alive && p->type == PType_Get(0x1d)) stone = p;
        }
        CHECK(smoke && stone);
        if (!smoke || !stone) return 1;
        int smoke_steps = 0, drawn = 0;
        for (int i = 0; i < 12; i++) {
            Particles_Step(NULL);
            smoke_steps += smoke->alive;
        }
        Particles_Interp(0x8000);
        int before = Sprite3d_Count();
        Particles_Draw(NULL, 0);
        drawn = Sprite3d_Count() - before;
        Sprite3d_Init();   /* the queue emptied */
        printf("particles: %d types, %d alive; the smoke lived %d steps, the stone at rest %d at %.3f, %d drawn\n",
               defined, Particles_Alive(), smoke_steps, stone->rest, stone->pos[1] / 65536.0, drawn);
        CHECK(defined == 30 && PType_Get(0)->tex && smoke_steps == 9 && stone->alive && stone->rest &&
              stone->pos[1] == 0 && drawn == 1);
        Particles_KillAll();
    }
    {
        /* the lights: the sun, then the headlights' two, their attenuation from the daylight (the
           test's second stage load, without the front end's Play_End, adds three more) */
        int n = Dlight_Count() - 3;
        D3DLIGHT7 *sun = Dlight_Get(n), *h1 = Dlight_Get(n + 1), *h2 = Dlight_Get(n + 2);
        CHECK(sun && h1 && h2);
        if (!sun || !h1 || !h2) return 1;
        Lamps_Lights(0);
        uint8_t hi[4];
        Light_GetHigh(hi);
        printf("lights: %d, sun %u (%.3f %.3f %.3f), headlights %u %u, high %u %u %u, attenuation %.3f, on %d\n",
               Dlight_Count(), sun->type, sun->direction.x, sun->direction.y, sun->direction.z, h1->type, h2->type,
               hi[0], hi[1], hi[2], h1->attenuation0, CI32(Car_Get(0), 0xb58));
        CHECK(n % 3 == 0 && h1->attenuation0 == 1025 && sun->type == D3DLIGHT_DIRECTIONAL && sun->direction.y < -0.6f &&
              h1->type == D3DLIGHT_POINT && h1->range == 60 && h1->attenuation0 == h2->attenuation0);
    }
    {
        /* the skid marks: the most segments car 0's wheels had laid at once in the race (each stamped
           with its frame), all dead after the finish (0x465530 at state 9) */
        int laid = 0;
        for (uint32_t i = 0; i < 800; i++) laid += gm8(0x548220 + i) != 0;
        printf("shadows: %d frames drawn, %d points, the wheels' centres at most %.2f from the car (the first 1000)\n", shadow_frames,
               shadow_points, shadow_far);
        CHECK(shadow_frames > 0 && shadow_points > 4 && shadow_far > 0.5 && shadow_far < 2.5);
        printf("skids: %d segments laid at most, %d after, span %d, frame %u\n", skids_laid, laid, gms32(0x51bca8), gm32(0x588750));
        CHECK(skids_laid > 0 && skids_stamped == skids_laid && laid == 0 && gms32(0x51bca8) == 2);
    }
    printf("race_test: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
