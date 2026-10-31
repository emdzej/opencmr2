/* The game's fixed-point math (16.16; angles in 4,096 units per turn): the tables Math_InitTables
   0x4b7b20 fills at start, and the matrix and vector helpers of 0x4ba000-0x4bb000. Matrices are 16 int32,
   row vectors (v' = v M, translation in the fourth row), as the game's. */
#pragma once
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

void Mat_Identity(FxMat *m);                                       /* 0x4ba2b0 */
void Mat_RotZ(FxMat *m, uint32_t angle);                           /* 0x4ba320 */
void Vec_Transform(FxVec *out, const FxVec *v, const FxMat *m);    /* 0x4baa40 */
void Mat_ToFloat(float out[16], const FxMat *m);                   /* 0x4ba090 */
/* 0x4ba3b0: v (in place, and out) turned by m about the point c (only its x and y are used). */
void Vec_RotateAbout(FxVec *out, FxVec *v, const FxVec *c, const FxMat *m);
