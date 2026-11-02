/* The port's binkw32.dll: the Bink 1 movies the game plays (FrontEnd\Videos\cm.bik and Intro.bik, the
   rallies' CountrySpecific\<region>\<rally>.bik, all revision 'i', 640 x 480 with Bink audio). The
   container, the video decoder (Y, then the two chroma planes at half size, in 8 x 8 blocks of ten
   kinds read from nine bundles of Huffman-coded values) and the audio decoder (blocks of quantised
   frequency bands through an inverse real DFT, the stereo channels interleaved before the transform)
   are written from the format; the codec's tables (the sixteen Huffman trees, the block scan, the
   sixteen run patterns, the intra and inter quantisers, the audio's band edges and run lengths) are
   read from the user's binkw32.dll (the installer cabinet's, 272,384 bytes, CRC-32 1b8bab1b) at run
   time, as exe.c does for CMR2.exe. The conversion to RGB is the usual BT.601 one (the DLL's own
   tables, YUV_init, are built by code). */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BINK_DLL "binkw32.dll"

typedef struct Bink Bink;

bool Bink_LoadTables(char *err, size_t cap);   /* binkw32.dll through the file layer; idempotent */
/* BinkOpen + BinkSetSoundTrack: the movie and its audio track (0 the first; none if out of range) */
Bink *Bink_Open(const char *path, int track);
void Bink_Close(Bink *b);                       /* BinkClose */
int Bink_Width(const Bink *b);
int Bink_Height(const Bink *b);
uint32_t Bink_Frames(const Bink *b);            /* +0x08 */
uint32_t Bink_Frame(const Bink *b);             /* +0x0c: the frame to be decoded next, from 1 */
void Bink_Rate(const Bink *b, uint32_t *num, uint32_t *den);
/* BinkDoFrame: the current frame's video into the planes and its audio track's blocks into the
   sample queue; false on a damaged frame (the planes are left as far as they got) */
bool Bink_DoFrame(Bink *b);
void Bink_NextFrame(Bink *b);                   /* BinkNextFrame */
/* the planes (0 Y, 1 U, 2 V; 1 and 2 at half size) */
const uint8_t *Bink_Plane(const Bink *b, int p, int *stride);
/* BinkCopyToBuffer at 32 bits: the frame as R, G, B, 0xff bytes, pitch bytes a row */
void Bink_CopyRGBA(const Bink *b, uint8_t *dst, int pitch);
/* the audio track: its rate and channels (0 without one), and the decoded samples queued since the
   last call (interleaved 16-bit), up to max frames; returns the frames given */
int Bink_AudioRate(const Bink *b);
int Bink_AudioChannels(const Bink *b);
int Bink_TakeAudio(Bink *b, int16_t *dst, int max_frames);
/* the decoded audio as floats, for the tests against a reference (interleaved, the samples since the
   last call; returns the frames) */
int Bink_TakeAudioFloat(Bink *b, float *dst, int max_frames);
