/* The music (0x4a28d0-0x4a3250): one MS ADPCM file (sounds\music\*.adp) streamed through the ACM into
   a looping DirectSound buffer of eight segments of 0xfe80 bytes, 44,100 Hz stereo; the segments
   behind the play cursor are refilled each frame, and the file starts again at its end. */
#pragma once
#include <stdbool.h>

void Music_Open(const char *path);   /* 0x4a28d0 */
void Music_SetVolume(int v);         /* 0x4a31f0: 0-100 (the options' music volume), 0 silent */
void Music_Update(void);             /* 0x4a2bd0: the whole buffer filled and playing */
void Music_Poll(void);               /* 0x4a2fe0 */
void Music_Stop(bool keep_name);     /* 0x4a2b50 */
