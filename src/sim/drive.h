/* Driving a car from the keyboard (docs/re/architecture.md#timing): the digital path of the
   original's controls reader 0x47b0e0, the start line (0x47b640) and race (0x47b000) states, and the
   step clock of Race_Update 0x41c5a0 (40 ms steps at 25 Hz, from the frame count). */
#pragma once
#include "platform.h"
#include "sim/car.h"

/* Action bits as the input record holds them (the masks at 0x51f4b0-0x51f4bc): low and high halves
   of each pair. */
enum { ACT_RIGHT = 1, ACT_LEFT = 2, ACT_THROTTLE = 4, ACT_BRAKE = 8, ACT_HANDBRAKE = 16, ACT_GEAR_UP = 32, ACT_GEAR_DOWN = 64,
       ACT_CAMERA = 128 };

/* The port's stand-in for the key bindings (the front end's options, not ported): arrows steer,
   throttle and brake, space the handbrake, A and Z the gears, C the camera; the pad's d-pad, B, Y, A, R,
   L and X. */
void Drive_ReadInput(uint32_t *held, uint32_t *pressed);
/* 0x47b0e0's digital path for car c: the controls (+0x1d0-+0x1d8) from the held and pressed bits, the
   steering and pedals digital (+0xb88, +0xb8c, +0xb90 clear); gears only once the race runs (+0xb9c). */
void Car_ControlsFromInput(Car *c, uint32_t held, uint32_t pressed);
/* The race state's reader 0x47b000: controls cleared, read, and the automatic box enabled (+0xb9c). */
void Car_RaceControls(Car *c, uint32_t held, uint32_t pressed);
void Car_RaceControlsCpu(Car *c, int n);       /* 0x47b000 without a pad */
void Car_StartLineControlsCpu(Car *c);         /* 0x47b640 without a pad */
void Car_Launch(Car *c);                       /* 0x43f570 */
/* Car_StartLineControls 0x47b640: controls cleared and read, the handbrake held, the automatic box off. */
void Car_StartLineControls(Car *c, uint32_t held, uint32_t pressed);
/* After the finish: 0x47b870 (no input) and 0x47b7b0 (the player's, then overridden): braking to a stop. */
void Car_FinishedControls(Car *c);
void Car_FinishedPlayerControls(Car *c, uint32_t held, uint32_t pressed);
/* 40 ms steps due for app frame n at the given frame rate. */
int Drive_StepsDue(uint32_t frame, double hz);
/* One simulation step of the cars in list (Sim_StepCars and the post-car pass). */
void Drive_Step(const int16_t *list, int n);
/* 0x42b6f0, 0x42b700: the race's car list (0x53b500, count 0x53a3a0); the port keeps the last list
   Drive_Step ran, until the race set-up is ported. */
const int16_t *Sim_Cars(int *n);
/* The stand-in for Car_PoseInterp 0x42bf70: the main model at the car's rows and position. */
void Car_PoseSimple(Car *c);
