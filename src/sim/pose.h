/* The cars' drawn poses: each step the body (car +0x00), the drawn body on its suspension (+0x40, the
   +0x754 matrix, Car_BodyPose 0x432c20), the wheels and their turn are captured (0x42b4a0, 0x42af50);
   each frame Car_PoseInterp 0x42bf70 blends the last two by the step fraction into the models. */
#pragma once
#include "sim/sim.h"

void Car_BodyPose(Car *c);            /* 0x432c20 */
void Pose_Spin(const CarList *l);     /* 0x42b4a0 */
void Pose_Capture(const CarList *l);  /* 0x42af50 */
void Car_PoseInterp(void);            /* 0x42bf70 */
void Pose_Init(int n);                /* 0x42bcd0 */
void Pose_Snap(int car);              /* 0x42c870 */
