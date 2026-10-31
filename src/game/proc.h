/* The process framework (0x49c0a0-0x49c400, docs/re/architecture.md#modes-and-processes): a group of
   processes, each a slot with a state word (byte 0 the state, byte 1 the kind, byte 2 the next state,
   bits 24-25 a pending switch's priority) and a frame count; a state table of {update, draw} pairs; a
   transition table of (state, kind, event, next) byte quadruples, 0xff matching any, ended by four
   0xff. The original's tables hold code addresses; the port has its own state tables in the same
   order and reads the transition tables from the exe. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct ProcGroup ProcGroup;
typedef void (*ProcFn)(ProcGroup *g, uint32_t slot);
typedef struct { ProcFn update, draw; } ProcState;
typedef struct { uint32_t word, frames; } ProcSlot;
struct ProcGroup {
    uint8_t n;                  /* +0x00 */
    ProcSlot *slots;            /* +0x04 */
    const ProcState *states;    /* +0x08 */
    const uint8_t *trans;       /* +0x0c */
};

void Proc_InitSlot(ProcSlot *s, uint32_t state, uint32_t kind);   /* 0x49c150 */
void Proc_Init(ProcGroup *g, uint8_t n, ProcSlot *slots, const ProcState *states, const uint8_t *trans);   /* 0x49c190 */
/* 0x49c1c0: the event for a slot: the first transition matching its state and kind takes it, unless a
   switch of the same or higher priority is pending (3 always gives way) */
bool Proc_Event(ProcGroup *g, uint32_t slot, uint32_t event, uint32_t prio);
void Proc_UpdateAll(ProcGroup *g);   /* 0x49c2c0 */
void Proc_DrawAll(ProcGroup *g);     /* 0x49c310: skipped for the frame after Proc_SkipDraw */
void Proc_Advance(ProcGroup *g);     /* 0x49c370 */
void Proc_SkipDraw(void);            /* sets DAT_00593cac */
static inline uint8_t Proc_State(const ProcGroup *g, uint32_t slot) { return (uint8_t)g->slots[slot].word; }
