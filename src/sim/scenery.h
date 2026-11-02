/* The cars against the scenery objects (TEMP.OBJ's trees, posts, rocks, fences): each mesh's
   collision volumes (the 0x5c records from its +0x10c, their count at +0x110) sorted at stage load
   into per-cell lists of fixed and movable ones (0x471dd0); each step Car_Scenery 0x4878a0 tests the
   car's four cells' volumes. An upright cylinder (0x489b20, 0x489750) or a box (0x487f60, 0x488640)
   pushes the car's flat box out and kicks it (0x48be20); a low object (flag 0x4000) bumps the
   wheels; a movable one is knocked down (sim/knock.c). Not yet: the damage. */
#pragma once
#include "sim/car.h"
#include <stdbool.h>

/* The box test's results (0x5914a8 the first box's push, 0x5914b8 the second's, 0x5915f4/0x5914d4
   the counts of the first's corners in the second and back, their corners and face axes) */
typedef struct {
    int32_t push[3], opush[3];
    int8_t ncar, nobj;
    uint8_t car_at[4], car_ax[4], obj_at[4], obj_ax[4];
} CollBoxes;
/* 0x488640 with two cars' flat boxes (0x48a5f0): b takes the share k of each push */
bool Scenery_BoxPair(Car *a, int32_t *ba, Car *b, int32_t *bb, int32_t k, CollBoxes *w);
void Car_ClampTurn(Car *c);    /* 0x48c750 */

void Scenery_CollInit(void);   /* 0x471dd0 (Stage_Load's end) */
void Scenery_CollFree(void);   /* 0x472720 */
int Car_Scenery(Car *c);       /* 0x4878a0 */
/* The port, for tests: the fixed volumes (and how many are boxes), the i-th one's centre, reach and record flags */
int Scenery_CollCount(int *boxes);
/* the tables in the original's layout at base (for tools/emu/sim_diff.c); 0 when they do not fit */
uint32_t Scenery_CollImage(uint8_t *buf, uint32_t cap, uint32_t base, uint32_t g[14]);
/* the port, for tests: the car against a made-up box volume (7.9 fixed point) centred at pos */
bool Scenery_CollTestBox(Car *c, const int32_t pos[3], const int16_t box[6], uint32_t flags);
bool Scenery_CollAt(int i, int32_t pos[3], int32_t *r, bool *box, uint32_t *flags);

/* The movable entries (one per collision volume of a movable object, 0x58c94c), for sim/knock.c */
struct Scenery;
int Scenery_Movables(void);                     /* 0x471bd0 */
struct Scenery *Scenery_Movable(int i);
void Scenery_MovedPos(int i, int32_t p[3]);     /* 0x471cc0 */
bool Scenery_UpFor(int i, int car);             /* 0x471d40 */
void Scenery_ViewFor(int player);               /* 0x471bf0: the movables as player sees them (split screen) */
void Scenery_SetUp(int i, int car, bool up);    /* 0x471d80 */
