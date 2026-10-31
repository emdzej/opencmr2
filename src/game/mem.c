/* The original's data section as the game state (see mem.h). */
#include "game/mem.h"
#include "exe.h"
#include "platform.h"
#include <stdlib.h>
#include <string.h>

static uint8_t *ram;

bool Mem_Init(void)
{
    if (!ram) ram = malloc(MEM_SIZE);
    if (!ram) return false;
    memset(ram, 0, MEM_SIZE);
    const uint8_t *d = exe_data(MEM_BASE, 0x14000);
    if (!d) return false;
    memcpy(ram, d, 0x14000);
    return true;
}

void Mem_Free(void)
{
    free(ram);
    ram = NULL;
}

uint8_t *gm(uint32_t va)
{
    if (!ram || va < MEM_BASE || va >= MEM_BASE + MEM_SIZE) {
        plat_log("gm: address outside the game state");
        abort();
    }
    return ram + (va - MEM_BASE);
}
