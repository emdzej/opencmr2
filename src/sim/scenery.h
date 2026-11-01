/* The cars against the scenery objects (TEMP.OBJ's trees, posts, rocks, fences): each mesh's
   collision volumes (the 0x5c records from its +0x10c, their count at +0x110) sorted at stage load
   into per-cell lists of fixed and movable ones (0x471dd0); each step Car_Scenery 0x4878a0 tests the
   car's four cells' volumes. An upright cylinder (0x489b20, 0x489750) or a box (0x487f60, 0x488640)
   pushes the car's flat box out and kicks it (0x48be20); a low object (flag 0x4000) bumps the
   wheels. Not yet: knocking movable objects down (0x46fe70) and the damage. */
#pragma once
#include "sim/car.h"
#include <stdbool.h>

void Scenery_CollInit(void);   /* 0x471dd0 (Stage_Load's end) */
void Scenery_CollFree(void);   /* 0x472720 */
int Car_Scenery(Car *c);       /* 0x4878a0 */
/* The port, for tests: the fixed volumes (and how many are boxes), the i-th one's centre, reach and record flags */
int Scenery_CollCount(int *boxes);
/* the port, for tests: the car against a made-up box volume (7.9 fixed point) centred at pos */
bool Scenery_CollTestBox(Car *c, const int32_t pos[3], const int16_t box[6], uint32_t flags);
bool Scenery_CollAt(int i, int32_t pos[3], int32_t *r, bool *box, uint32_t *flags);
