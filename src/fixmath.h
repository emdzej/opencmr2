/* The game's fixed-point math (16.16; angles in 4,096 units per turn): the tables Math_InitTables
   0x4b7b20 fills at start, and the matrix and vector helpers of 0x4ba000-0x4bb000. Matrices are 16 int32,
   row vectors (v' = v M, translation in the fourth row), as the game's. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct { int32_t m[16]; } FxMat;
typedef struct { int32_t x, y, z; } FxVec;

/* Math_InitTables 0x4b7b20: sine and tangent (4,096 int32 each, tan's two poles 0x7fffffff), square
   roots (4,096 u16: sqrt((8 + 16 i) / 65536) in 16.16), arcsine (4,096 u16: asin(i / 4095) as an angle)
   and arctangent (512 u16: atan(i / 511) as an angle), computed as the original does with doubles and
   round-to-nearest. */
void Math_InitTables(void);
extern int32_t g_sin[4096];      /* 0x6e2ef4; cosine is g_sin[(a + 0x400) & 0xfff] */
extern int32_t g_tan[4096];      /* 0x6e93f4 */
extern uint16_t g_sqrt[4096];    /* 0x6e0ef4 */
extern uint16_t g_asin[4096];    /* 0x6e6ef4 */
extern uint16_t g_atan[512];     /* 0x6e8ff4 */

static inline int32_t fx_mul(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 16); }

/* The x87 as the race runs it: at 64-bit precision (confirmed by tools/emu/sim_diff on the Fra01 demo:
   16.16 degrees 0x360a40 times the double 1/5760 at 0x511300, a hair above the true value, is 614.5
   and a hair; the original's FISTP makes it 615, where 53 or 24 bits would give the half and 614).
   X87_MulRound is FILD, FMUL by a double and FISTP: the product rounded to 64 bits, then to the
   nearest integer (ties to even). */
int64_t X87_MulRound(double a, double b);

void Mat_Identity(FxMat *m);                                       /* 0x4ba2b0 */
void Mat_RotZ(FxMat *m, uint32_t angle);                           /* 0x4ba320 */
void Vec_Transform(FxVec *out, const FxVec *v, const FxMat *m);    /* 0x4baa40 */
void Mat_ToFloat(float out[16], const FxMat *m);                   /* 0x4ba090 */
void Mat_Mul(FxMat *out, const FxMat *a, const FxMat *b);          /* 0x4b9f20: a b */
void Mat_Inverse(FxMat *out, const FxMat *m);                      /* 0x4ba440 */
uint32_t Fx_Sqrt(uint32_t x);   /* the inline table square root: sqrt of a 16.16 value, in 16.16 */
/* 0x498d80: atan2(y, x) in 16.16 degrees (the x87's atan, times the double at 0x5113f8, 180 / 3.14159,
   and 65536, rounded to nearest) */
int32_t Fx_Atan2Deg(int32_t y, int32_t x);
/* 0x498db0: an angle in 16.16 degrees brought into -180..180 by one turn */
static inline int32_t Fx_WrapDeg(int32_t a)
{
    if (a > 0xb3ffff) a -= 0x1680000;
    if (a < -0xb40000) a += 0x1680000;
    return a;
}
/* The inline turn of 16.16 degrees into a sine-table index: times 1/5760 (0x511300), rounded to
   nearest, the low 12 bits */
int Fx_DegIndex(int32_t a);
/* 0x4ba3b0: v (in place, and out) turned by m about the point c (only its x and y are used). */
void Vec_RotateAbout(FxVec *out, FxVec *v, const FxVec *c, const FxMat *m);
/* The game's inline normalisation: v times 2^32 / (table square root of v.v), zero for a zero length. */
static inline void Vec_Unit(int32_t v[3])
{
    uint32_t l = Fx_Sqrt((uint32_t)fx_mul(v[0], v[0]) + (uint32_t)fx_mul(v[1], v[1]) + (uint32_t)fx_mul(v[2], v[2]));
    if (!l) { v[0] = v[1] = v[2] = 0; return; }
    int32_t r = (int32_t)(0x100000000LL / (int32_t)l);
    for (int i = 0; i < 3; i++) v[i] = fx_mul(v[i], r);
}
/* 0x4bb0a0: a vector's length, the axes over 512 first (and the length times 512) when one is beyond
   100 units */
static inline int32_t Vec_Length(const int32_t v[3])
{
    bool far = false;
    for (int i = 0; i < 3; i++) far |= v[i] > 0x640000 || v[i] < -0x640000;
    if (!far) return (int32_t)Fx_Sqrt((uint32_t)fx_mul(v[0], v[0]) + (uint32_t)fx_mul(v[1], v[1]) + (uint32_t)fx_mul(v[2], v[2]));
    int32_t s[3] = {v[0] / 512, v[1] / 512, v[2] / 512};
    return (int32_t)(Fx_Sqrt((uint32_t)fx_mul(s[0], s[0]) + (uint32_t)fx_mul(s[1], s[1]) + (uint32_t)fx_mul(s[2], s[2])) << 9);
}
/* 0x4bae10: v made unit, as Vec_Unit but with the axes over 512 first when one is beyond 100 units */
static inline void Vec_Normalize(int32_t v[3])
{
    bool far = false;
    for (int i = 0; i < 3; i++) far |= v[i] > 0x640000 || v[i] < -0x640000;
    if (far)
        for (int i = 0; i < 3; i++) v[i] /= 512;
    Vec_Unit(v);
}
/* 0x4ac820's and 0x429f20's turn of a 3 x 3 (rows `stride` ints apart) by three u16 angles: [1]
   about row 1 (rows 0, 2 turned), [2] about row 2 (0, 1), [0] about row 0 (1, 2), each about the row
   as already turned, the turned rows made unit. */
void Rows_Turn3(int32_t *m, int stride, const uint16_t a[3]);
