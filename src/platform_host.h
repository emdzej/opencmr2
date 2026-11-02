/* The native platform's test hooks (platform_host.c). */
#pragma once
#include <stdbool.h>
#include "platform.h"

void plat_host_key(int code, bool down);   /* a DirectInput key code held or let go */
void plat_host_gamepad(int slot, const PlatGamepad *g);   /* a controller in the slot, NULL none */
