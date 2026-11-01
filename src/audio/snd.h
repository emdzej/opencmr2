/* The original's sound layer: up to 200 sounds loaded from WAV members (Snd_Load 0x4a1f50,
   Snd_LoadNamed 0x4b76c0) and 32 voices playing them (0x4b7790-0x4b7ae0, the driver 0x4a22c0-
   0x4a27c0), on the port's DirectSound (dsound.h). A voice is named by a handle: its slot in the low 6
   bits, the sound id above (0x4b7a90). */
#pragma once
#include "bfl.h"
#include <stdbool.h>
#include <stdint.h>

void Snd_Init(void);                                   /* sound on (0x6e0eec) */
bool Snd_LoadNamed(const char *name, const BflSlot *archive);   /* 0x4b76c0 */
int Snd_Count(void);                                   /* 0x4b7940 */
void Snd_Truncate(int from);                           /* 0x4b7740: the sounds from `from` on freed */
/* 0x4b7790: sound id at vol (0..0x10000) and freq (Hz); loop from `start` (bytes, 0 the whole) when
   loop; 3d unused. The voice's handle, or -1. */
int32_t Voice_Play(int id, int32_t vol, uint32_t freq, uint32_t start, bool loop, bool three_d);
bool Voice_Playing(int32_t h);                         /* 0x4b78a0 */
void Voice_Stop(int32_t h);                            /* 0x4b78d0 */
void Voices_StopAll(void);                             /* 0x4b7910 */
void Snd_SetMaster(int32_t vol);                       /* 0x4b7950 */
void Voice_SetVolume(int32_t h, int32_t vol);          /* 0x4b79a0 */
void Voice_SetFrequency(int32_t h, uint32_t hz);       /* 0x4b79e0 */
int32_t Voice_Volume(int32_t h);                       /* 0x4b7a10 */
