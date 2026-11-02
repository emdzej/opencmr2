/* The cars' engine sounds: Sounds\car\<engine>.bfl's thirteen loops and effects per player car
   (0x478f50), reset per race (0x478dc0), mixed each step by the revs over three bands of the
   tables at 0x51ea60, 0x51eab8, 0x51ec50 (0x47a710, 0x479360), louder in front from inside the car,
   faded by the camera's distance; the backfires, detonations and idle (0x479360), the gearbox whine
   (0x47a3d0), option 0x13's two loops; the two nearest other cars on the first player's rt loop
   (0x47aa70, 0x47ad20). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

bool Engine_Load(int players);             /* 0x478f50 */
void Engine_Reset(void);                   /* 0x478dc0 */
void Engine_Volume(int32_t sfx_percent);   /* 0x427c10's: a third of the effects volume */
void Engine_Update(int car, int player);   /* 0x47a710 */
void Engine_Pause(void);                   /* 0x478c40 */
void Engine_Others(void);                  /* 0x47aa70: modes 5 and 6 with one player */
void Engine_Circuit(void);                 /* 0x47ad20: circuit modes 11 and 12, the network slots' cars */
int32_t Engine_DistanceFade(int car, int player);   /* 0x427d50: 1.0 within 2 units of player's camera, 0 beyond 100 */
