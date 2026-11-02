/* The cars' passes (0x46b670-0x46bd50): per car and view the kind of drawing by the view's camera
   (0x46bb40, into 0x588cd4), and from it each car's objects' pass masks (0x46b8f0): the body always,
   the wheels and the second model outside, the cockpit's model inside it, nothing for the bonnet and
   roof views of the own car or another car with two players apart. */
#pragma once
#include "sim/car.h"
#include <stdint.h>

void Cars_ViewKinds(void);       /* 0x46bb40 */
void Car_Masks(const Car *c);    /* 0x46b8f0 */
uint8_t Car_Shown(int n);        /* 0x46bd40 */
void Cars_Lod(int p, const int16_t view[4]);   /* 0x428bf0: which cars get their full models */
