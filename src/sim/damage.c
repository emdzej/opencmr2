/* Car_Damage (see damage.h). */
#include "sim/damage.h"
#include "audio/crash.h"
#include "game/camera.h"
#include "game/gstate.h"
#include "game/mem.h"
#include "game/timer.h"
#include "fixmath.h"
#include "pending.h"

/* 0x588a88: per player the time of the last hit's sound, in the game state at its address (the third
   and fourth players' share 0x588a90-0x588a94 with the damage records' count, as in the original;
   0x4668d0, the damage records' reset, clears the first two) */
static uint32_t last_hit(int p) { return 0x588a88 + 4 * (uint32_t)p; }

void Car_Damage(Car *c, const int32_t pt[3], const int32_t n[3], uint32_t size, uint8_t kind, bool replay)
{
    (void)pt, (void)n, (void)size, (void)kind;
    if (!replay) {
        int32_t imp[3] = {CI32(c, 0x5c4), CI32(c, 0x5c8), CI32(c, 0x5cc)};
        int32_t v = (int32_t)Fx_Sqrt((uint32_t)fx_mul(imp[0], imp[0]) + (uint32_t)fx_mul(imp[1], imp[1]) + (uint32_t)fx_mul(imp[2], imp[2]));
        if (0x10000 < v) v = 0x10000;
        else if (v < 0) v = 0;
        int p = CI8(c, 0xb1a);
        if (p < (int)Gs_SelBits14()) {
            if (CI32(c, 0xb74) == 0) Crash_Roll(p, v, false, p);
            else if (10 < Timer_Centis() - gm32(last_hit(p))) {
                Crash_Hit(p, v, p);
                gw32(last_hit(p), Timer_Centis());
            }
        }
        Camera_Shake(c, imp, true);   /* 0x4241d0 */
    }
    PENDING_NOTE(0x466ef0, "Car_Damage: the damage records, the deformation and the panels");
}
