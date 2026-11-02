/* Car_Load 0x457000: car n's model, textures and data from the selection: the car (a player's from
   the player record's low six bits, 0x4086b0), its handling class (0x516b40), the detail letter by the
   game mode from the table at 0x51a3d0 (0x457c50 fills it from the detail setting) and the set
   number; <name><letter><set>.C3D (A: <name>A1N.c3d), <name>.cin, <name><letter><set>.bfl; then placed
   on the start (Car_Place). Ported for a player in a rally; the other modes' choices are logged. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

void Car_LodTable(void);       /* 0x457c50 */
bool Car_Load(int n);          /* 0x457000 */
int Car_IdByName(const char *name);   /* the port: the car table's index of a name ("205"), -1 none */
const uint8_t *Car_Cin(int n);       /* 0x457e00: car n's .cin */
/* Car_CinSection 0x457e10: section k of the .cin through its u16 offset directory */
const uint8_t *Car_CinSection(int n, int k);
void Car_Replace(int n);       /* the port: back on the start, for a restart */
int Cars_Racing(void);           /* 0x420190 */
int Cars_Count(void);          /* 0x456ca0 */
void Cars_Grid(void);          /* 0x4584d0 */
void Cars_Reset(int first, int n, bool keep);   /* 0x42b800 */
