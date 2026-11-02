/* Driving a car (docs/re/architecture.md#timing): the original's controls reader 0x47b0e0, the start line (0x47b640) and race (0x47b000) states, and the
   step clock of Race_Update 0x41c5a0 (40 ms steps at 25 Hz, from the frame count). */
#pragma once
#include "platform.h"
#include "sim/car.h"

/* The keyboard players' action bits (1 << action, Ctl_Mask 0x40c2a0 for their devices): the actions
   of 0x47b0e0 and 0x404e10 in the controller pages' order. */
enum { ACT_LEFT = 1, ACT_RIGHT = 2, ACT_THROTTLE = 4, ACT_BRAKE = 8, ACT_HANDBRAKE = 0x10, ACT_GEAR_UP = 0x20,
       ACT_GEAR_DOWN = 0x40, ACT_CAMERA = 0x80, ACT_REAR = 0x100, ACT_PAUSE = 0x400 };

/* 0x47b0e0: a player's car's controls (+0x1d0 left, +0x1d1 right, +0x1d2 accelerate, +0x1d3 brake,
   0-63; +0x1d4 the gear asked; +0x1d8 the handbrake) from input record rec by the player's action bits
   (0x47bca0), a game controller's axes when analogue (+0xb88, +0xb8c, +0xb90 say which); the camera and
   rear view keys; gears only once the race runs (+0xb9c). */
void Car_ControlsFromInput(Car *c, int rec);
/* The race state's reader 0x47b000: controls cleared, read, and the automatic box enabled (+0xb9c). */
void Car_RaceControls(Car *c, int rec);
void Car_RaceControlsCpu(Car *c, int n);       /* 0x47b000 without a pad */
void Car_StartLineControlsCpu(Car *c);         /* 0x47b640 without a pad */
void Car_Launch(Car *c);                       /* 0x43f570 */
/* Car_StartLineControls 0x47b640: controls cleared and read, the handbrake held, the automatic box off. */
void Car_StartLineControls(Car *c, int rec);
/* After the finish: 0x47b870 (no input) and 0x47b7b0 (the player's, then overridden): braking to a stop. */
void Car_FinishedControls(Car *c);
void Car_FinishedPlayerControls(Car *c, int rec);
/* 40 ms steps due for app frame n at the given frame rate. */
int Drive_StepsDue(uint32_t frame, double hz);
/* One simulation step of the cars in list (Sim_StepCars and the post-car pass). */
void Drive_Step(const int16_t *list, int n);
/* 0x42b6f0, 0x42b700: the race's car list (0x53b500, count 0x53a3a0); the port keeps the last list
   Drive_Step ran, until the race set-up is ported. */
const int16_t *Sim_Cars(int *n);
/* The stand-in for Car_PoseInterp 0x42bf70: the main model at the car's rows and position. */
void Car_PoseSimple(Car *c);
