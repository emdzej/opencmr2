/* The replays (0x46bdc0-0x46d610, docs/formats/stage.md#replays-rpl-grp): input recordings. A replay
   is the image of the .rpl file: a 0x110-byte control block, per segment a snapshot of the car (kind 0:
   0x114c bytes, the damage record, the car's state block, its progress record and up to ten changes
   of the car's race state; kinds 1 and 2: 0x5c bytes), the segments' frames (kinds 0 and 1: 4-byte
   run-length coded controls, kind 2: 16-byte poses) and the frames used per segment. Recording packs
   the car's controls each step it is due; playing restores the snapshot and unpacks the frames into
   the controls before the cars are stepped, so the simulation runs the recorded drive again. Slots:
   eight made (0x588e80) and eight loaded (0x588ea0), stepped in that order. Kind 1's snapshot (the
   car reset through 0x42b800, 0x46c320 and 0x46c390) is ported; kind 2 (the network game's poses,
   0x46de20, 0x46e340, 0x46d510, 0x46d610) is not. The CPU runs on the disc are kind 0. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct Replay Replay;

Replay *Replay_New(int segs, int frames, int kind);   /* 0x46c5a0 (NULL when eight are made) */
Replay *Replay_Load(const char *path);                /* 0x46d2d0: a file, else the stage archive's member */
void Replay_FreeAll(void);                            /* 0x46c6d0 (the race's exit handlers) */
bool Replay_Start(Replay *r, int car);                /* 0x46c750: record car from the next step */
bool Replay_Stop(Replay *r);                          /* 0x46cc60: the recording ended */
/* 0x46cce0: play segment seg into car, after `pre` steps of the snapshot held (0: none) */
bool Replay_Play(Replay *r, int seg, int pre, int car);
bool Replay_End(Replay *r);                           /* 0x46d2a0: the playing ended */
bool Replay_Save(const Replay *r, const char *path);  /* 0x46d3f0 */
void Replay_RecordStep(void);                         /* 0x46c8e0 (Sim_Step) */
void Replay_PlayStep(void);                           /* 0x46d270 (Sim_Step) */
int32_t Replay_Word(const Replay *r, uint32_t o);    /* a word of the control block (the original's struct) */
bool Replay_Playing(const Replay *r);                 /* +0x04 */
bool Replay_Recording(const Replay *r);               /* +0x0c */
uint8_t Replay_State(const Replay *r);                /* +0x10c: the recorded race state now */
/* the port, for tests: the file image and its size; the frames used in segment seg */
const uint8_t *Replay_Image(const Replay *r, uint32_t *size);
int Replay_Frames(const Replay *r, int seg);

/* The race's replays (0x537f3c, one per player; 0x41f3d0 and 0x41f3f0 read them) */
Replay *Race_Replay(int p);
void Race_SetReplay(int p, Replay *r);
void Replay_LoadDemo(void);   /* 0x41c510 */
void Replay_Setup(void);   /* 0x41c260 (the line's set-up 0x424c50; Stage_Load in mode 3) */
/* For the differential tests: the made (0-7) and loaded (8-15) replays, and where in its image a
   replay's snapshots, frames and counts are */
Replay *Replay_Slot(int i);
void Replay_Layout(const Replay *r, uint32_t *snap, uint32_t *frames, uint32_t *counts);
void Replay_RecordStep2(void);   /* 0x46d510: the kind 2 recordings (the network game's poses) */
void Replay_PlayStep2(void);     /* 0x46d5e0: the kind 2 playings */
