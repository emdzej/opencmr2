/* The car-to-car contacts (Cars_Collide 0x48a1f0, in Sim_Collide 0x4877a0 when the mode lets the cars
   collide, Gs_CarsCollide). Each pair sharing a stage cell and within reach of each other has its flat
   boxes tested (0x488640 through 0x48a5f0, each car taking half the push); a contact lists the two as
   touching (0x5913dc, 0x5913f8) and exchanges an impulse along the contact normal (0x48ae90): a common
   velocity by the two masses, a knock above 0.6, the damage, the sparks. Any later push of a car (the
   walls, the scenery, the other contacts) moves the cars it touches with it (Cars_PushGroup). */
#pragma once
#include "sim/car.h"
#include <stdbool.h>

void Cars_Collide(const int16_t *list, int n);   /* 0x48a1f0 */
/* 0x487130: the last pair of cars whose boxes were tested (in any step since) touched (0x591390) */
bool Cars_Touching(void);
/* 0x48c870: car n moved by v, and through the contact lists every car touching it, once each (not
   n and not other, -1 none); with corners the touching cars' flat boxes too */
void Cars_PushGroup(int n, int other, const int32_t v[3], bool corners);
