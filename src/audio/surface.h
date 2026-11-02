/* The surface sounds (0x418d30-0x41b040): each car's wheels on the stage's surfaces. The stage's
   .srf (Stage_LoadSrf 0x41fcd0) marks the sound sets in use (0x41afe0); the loader (0x418ff0) loads
   their Sounds\SurfSnd\<set>_i|o|ski|sko<n>.wav, inside and outside the car, the rain on the roof
   and on the body. Each step (0x41af60) the wheels' surfaces pick the car's surface (0x41a340), a
   change of the surface's class starts and ends the loops and skids of a transition (0x419200,
   0x419b90), and the levels follow the speed, the slips and the transition's fade (0x41a5c0); the
   loops change to another of their set now and then (0x419cd0) and when the camera goes in or out
   (0x419ed0). The state lives at the original's addresses: per car 0xb4 bytes at 0x5377a8. */
#pragma once
#include <stdint.h>

void Surface_Mark(const uint8_t *srf, uint32_t n);   /* 0x41afe0 */
void Surface_LoadSounds(void);                       /* 0x418ff0 (Race_S3_Stage) */
void Surface_Reset(void);                            /* 0x418f20 */
void Surface_Volume(int32_t v);                      /* 0x41b040 (16.16) */
void Surface_Silence(void);                          /* 0x418ee0: every voice at volume 0 */
void Surface_Step(int car, int player);              /* 0x41af60 (Race_Update) */
