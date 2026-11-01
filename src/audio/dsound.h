/* The port's DirectSound (docs/re/architecture.md#sound-and-music): the buffers the original creates
   (IDirectSound::CreateSoundBuffer, Lock/Unlock, Play, Stop, GetStatus, SetVolume, SetPan,
   SetFrequency), as PCM kept in memory, and the mixer that renders them at 44,100 Hz stereo for the
   gasm audio push. Volumes are DirectSound's: hundredths of a decibel, 0 full, -10,000 silent; pans
   -10,000 left to 10,000 right as the other side's attenuation. 3D buffers are not emulated (the game
   runs with 3D sound off). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct Dsb Dsb;
/* CreateSoundBuffer: a buffer of size bytes in rate Hz, bits 8 or 16, channels 1 or 2 */
Dsb *Dsb_Create(uint32_t rate, int bits, int channels, uint32_t size);
Dsb *Dsb_Duplicate(Dsb *b);                 /* DuplicateSoundBuffer: the same data, its own voice */
void Dsb_Release(Dsb *b);
void Dsb_Write(Dsb *b, uint32_t at, const void *data, uint32_t n);   /* Lock, copy, Unlock */
void Dsb_Play(Dsb *b, bool loop);
void Dsb_Stop(Dsb *b);
void Dsb_SetPosition(Dsb *b, uint32_t at);  /* SetCurrentPosition (bytes) */
bool Dsb_Playing(const Dsb *b);             /* GetStatus's DSBSTATUS_PLAYING */
void Dsb_SetVolume(Dsb *b, int32_t vol);    /* hundredths of a dB, -10,000..0 */
void Dsb_SetPan(Dsb *b, int32_t pan);
void Dsb_SetFrequency(Dsb *b, uint32_t hz);
/* The mixer: every playing buffer into stereo floats (frames at 44,100 Hz) */
void Dsb_Mix(float *stereo, unsigned frames);
