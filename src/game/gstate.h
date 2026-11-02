/* The game-state accessors (0x405c00-0x409000): one-line reads of bit fields in the mode word
   0x52afb4, the options 0x52afc0, the word 0x52afb8 and the stage selection 0x52f2ac. Names follow
   what is known of each; the numbers are the original's. */
#pragma once
#include "game/mem.h"

static inline uint32_t Gs_Mode(void) { return gm32(0x52afb4) >> 3 & 0x7f; }         /* 0x405d80 */
static inline uint32_t Gs_Mode2(void) { return gm32(0x52afb4) >> 10 & 7; }          /* 0x405d90 */
static inline uint32_t Gs_ModeBit17(void) { return gm32(0x52afb4) >> 0x11 & 1; }    /* 0x405da0 */
static inline uint32_t Gs_ModeBit19(void) { return gm32(0x52afb4) >> 0x13 & 1; }    /* 0x405e00 */
static inline uint32_t Gs_Bit30(void) { return gm32(0x52afb8) >> 0x1e & 1; }        /* 0x406310 */
static inline uint8_t Gs_Byte52ea52(void) { return gm8(0x52ea52); }                 /* 0x406320 */
/* 0x407270: a rally, rally 8, stage index 11: the head-to-head super special, run at night */
static inline bool Gs_SuperSpecial(void) { return (gm32(0x52afb4) >> 3 & 0x7f) == 0 && (gm32(0x52f2ac) & 0x1f) == 8 && (gm32(0x52f2ac) & 0x3e0) == 0x160; }
/* 0x4063f0: option n, bit n of the options word's third byte */
static inline bool Gs_Option(uint32_t n) { return (gm8(0x52afc2) & (1u << (n & 31))) != 0; }
static inline uint32_t Gs_SelBits14(void) { return gm32(0x52f2ac) >> 0xe & 3; }     /* 0x4074f0 */
static inline uint32_t Gs_SelBit24(void) { return gm32(0x52f2ac) >> 0x18 & 1; }     /* 0x407e50 */
static inline uint32_t Gs_SelBit25(void) { return gm32(0x52f2ac) >> 0x19 & 1; }     /* 0x407e60 */
static inline uint32_t Gs_SelBit26(void) { return gm32(0x52f2ac) >> 0x1a & 1; }     /* 0x407e70 */
static inline uint32_t Gs_SelBit27(void) { return gm32(0x52f2ac) >> 0x1b & 1; }     /* 0x407e90 */
static inline uint32_t Gs_SelBit22(void) { return gm32(0x52f2ac) >> 0x16 & 1; }     /* 0x407e30 */
static inline uint32_t Gs_SelBit31(void) { return gm32(0x52f2ac) >> 0x1f; }           /* 0x407e80 */
static inline uint32_t Gs_SelBit28(void) { return gm32(0x52f2ac) >> 0x1c & 1; }     /* 0x407ea0 */
static inline uint8_t Gs_Byte537ef6(void) { return gm8(0x537ef6); }                 /* 0x41b370 */
static inline uint32_t Gs_ClassB7c(int c) { return gm32(0x516c40 + 4 * (uint32_t)c); }   /* 0x40ee80 */
static inline uint32_t Gs_ClassB80(int c) { return gm32(0x516c08 + 4 * (uint32_t)c); }   /* 0x40ee70 */
/* 0x407630: a car's set-up, 7 bytes at 0x52f0e0 */
static inline uint32_t Gs_Setup(int n) { return 0x52f0e0 + 7 * (uint32_t)n; }

/* 0x46bd50: the cars collide with each other: not in modes 8-10; in 11 and 12, and in every mode
   above 3; in modes 0-3 when the selection's bit 27 is set */
static inline bool Gs_CarsCollide(void)
{
    uint32_t m = Gs_Mode();
    if (m == 8 || m == 9 || m == 10) return false;
    if (m == 11 || m == 12 || m > 3) return true;
    return Gs_SelBit27() != 0;
}

