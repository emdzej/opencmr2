/* The race's pause menu (0x401000-0x405500): pages on the front end's menu engine, built when the race
   starts (0x403500): the main page (0x52ad60: graphics, sound, controller, restart, exit, continue),
   the sound volumes (0x52a670), the restart (0x529af8) and exit (0x52a0c0) confirmations, the graphics
   and camera pages. Escape (bit 0x400 of the race's input, the key binding 10) pauses on the line, in
   the countdown and on the stage; restart and quit fade the screen out and post the race's event 1
   (to the results state) with their flag (0x53810c, 0x53810d). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

void Pause_Build(void);            /* 0x403500, with the menus' arrows 0x4054f0 */
bool Pause_Pressed(int player);    /* 0x404e10 */
void Pause_Begin(int player);      /* 0x404ea0 */
void Pause_End(void);              /* 0x404ef0 */
bool Pause_Active(void);           /* 0x404f20 */
bool Pause_OsdPage(int p);         /* 0x4054b0 */
void Pause_Frame(void);            /* 0x404f40: the page's input */
void Pause_Draw(void);             /* 0x4054a0: the page's draw */
bool Pause_RestartAsked(void);     /* DAT_0053810c */
bool Pause_QuitAsked(void);        /* DAT_0053810d */
bool Pause_ServiceAsked(void);     /* DAT_00537ffa */
void Pause_ClearAsks(void);
void Pause_Ask(bool restart, bool service, bool quit);   /* 0x41f280, 0x41f290, 0x41f2a0 */
typedef struct Tex Tex;
Tex *Pause_ArrowOn(void);         /* 0x4055e0 */
Tex *Pause_ArrowOff(void);        /* 0x4055f0 */
