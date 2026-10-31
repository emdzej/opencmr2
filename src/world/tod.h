/* The stage's time of day and weather (docs/re/world.md#time-of-day): the stage selection word, the
   start and end times, the weather pair, and the horizon records (<hhmm>[CLO|STO|BLI].hor in the stage
   archive) blended for a time and weather. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* The stage selection word 0x52f2ac: bits 0-4 the rally (0 FIN, 1 GRE, 2 FRA, ... in the exe's prefix table 0x51931c), 5-9
   the stage (0-based), 10-11 and 12-13 two further indices, bit 24 and bit 26 the modes that take the
   time and weather from the tables at 0x516260 and 0x52f36c (not set in a rally). */
/* Both live in the game state (game/mem.h): 0x52f2ac, and the mode word 0x52afb4 (0x405d80: bits 3-9
   the mode; 0 a rally). */
/* The port: the selection for a stage id ("fra02"), from the exe's prefixes; false if unknown. */
bool Tod_Select(const char *stage);
uint32_t Tod_Rally(void);       /* 0x406910 */
uint32_t Tod_Stage(void);       /* 0x406930 */
uint32_t Tod_StartTime(void);   /* 0x407650: hhmm */
uint32_t Tod_EndTime(void);     /* 0x407710 */
/* 0x407520: the stage's two weather conditions (start, end), from 0x52f100, which the game fills
   before a rally (zero in the exe: clear). */
const int32_t *Tod_Weather(uint32_t stage);

enum { HOR_SIZE = 80 };
typedef struct { uint8_t b[HOR_SIZE]; } Horizon;

/* 0x461710: a to b by t (16.16): the seven i32 at +0x00, the bytes +0x1c-+0x47 and +0x48-+0x4d. */
void Hor_Blend(Horizon *out, const Horizon *a, const Horizon *b, int32_t t);
/* 0x461830: the record for a time (hhmm) and the start (which 0) or end (1) weather, from the 48 files
   (NULL entries missing). NULL when the time is past 24:00, its minutes are 60 or more, or a file is
   missing. */
const Horizon *Hor_ForTime(uint32_t time, int which, const uint8_t *const files[48]);
/* Stage_LoadHorizons 0x41fd30: the 48 files from the stage slot, the start and end records (0x538238,
   0x538234), then the default lights (0x4918d0). */
void Stage_LoadHorizons(void);
extern const Horizon *g_hor_start, *g_hor_end;
/* The light part of 0x460da0: the low and high light colours (0x547998, 0x5479a4, 16.16 per channel)
   from the start record, or 200 and 255 without records. */
extern int32_t g_hor_low[3], g_hor_high[3];
void Hor_Apply(void);
/* The light part of the race start 0x45eca0: the low colour (alpha 255) if it differs from the current
   one (0x462cb0), the high colour (0x492e30), then every cell relit (0x4b3f20). */
void Tod_StartLights(void);
