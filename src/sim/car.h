/* The car state (docs/formats/cars.md#handling): 0xc24 bytes per car, kept as the original's block and
   addressed by its offsets (CI32 and friends), since the simulation (0x428000-0x445000) is written
   against them. Pointers the original keeps inside the block (the models at +0x71c/+0x720, the wheels
   at +0x738-+0x744, the parts at +0x748/+0x74c, the self pointer at +0x750) are kept beside it. */
#pragma once
#include "world/obj.h"
#include <stdbool.h>
#include <stdint.h>

enum { CAR_SIZE = 0xc24, CAR_MAX = 8 };

typedef struct {
    uint8_t s[CAR_SIZE];
    Obj *main, *second;   /* +0x71c, +0x720 */
    Obj *wheel[4];        /* +0x738-+0x744: objects of type 1-4 under the main model */
    Obj *part748, *part74c;   /* types 0xf and 0xe under the second model */
} Car;

static inline int32_t CI32(const Car *c, uint32_t o) { const uint8_t *p = c->s + o; return (int32_t)((uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24); }
static inline void CW32(Car *c, uint32_t o, int32_t v) { uint8_t *p = c->s + o; uint32_t u = (uint32_t)v; p[0] = (uint8_t)u, p[1] = (uint8_t)(u >> 8), p[2] = (uint8_t)(u >> 16), p[3] = (uint8_t)(u >> 24); }
static inline int8_t CI8(const Car *c, uint32_t o) { return (int8_t)c->s[o]; }
static inline void CW8(Car *c, uint32_t o, uint8_t v) { c->s[o] = v; }
static inline void CW16(Car *c, uint32_t o, uint16_t v) { c->s[o] = (uint8_t)v, c->s[o + 1] = (uint8_t)(v >> 8); }

Car *Car_Get(int n);   /* 0x42b5f0 */
uint8_t *Damage_Get(int n);   /* 0x469680: the car's 0x4d0-byte damage record */
static inline int32_t DI32(const uint8_t *d, uint32_t o) { return (int32_t)((uint32_t)d[o] | d[o + 1] << 8 | d[o + 2] << 16 | (uint32_t)d[o + 3] << 24); }
extern Car *g_car;     /* DAT_0053cc18: the car being worked on */

/* The car table at 0x542630 (0x24 bytes per car): the port keeps its model objects here. */
typedef struct { Obj *second, *main; char kind; } CarSlot;   /* +0x04, +0x08, +0x20 */
CarSlot *Car_Slot(int n);   /* 0x456be0 */

Obj *Obj_FindType(Obj *o, uint32_t type);   /* 0x4ad890: depth first, siblings after children */
void Obj_Reparent(Obj *o, Obj *parent);     /* 0x4ac7a0 */

/* Car_Init 0x43c7f0: the state of car n with model obj at pos, handling class cls; with keep set the
   models are not attached again. */
void Car_Init(Car *c, Obj *obj, int cls, const int32_t pos[3], int n, bool keep);
void Car_BoxCorners(Car *c);   /* 0x43eef0 */
void Car_Settle(Car *c);       /* 0x43e680 */
void Damage_Reset(Car *c);     /* 0x469690 */
/* Car_Place 0x457e50: the start pose on both models, then Car_Init and Car_Settle. */
void Car_Place(Obj *main, Obj *second, int n, int cls, const uint16_t ang[3], const int32_t pos[3]);
