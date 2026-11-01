/* The co-driver (0x416670-0x418580): the speech (0x416770: some sixty WAVs from the speech archive), the
   stage's call descriptors (<stage>.cod, 32-bit fields: severity, direction, distance, joins, changes,
   modifiers), indexed by the centre line's points (byte +0x19, 0xff none). As the car passes a point
   its call is queued (0x4174e0) and spoken (0x418000 through the queue 0x417660/0x4176b0); the countdown
   gets "three, two, one" and the start "go". Not yet: the HUD's arrows, the wrong-way message, the
   split screen. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

bool Codriver_LoadSpeech(void);            /* 0x416770 (0x416710) */
bool Codriver_LoadCod(const char *stage);  /* 0x416720 (with 0x416670) */
void Codriver_Reset(void);                 /* 0x416670 */
void Codriver_Volume(int32_t speech_percent);   /* 0x418560 */
int32_t Codriver_Level(void);              /* 0x418570: the speech volume, 16.16 */
void Codriver_Start(int player);           /* 0x416f70: each step of the countdown */
void Codriver_Step(int player);            /* 0x417090: each step of the stage */
void Codriver_Notes(int player);           /* 0x4177d0's speech, each frame */
int Codriver_Said(void);                   /* the port: the words queued so far */
