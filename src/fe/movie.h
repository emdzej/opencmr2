/* The Bink movies' player (0x50fdf0-0x5103d0): a movie opened on its sound track, each frame decoded
   when its time comes (BinkWait) and blitted to the screen at 640 x 480, or 1,024 x 768 and 1,280 x 960
   on wider modes, centred (BinkBufferSetScale, BinkBufferSetOffset), its sound streamed into a sound
   buffer; the select button ends it. The original's 0x50fdf0 loops until the end (flag 4 clear) or
   steps once (flag 4); the port steps once a frame. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

bool Movie_Open(const char *path, int track);   /* 0x50ff90: false when it can't be played */
bool Movie_Playing(void);                       /* DAT_00831c68 */
/* 0x510120: the input (select ends it: false), the frame due decoded, false past the last */
bool Movie_Step(void);
void Movie_Draw(void);    /* the last frame decoded, scaled and centred (BinkBufferBlit) */
void Movie_Close(void);   /* 0x5103d0 */
