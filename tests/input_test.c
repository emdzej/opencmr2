/* The input records and the controller configuration (game/input.c, game/ctlcfg.c): the keyboard
   players' default keys in the race and the menus' fixed keys, a game controller's record (axes,
   buttons, the hat) and its configuration's defaults, the player moved onto it and its bits merged, the
   action masks, Controller.rcf kept. Skips (exit 0) without data (the game state needs the exe). */
#include "exe.h"
#include "game/ctlcfg.h"
#include "game/input.h"
#include "game/mem.h"
#include "platform_host.h"
#include "vfs.h"
#include "vfs_host.h"
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void)
{
    char err[256];
    if (!vfs_mount_default() || !exe_init(err, sizeof err)) {
        printf("input_test: no game data, skipped\n");
        return 0;
    }
    CHECK(Mem_Init());
    /* a standard pad in slot 0 before the devices are found */
    PlatGamepad g = {0};
    g.standard = true, g.nbuttons = 17, g.naxes = 4;
    plat_host_gamepad(0, &g);
    Input_Init();
    CHECK(Input_DeviceCount() == 3);
    InputRecord *k0 = Input_Record(0), *k1 = Input_Record(1), *p = Input_Record(2);
    CHECK(k0->device == DEV_KEYBOARD && k1->device == DEV_KEYBOARD && p->device == DEV_PAD);
    CHECK(!strcmp(k0->name, "Keyboard") && p->joy == 0 && p->buttons == 10 && p->naxes == 5);
    CHECK(k0->key[0] == 0xcb && k0->key[1] == 0xcd && k0->key[10] == 0x01 && k1->key[0] == 0x4b);
    /* the race's keys: Left is action 0 (bit 1), Escape the pause's 0x400 */
    plat_host_key(0xcb, true);
    plat_host_key(0x01, true);
    Input_PollAll();
    CHECK((k0->held & 0xffff) == 0x401 && k0->pressed == 0x401);
    plat_host_key(0xcb, false);
    plat_host_key(0x01, false);
    /* the menus' keys: Escape 0x20 */
    Input_SetMenuMode(true);
    plat_host_key(0x01, true);
    Input_PollAll();
    CHECK((k0->held & 0xffff) == 0x20);
    plat_host_key(0x01, false);
    Input_PollAll();
    /* the pad: stick left and down (past 0x9999), button A (0x10), the d-pad up */
    g.axis[0] = -0.8f, g.axis[1] = 0.9f, g.button[0] = 1, g.button[12] = 1;
    plat_host_gamepad(0, &g);
    Input_PollAll();
    printf("pad: held %#x, x %d, y %d\n", p->held, p->axis[0].value, p->axis[1].value);
    CHECK((p->held & 0xffff) == (1 | 8 | 4 | 0x10) && p->axis[0].value < -0x9999 && p->axis[1].value > 0x9999);
    /* the configuration: the keyboards' keys and names, the pad's bits and axis slots */
    Ctl_Configure();
    CHECK(Ctl_Count() == 3);
    uint32_t c0 = CTL_CONFIGS, c2 = CTL_CONFIGS + 2 * CTL_SIZE;
    CHECK(gm8(c0 + 0x13e) == 0xcb && gm16(c0 + 0x128) == 0 && gm32(c2 + 0x110) == 1 && gm16(c2 + 0x12c) == 4);
    CHECK(gm32(c2 + 0x210) == 1 && gm16(c2 + 0x2d8) == 0 && gm16(c2 + 0x2dc) == 1);
    CHECK(Ctl_Mask(0, 0) == 1 && Ctl_Mask(0, 9) == 0x200);
    /* player 0 on the pad: its bits merged into the keyboard record, its masks, axes */
    Ctl_SetDevice(0, 2);
    CHECK(Ctl_Mask(0, 4) == 0x10 && Ctl_Axis(0, 0) == 0 && Ctl_Axis(0, 2) == 1 && Ctl_AnalogSteer(0));
    Input_PollAll();
    Ctl_Merge(0, k0);
    CHECK((k0->held & 0x1d) == 0x1d);
    printf(fails ? "input_test: FAILED\n" : "input_test: ok\n");
    return fails != 0;
}
