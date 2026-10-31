/* Car_GroundContact 0x4348c0 (named Car_Drive before it was read), ported from the disassembly:
   the ground's support. Each touching corner gets the normal force (+0x5e8, unless all four
   wheels' corners carry the car) of its share of the mass plus 30 against gravity (0x53cad0), and
   its friction terms (+0xa0c-+0xa5c) from the per-corner coefficients; the total force (+0x6a8)
   loses its part along the ground normal, less the tilt factor (0x53c9d4). */
#include "sim/ground_contact.h"
#include "fixmath.h"
#include "game/gstate.h"
#include <math.h>

static int32_t dot3(const int32_t a[3], const int32_t b[3]) { return fx_mul(a[0], b[0]) + fx_mul(a[1], b[1]) + fx_mul(a[2], b[2]); }

static int32_t share(int32_t m, int n) { return n == 0 ? 0 : n == 4 ? m / 4 : n == 2 ? m / 2 : n == 1 ? m : m / n; }

void Car_GroundContact(Car *c)
{
    int n = CI8(c, 0xb34);
    CW32(c, 0x8b0, share(CI32(c, 0x75c), n));
    int32_t nrm[3] = {CI32(c, 0x48c), CI32(c, 0x490), CI32(c, 0x494)};
    int32_t g[3] = {gms32(0x53cad0), gms32(0x53cad4), gms32(0x53cad8)};
    int32_t q = -fx_mul(CI32(c, 0x8b0) + 0x1e0000, dot3(g, nrm));   /* local -0x4 */
    int32_t f = fx_mul(q, 0x10000), F[3];
    for (int k = 0; k < 3; k++) F[k] = fx_mul(nrm[k], f);
    int32_t k1 = fx_mul(CI32(c, 0x778), fx_mul(0x1cccc, 0x10000));
    if (k1 > 0x1cccc) k1 = 0x1cccc;
    k1 += 0x10000;
    for (uint32_t i = 0; i < 8; i++) {
        uint32_t a = 0xa5c + 4 * i;
        bool touch = !c->s[0xb2c + i];
        if (!touch) {
            if (i < 4 && CI32(c, a + 0x150)) touch = true;   /* the wheel is on the ground */
            else {
                CW32(c, a - 0x30, 0);
                CW32(c, a - 0x50, 0);
                if (i < 4) {
                    CW32(c, a - 0x10, 0);
                    CW32(c, a, 0);
                }
                continue;
            }
        }
        if (n != 4)
            for (uint32_t k = 0; k < 3; k++) CW32(c, 0x5e8 + 12 * i + 4 * k, CI32(c, 0x5e8 + 12 * i + 4 * k) + F[k]);
        if (i < 4 && CI32(c, 0xb74)) {
            int32_t r = fx_mul(CI32(c, 0x4a8 + 12 * i), 0x8000);
            q = fx_mul(CI32(c, 0x8b8 + 4 * i) + 0x1e0000, r);
            CW32(c, a, fx_mul(q, CI32(c, 0x1a4 + 12 * i) + CI32(c, 0x88 + 0x24 * i)));
            CW32(c, a - 0x10, fx_mul(q, CI32(c, 0x1a4 + 12 * i) + CI32(c, 0x80 + 0x24 * i)));
        } else {
            /* q here is the last value computed: the gravity share above, or a wheel's from before */
            CW32(c, a - 0x50, fx_mul(q, CI32(c, 0x88 + 0x24 * i)));
            CW32(c, a - 0x30, fx_mul(q, CI32(c, 0x80 + 0x24 * i)));
            CW32(c, a - 0x50, fx_mul(CI32(c, a - 0x50), k1));
            CW32(c, a - 0x30, fx_mul(CI32(c, a - 0x30), k1));
        }
    }
    if (n <= 0 && !c->s[0xb28]) return;
    int32_t t[3] = {CI32(c, 0x6a8), CI32(c, 0x6ac), CI32(c, 0x6b0)}, p = dot3(nrm, t), r[3];
    for (int k = 0; k < 3; k++) r[k] = t[k] - fx_mul(nrm[k], p);
    for (int k = 0; k < 3; k++) r[k] = fx_mul(r[k], 0x10000 - gms32(0x53c9d4));
    /* the per-contact share of it into 0x53c9d8 (the count through a double, 65536.0 at 0x5112e0),
       then scaled by max(0, 1 / 0.24 x -0.7): always 0 */
    int cnt;
    if (!CI32(c, 0xb74)) cnt = n > 0 ? n : c->s[0xb28];
    else cnt = c->s[0xb28] ? c->s[0xb28] : n;
    int32_t n16 = (int32_t)(int64_t)llrint((double)cnt * 65536.0), iv = n16 ? (int32_t)(0x100000000LL / n16) : 0;
    int32_t z = fx_mul((int32_t)(((int64_t)0x10000 << 16) / 0x3d70), (int32_t)0xffff4ccd);
    if (z < 0) z = 0;
    for (uint32_t k = 0; k < 3; k++) gw32(0x53c9d8 + 4 * k, (uint32_t)fx_mul(fx_mul(r[k], iv), z));
    if (n > 0)
        for (uint32_t k = 0; k < 3; k++) CW32(c, 0x6a8 + 4 * k, r[k]);
}
