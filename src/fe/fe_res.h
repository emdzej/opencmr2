/* The front end's loads at start-up (0x4e2e50, 0x4f3b50-0x4f4ef0): textures, sounds, fonts, the text
   and the credits. */
#pragma once
#include "render/tex.h"
#include <stdbool.h>

typedef struct {
    Tex *arrow_a, *arrow_d;      /* 0x818548, 0x8185b8: Ar_640A, Ar_640D */
    Tex *lg_matrix, *sm_matrix;  /* 0x8182b8, 0x818324 */
    Tex *banner[8], *flag[8];    /* 0x818328, 0x818348: the rallies' banners and small flags */
    Tex *gold, *silver, *bronze; /* 0x8182c4, 0x8182c0, 0x8182bc */
    Tex *dot[4];                 /* 0x819e94: dot00-dot03 */
    Tex *car_front[22], *car_back[22];   /* 0x81884c, 0x8182cc */
    Tex *livery[3];              /* 0x818530 */
} FeTextures;
extern FeTextures g_fe;

typedef struct { const char *a, *b; } FeCredit;

void Fe_LanguageCount(void);     /* 0x4f4b40 */
int Fe_Languages(void);          /* DAT_0081a754 */
void Fe_LoadTextures(void);      /* 0x4e2e50 */
bool Fe_LoadSounds(void);        /* 0x4f3b50 */
void Fe_MenuSounds(void);        /* 0x4f3bb0 */
bool Fe_LoadFonts(void);         /* 0x4f4d20 */
void Fe_ReloadFonts(void);       /* 0x4f4d80 */
bool Fe_LoadText(void);          /* 0x4f4b90 */
void Fe_LoadCredits(void);       /* 0x4f4910 */
int Fe_Credits(const FeCredit **lines);
void Fe_ResetPanels(void);       /* 0x4ef150 */
void Fe_LoadDots(void);
void Fe_LoadCars(void);          /* 0x4d2590 */          /* 0x4f3f60 */
void Fe_LoadDmd(void);           /* 0x4d5ca0 */
