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
void Cars_ShellsToCamera(const Obj *camera);   /* 0x429810 */
/* 0x42cae0: the wheels drawn narrow (and with mode 1 the hubs turned) */
bool Pose_Narrow(const Car *c, int mode);
/* The drawn pose's records: the car's 63 ints (0x42ca70: the main matrix, the ground normal at 56,
   the four heights at 59), its wheels' four 16-int matrices (0x42cab0) and its box's 8 corners (0x42cac0) */
const int32_t *Pose_Out(int n);
const int32_t *Pose_WheelM(int n);
const int32_t *Pose_Box(int n);
