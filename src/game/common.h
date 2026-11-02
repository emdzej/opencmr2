/* The race's common archives (Bfl_MountCommon 0x455080) and fonts (0x478b50). */
#pragma once
#include "bfl.h"
#include <stdbool.h>

/* 0x455080: Game\Bigfiles\Com640.bfl (Com1024 for a 1,024-wide mode with the texture memory, a C
   variant with compressed textures), the language's speech and text archives under CountrySpecific,
   Game\Bigfiles\Common.bfl, Sounds\surface.bfl, and for the super special FireWork.bfl. */
void Bfl_MountCommon(void);
BflSlot *Bfl_Com(void);        /* 0x4552a0 (0x541f18) */
BflSlot *Bfl_Speech(void);     /* 0x4552b0 */
BflSlot *Bfl_Text(void);       /* 0x4552f0 */
BflSlot *Bfl_CommonArc(void);  /* 0x455290 */
BflSlot *Bfl_Surface(void);    /* 0x4552e0 */
BflSlot *Bfl_Firework(void);   /* 0x4552d0 (0x541f48) */
/* 0x478b50: seven font slots from the Com archive, the names at 0x51e994 (20 bytes apart):
   general\hel_13pt, hel_20pt, hel_36pt, lcd_14pt, lcd_640, ocr_12pt, ocr_60pt */
void Race_LoadFonts(void);
bool Race_LoadText(void);       /* 0x478a20 */
