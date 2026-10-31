/* Fixed-point math (see fixmath.h).

   Math_InitTables uses the original's constants: the angle step 2 pi / 4096, the arcsine step 1 / 4095,
   the arctangent step 1 / 511 and radians-in-16.16 to angle units 1 / (32 pi) (doubles at 0x511d08,
   0x511d00, 0x511cf8, 0x511380; tests/math_test.c compares them with the exe). The original computes on
   the x87: products rounded to double (the runtime's 53-bit precision), fsin / fptan / fpatan results
   in extended precision, fistp rounding to nearest. The port's libm values can differ from those only
   in the last bits, which matters only for a product within that of a half; the test checks a sample
   against values computed independently. */
#include "fixmath.h"
#include <math.h>
#include <string.h>

int32_t g_sin[4096], g_tan[4096];
uint16_t g_sqrt[4096], g_asin[4096], g_atan[512];

/* fistp to a 64-bit integer, round to nearest even; the caller keeps the low bits */
static int64_t fistp(double v) { return (int64_t)nearbyint(v); }

void Math_InitTables(void)
{
    const double step = 2.0 * M_PI / 4096.0, to_angle = 1.0 / (32.0 * M_PI);
    for (int i = 0; i < 4096; i++) {
        double a = (double)i * step;
        g_sin[i] = (int32_t)fistp(sin(a) * 65536.0);
        double t = tan(a) * 65536.0;
        /* a result past int64 is the "integer indefinite", whose low half is 0 */
        g_tan[i] = fabs(t) < 9.2e18 ? (int32_t)(uint32_t)(uint64_t)fistp(t) : 0;
    }
    g_tan[0x400] = g_tan[0xc00] = 0x7fffffff;
    for (int i = 0; i < 4096; i++) g_sqrt[i] = (uint16_t)fistp(sqrt((double)(8 + 16 * i) * (1.0 / 65536.0)) * 65536.0);
    for (int i = 0; i < 4096; i++) {
        int32_t r = (int32_t)fistp(asin((double)i * (1.0 / 4095.0)) * 65536.0);
        g_asin[i] = (uint16_t)fistp((double)r * to_angle);
    }
    for (int i = 0; i < 512; i++) {
        int32_t r = (int32_t)fistp(atan2((double)i * (1.0 / 511.0), 1.0) * 65536.0);
        g_atan[i] = (uint16_t)fistp((double)r * to_angle);
    }
}

void Mat_Identity(FxMat *m)
{
    memset(m, 0, sizeof *m);
    m->m[0] = m->m[5] = m->m[10] = m->m[15] = 0x10000;
}

/* A turn about z: cosine, sine / -sine, cosine in the upper left. */
void Mat_RotZ(FxMat *m, uint32_t a)
{
    Mat_Identity(m);
    int32_t s = g_sin[a & 0xfff], c = g_sin[(a + 0x400) & 0xfff];
    m->m[0] = c;
    m->m[1] = s;
    m->m[4] = -s;
    m->m[5] = c;
}

/* v M with each product shifted down by 16 (an arithmetic shift of the 64-bit product). */
void Vec_Transform(FxVec *o, const FxVec *v, const FxMat *m)
{
    const int32_t *a = m->m;
    FxVec r;
    r.x = a[12] + fx_mul(a[8], v->z) + fx_mul(a[4], v->y) + fx_mul(a[0], v->x);
    r.y = a[13] + fx_mul(a[9], v->z) + fx_mul(a[5], v->y) + fx_mul(a[1], v->x);
    r.z = a[14] + fx_mul(a[10], v->z) + fx_mul(a[6], v->y) + fx_mul(a[2], v->x);
    *o = r;
}

void Mat_ToFloat(float out[16], const FxMat *m)
{
    for (int i = 0; i < 16; i++) out[i] = (float)((double)m->m[i] * (1.0 / 65536.0));
}

/* Moved by -c, turned, moved back by c; v holds the turned point (before the move back). */
void Vec_RotateAbout(FxVec *out, FxVec *v, const FxVec *c, const FxMat *m)
{
    FxMat t;
    Mat_Identity(&t);
    t.m[12] = -c->x;
    t.m[13] = -c->y;
    Vec_Transform(out, v, &t);
    *v = *out;
    Vec_Transform(out, v, m);
    *v = *out;
    t.m[12] = c->x;
    t.m[13] = c->y;
    Vec_Transform(out, v, &t);
}
