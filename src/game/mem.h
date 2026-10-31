/* The original's data section as the game state (docs/re/architecture.md#game-state): 0x516000 to
   0x831c7c, its first 0x14000 bytes initialised from the exe at run time, the rest (BSS) zero. Ported
   code reads and writes the game's variables and tables there by their addresses, so the state the
   original keeps in globals (modes, options, championship tables, car set-ups) behaves as it does
   there without a table copied into the source. Only state is kept here; the port's own structures
   (objects, meshes, the device) are not. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { MEM_BASE = 0x516000, MEM_SIZE = 0x31bc7c };

bool Mem_Init(void);   /* after exe_init; again to reset the state */
void Mem_Free(void);
uint8_t *gm(uint32_t va);   /* the byte at va (an address outside the range is fatal) */
static inline uint8_t gm8(uint32_t va) { return *gm(va); }
static inline int8_t gms8(uint32_t va) { return (int8_t)*gm(va); }
static inline uint16_t gm16(uint32_t va) { const uint8_t *p = gm(va); return (uint16_t)(p[0] | p[1] << 8); }
static inline int16_t gms16(uint32_t va) { return (int16_t)gm16(va); }
static inline uint32_t gm32(uint32_t va) { const uint8_t *p = gm(va); return (uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static inline int32_t gms32(uint32_t va) { return (int32_t)gm32(va); }
static inline void gw8(uint32_t va, uint8_t v) { *gm(va) = v; }
static inline void gw16(uint32_t va, uint16_t v) { uint8_t *p = gm(va); p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8); }
static inline void gw32(uint32_t va, uint32_t v) { uint8_t *p = gm(va); p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24); }
