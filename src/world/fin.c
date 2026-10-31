/* The stage's walls (see fin.h). */
#include "world/fin.h"
#include "world/section.h"
#include "world/stage.h"
#include "bfl.h"
#include <stdio.h>
#include <stdlib.h>

Wall *g_fin;
int g_nfin;

static int32_t rd32(const uint8_t *p) { return (int32_t)((uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24); }

void Fin_Free(void)
{
    free(g_fin);
    g_fin = NULL;
    g_nfin = 0;
}

bool Stage_LoadFinDat(const char *stage)
{
    char name[32];
    uint32_t size = 0;
    Fin_Free();
    snprintf(name, sizeof name, "%sfin.dat", stage);
    const uint8_t *d = Bfl_FindFile(&g_stage_bfl, name, NULL, &size, false);
    if (!d || size < 0x30) return false;
    g_nfin = (int)(size / 0x30);
    g_fin = calloc((size_t)g_nfin, sizeof *g_fin);
    if (!g_fin) return g_nfin = 0, false;
    for (int i = 0; i < g_nfin; i++) {
        const uint8_t *r = d + 0x30 * i;
        Wall *w = &g_fin[i];
        for (int k = 0; k < 3; k++) w->a[k] = rd32(r + 4 * k), w->b[k] = rd32(r + 0xc + 4 * k), w->n[k] = rd32(r + 0x18 + 4 * k);
        w->x24 = rd32(r + 0x24);
        w->cell = rd32(r + 0x28);
        w->type = r[0x2c];
        w->flags = r[0x2d];
        w->cap[0] = r[0x2e];
        w->cap[1] = r[0x2f];
    }
    return true;
}

void Fin_File(void)
{
    for (int i = 0; i < g_nfin; i++) {
        Wall *w = &g_fin[i];
        if (w->cell < 0 || w->cell >= g_nsec) continue;   /* the port: the original trusts the file */
        Section *s = &g_sec[w->cell];
        w->next = s->walls;
        s->walls = w;
        s->nwalls++;
    }
}
