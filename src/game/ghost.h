/* The time trial's ghost (selection bit 28 with the ghost option, 0x52afb8 bit 30): the best run so far
   played into an extra car (the last loaded, +0xc0c set: no contact, see-through), saved as the
   stage's .grp in the user's storage. ghost.c, 0x465ec0-0x466100. */
#pragma once
#include <stdbool.h>
#include "game/replay.h"
#include "sim/car.h"
#include "world/obj.h"

static inline bool Ghost_On(void);
void Ghost_New(int segs, int frames);   /* 0x465f60 */
void Ghost_Load(const char *path);      /* 0x465f90: the .grp, else a new one */
void Ghost_SetCar(Car *c);              /* 0x465fc0 */
void Ghost_Start(int seg, int pre);     /* 0x466030: its run from the start into its car */
void Ghost_End(void);                   /* 0x466080 */
bool Ghost_Playing(void);               /* 0x466090 */
void Ghost_Take(int p, int car);        /* 0x4660a0: player p's run becomes the ghost if it is the one named */
void Ghost_TakeFrom(int p);             /* 0x4660e0 */
int Ghost_Owner(void);                  /* 0x4660f0: the player whose car the ghost shows, -1 none */
void Ghost_Fade(int p, const Obj *camera);   /* 0x466100: see-through by its size on player p's view */
/* the stage's replay name (Race_Setup 0x41f560's 0x538444): <root>\replays\<rally><stage, 2 digits>H
   (L with the low-detail clouds), the arcade's ...chH / chL */
const char *Ghost_StageName(void);

#include "game/gstate.h"
static inline bool Ghost_On(void) { return Gs_SelBit28() && Gs_Bit30(); }
