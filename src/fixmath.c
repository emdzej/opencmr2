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

/* a b, each of the four products of a term shifted down by 16 before the sum */
void Mat_Mul(FxMat *o, const FxMat *a, const FxMat *b)
{
    FxMat r;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            int32_t s = 0;
            for (int k = 0; k < 4; k++) s += fx_mul(a->m[i * 4 + k], b->m[k * 4 + j]);
            r.m[i * 4 + j] = s;
        }
    *o = r;
}

/* The 3x3 part by its cofactors over the determinant (as 2^32 / det, truncated), the translation as
   -t times that; a determinant of 0 gives the identity. */
void Mat_Inverse(FxMat *o, const FxMat *a)
{
    const int32_t *m = a->m;
    int32_t c0 = fx_mul(m[5], m[10]) - fx_mul(m[9], m[6]);
    int32_t c1 = fx_mul(m[9], m[2]) - fx_mul(m[1], m[10]);
    int32_t c2 = fx_mul(m[1], m[6]) - fx_mul(m[5], m[2]);
    int32_t det = fx_mul(c2, m[8]) + fx_mul(c1, m[4]) + fx_mul(c0, m[0]);
    if (!det) {
        Mat_Identity(o);
        return;
    }
    int32_t inv = (int32_t)(0x100000000LL / det);
    int32_t *r = o->m;
    FxMat src = *a;   /* o may be a */
    m = src.m;
    r[3] = r[7] = r[11] = 0;
    r[0] = fx_mul(inv, c0);
    r[1] = fx_mul(inv, c1);
    r[2] = fx_mul(inv, c2);
    r[4] = fx_mul(inv, fx_mul(m[8], m[6]) - fx_mul(m[4], m[10]));
    r[5] = fx_mul(inv, fx_mul(m[0], m[10]) - fx_mul(m[8], m[2]));
    r[6] = fx_mul(inv, fx_mul(m[4], m[2]) - fx_mul(m[0], m[6]));
    r[8] = fx_mul(inv, fx_mul(m[4], m[9]) - fx_mul(m[8], m[5]));
    r[9] = fx_mul(inv, fx_mul(m[8], m[1]) - fx_mul(m[0], m[9]));
    r[10] = fx_mul(inv, fx_mul(m[0], m[5]) - fx_mul(m[4], m[1]));
    int32_t tx = -m[12], ty = -m[13], tz = -m[14];
    r[12] = fx_mul(tz, r[8]) + fx_mul(ty, r[4]) + fx_mul(tx, r[0]);
    r[13] = fx_mul(tz, r[9]) + fx_mul(ty, r[5]) + fx_mul(tx, r[1]);
    r[14] = fx_mul(tz, r[10]) + fx_mul(ty, r[6]) + fx_mul(tx, r[2]);
    r[15] = 0x10000;
}

/* The square root the game writes out inline (Section_BuildVisible 0x4b7de0 and others): the value's
   top bit, an even shift that brings it to 10 or 11 bits, the table, the shift halved back. In 16.16:
   sqrt of a 16.16 value. */
uint32_t Fx_Sqrt(uint32_t x)
{
    if (!x) return 0;
    uint32_t t = x;
    int c = 0;
    if (t > 0xffff) t >>= 16, c = 16;
    if (t > 0xff) t >>= 8, c += 8;
    if (t > 0xf) t >>= 4, c += 4;
    if (t > 3) t >>= 2, c += 2;
    if (t > 1) c += 1;
    int8_t b = (int8_t)(c - 15);
    if (b & 1) b = (int8_t)(c - 14);
    int8_t s = (int8_t)(b + 4);
    uint32_t i = s < 0 ? x << -s : x >> s;
    uint32_t r = (i & 0xffff0000u) | g_sqrt[i & 0xfff];
    int8_t h = (int8_t)(b >> 1);
    return h < 0 ? r >> -h : r << h;
}
