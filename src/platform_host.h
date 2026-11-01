/* The native platform's test hooks (platform_host.c). */
#pragma once
#include <stdbool.h>

void plat_host_key(int code, bool down);   /* a DirectInput key code held or let go */
