/* The process framework (see proc.h). */
#include "game/proc.h"

static bool skip_draw;   /* DAT_00593cac */

void Proc_SkipDraw(void) { skip_draw = true; }

void Proc_InitSlot(ProcSlot *s, uint32_t state, uint32_t kind)
{
    s->frames = 0;
    s->word = (s->word >> 8 & 0xfc0000) << 8 | (state & 0xff) | (kind & 0xff) << 8;
}

void Proc_Init(ProcGroup *g, uint8_t n, ProcSlot *slots, const ProcState *states, const uint8_t *trans)
{
    g->n = n;
    g->slots = slots;
    g->states = states;
    g->trans = trans;
}

bool Proc_Event(ProcGroup *g, uint32_t slot, uint32_t event, uint32_t prio)
{
    ProcSlot *s = &g->slots[slot & 0xff];
    uint32_t w = s->word, pend = w >> 24 & 3;
    if (pend != 3 && (int)prio <= (int)pend) return false;
    for (const uint8_t *t = g->trans; !(t[0] == 0xff && t[1] == 0xff && t[2] == 0xff && t[3] == 0xff); t += 4) {
        if ((t[0] == (uint8_t)w || t[0] == 0xff) && (t[1] == (uint8_t)(w >> 8) || t[1] == 0xff) && t[2] == (uint8_t)event) {
            s->word = (uint32_t)t[3] << 16 | (prio & 3) << 24 | (w & 0xfc00ffff);
            return true;
        }
    }
    return false;
}

void Proc_UpdateAll(ProcGroup *g)
{
    for (uint32_t i = 0; i < g->n; i++) {
        ProcFn f = g->states[g->slots[i].word & 0xff].update;
        if (f) f(g, i);
    }
}

void Proc_DrawAll(ProcGroup *g)
{
    if (skip_draw) {
        skip_draw = false;
        return;
    }
    for (uint32_t i = 0; i < g->n; i++) {
        ProcFn f = g->states[g->slots[i].word & 0xff].draw;
        if (f) f(g, i);
    }
}

void Proc_Advance(ProcGroup *g)
{
    for (uint32_t i = 0; i < g->n; i++) {
        ProcSlot *s = &g->slots[i];
        if (!(s->word & 0x3000000)) s->frames++;
        else {
            s->word = (s->word >> 16 & 0xff) | (s->word & 0xffffff00);
            s->word &= 0xfc00ffff;
            s->frames = 0;
        }
    }
}
