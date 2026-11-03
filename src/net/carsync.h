/* The network game's cars (0x424dc0-0x427580; net/carsync.c, docs/re/network.md#cars): each local car
   packed into 30 bytes (0x539388) and sent every step (type 0xb), and each other player's car moved by
   a predictor record (0x5393d8, 0xec bytes a car slot) from the last state taken, between states. */
#pragma once
#include "sim/car.h"
#include "sim/sim.h"

void Net_CarsReset(void);            /* 0x424ed0 (the line, state 5): the frame estimates, the steps' table */
void Net_CarStart(Car *c);           /* 0x424dc0 (Car_Place with mode bit 19): the car's predictor */
void Net_SendCars(const CarList *l); /* 0x4258e0 (Sim_Step's end): the local cars (+0xc1c clear) sent */
void Sim_SimpleCars(const CarList *l);   /* 0x425950 (Sim_Step): the states taken, the others moved */
void Net_CarsAfter(const CarList *l);    /* 0x426fc0 (Sim_Step, after the collisions): back into the records */
void Net_SetRate(int bytes, int bits, int players);   /* 0x427580: 0x5393d4, 40 or 41 */
