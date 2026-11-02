/* The cars' engine sounds: Sounds\car\<engine>.bfl's thirteen loops and effects per player car
   (0x478f50), reset per race (0x478dc0), mixed each step by the revs over three bands of the
   tables at 0x51ea60, 0x51eab8, 0x51ec50 (0x47a710, 0x479360), louder in front from inside the car,
   faded by the camera's distance; the backfires, detonations and idle (0x479360), the gearbox whine
   (0x47a3d0). Not yet: the replay's Doppler, option 0x13's sounds. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

bool Engine_Load(int players);             /* 0x478f50 */
void Engine_Reset(void);                   /* 0x478dc0 */
void Engine_Volume(int32_t sfx_percent);   /* 0x427c10's: a third of the effects volume */
void Engine_Update(int car, int player);   /* 0x47a710 */
void Engine_Pause(void);                   /* 0x478c40 */
int32_t Engine_DistanceFade(int car, int player);   /* 0x427d50: 1.0 within 2 units of player's camera, 0 beyond 100 */
