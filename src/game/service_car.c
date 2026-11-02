/* The service screens' scene pieces (see service.h): the stage panel's textures (0x503ea0), the
   stage's information (0x5040f0), the tyres (0x505e70) and the car on its stand (0x506080-0x509dc0). */
#include "game/service_int.h"
#include "pending.h"

void Sv_LoadTextures(void) { PENDING_NOTE(0x503ea0, "the service screens' map and weather textures"); }
void Sv_StageInit(void) { PENDING_NOTE(0x5040f0, "the stage's information"); }
void Sv_TyresInit(void) { PENDING_NOTE(0x505e70, "the tyres left"); }
void Sv_CarShow(int p, bool on) { (void)p, (void)on; PENDING_NOTE(0x506b20, "the car on its stand"); }
void Sv_CarView(int p, int view, bool now) { (void)p, (void)view, (void)now; PENDING_NOTE(0x506930, "the car's view"); }
void Sv_CarTyres(int p, int view, bool now) { (void)p, (void)view, (void)now; PENDING_NOTE(0x5009c0, "the car's tyres view"); }
void Sv_CarTurn(void) { PENDING_NOTE(0x506720, "the car turning"); }
void Sv_Free(void) {}
