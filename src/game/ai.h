/* The computer drivers (0x47bda0-0x47d330, 0x498590-0x498d2c): each CPU car steers, throttles and
   brakes from small neural networks read from the stage's .ai0/.ai1/.ai2 (by the difficulty), fed
   with the car's state against the centre line and the file's racing lines; rules on top keep it
   off the verges, past slower cars and out of others' sides. docs/re/ai.md. */
#pragma once
#include "sim/car.h"
#include <stdbool.h>
#include <stdint.h>

void Ai_LoadLines(const char *stage);     /* 0x47c2f0 (Stage_Load, with selection bit 24) */
void Ai_Init(void);                       /* 0x47bda0 (0x424c50 via 0x48ca70, selection bit 26) */
void Ai_Go(bool restart);                 /* 0x47bdc0: the start's revs and launches (the countdown) */
/* 0x47bdd0: car c (index n)'s controls +0x1d0-+0x1d3, +0x1d8, unless `peek` */
void Ai_Drive(Car *c, int n, bool peek);
int32_t Ai_EdgeDistance(int n);           /* 0x47c5b0: the car's distance to the nearer verge */
bool Ai_Loaded(void);
