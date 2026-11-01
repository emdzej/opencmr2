/* The screen's colour test (0x4bc490, docs/re/render.md#the-colour-test): the share of a rectangle of
   the frame that is near a colour, for the sun glow. A query keeps the rectangle and colour and returns
   the last measure; the measure itself runs once a frame in World_Render, after the 3D world and
   before the 2D layers, so a query sees the previous frame's share. */
#pragma once
#include <stdint.h>
#include "render/twod.h"

/* 0x4bc490 with a rectangle: kept with the colour's bytes 0-2 and the tolerance; the last share
   (0-100) returned */
uint8_t Screen_ColourQuery(const Rect16 *r, const uint8_t c[3], uint8_t tol);
/* 0x4bc490 without one: the kept rectangle measured, the share kept */
void Screen_ColourMeasure(void);
