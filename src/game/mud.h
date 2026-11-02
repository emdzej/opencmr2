/* The mud on the first car's body texture (0x46e530-0x46eef0): Car_Load (0x458050, for a car of
   detail A or C outside the time trial) takes the first "BODF" texture and its class's eleven
   patches of the texture (0x51a3e8, 88 bytes a class); in the first view of a one-player race,
   unpaused, a wet wheel spinning on a loose surface throws splats onto the patches (the front wheels
   along their patch, the rear ones anywhere on the others), and each frame the splats inside one
   quarter of the texture are painted in with a brush (0x51c280, or 0x51c240 at the lower texture
   details) and the list emptied. */
#pragma once

void Mud_Setup(int car, int cls);   /* 0x458050 (0x46e580, 0x46e620 for each patch) */
void Mud_Throw(int car);            /* 0x46ea10 */
void Mud_Paint(void);               /* 0x46e780 */
void Mud_Reset(void);               /* 0x46ef20: the body texture clean again */
