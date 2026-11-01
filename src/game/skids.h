/* The skid marks (0x464c60-0x465ea0, docs/re/world.md#skid-marks): per car (8) and wheel a ring of 200
   segments, each two ground points either side of the wheel with a colour and two alphas, laid while
   the wheel slides or on soft ground and drawn as quads between segments laid L frames apart. */
#pragma once

void Skids_Reset(void);       /* 0x465530 */
void Skids_Load(void);        /* 0x465600: the reset, the three textures, the span and the colours */
void Skids_Step(int car);     /* 0x464c60: each step, the rings of the wheels that laid a segment advance */
void Skids_Centres(int car);  /* 0x4657d0: the wheels' ground points from the car's shadow record */
void Skids_Add(int car);      /* 0x464cb0: each frame, the expiry and a new segment per wheel */
void Skids_Draw(int view);    /* 0x465780 */
