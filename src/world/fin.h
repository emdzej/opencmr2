/* The stage's walls, <stage>fin.dat (docs/formats/stage.md#fin-dat): 0x30-byte records, each a segment
   along an edge of the stage with its normal, a type and the end caps, filed into the section cells. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct Wall {
    int32_t a[3];        /* +0x00: the first end (y -1000 or 1000 in the files seen) */
    int32_t b[3];        /* +0x0c: the second end */
    int32_t n[3];        /* +0x18: the normal, horizontal */
    int32_t x24;         /* +0x24: not read by the collision */
    int32_t cell;        /* +0x28: the cell; the list link once filed */
    struct Wall *next;
    uint8_t type;        /* +0x2c: 0, 1 none; 6, 0x17, 0x18 special; 7-11 no-ops; 0x19 sets +0xbf8 */
    uint8_t flags;       /* +0x2d: bit 0 the two-sided test (0x490570) and no end caps */
    uint8_t cap[2];      /* +0x2e, +0x2f: the ends' cap angles in 1.8 degree units, 0xff none */
} Wall;

/* Stage_LoadFinDat 0x490c30: <stage>fin.dat from the stage archive (stage: "fra02"); false without it. */
bool Stage_LoadFinDat(const char *stage);
/* Section_Init's part: each record in front of its cell's list (+0x28), the cell's count (+0x24) up. */
void Fin_File(void);
void Fin_Free(void);   /* the port */
extern Wall *g_fin;    /* DAT_0072d254 */
extern int g_nfin;     /* DAT_0072d250 */
