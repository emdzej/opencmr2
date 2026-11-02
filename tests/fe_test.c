/* The front end from the start-up through the menus to a stage (Single Stage, a guest, the first car,
   Finland's first stage), then back to the menus: the process's states, the pages on the way, the
   selection it leaves, the race's start, the return. */
#include "audio/dsound.h"
#include "exe.h"
#include <math.h>
#include "fe/fe.h"
#include "fe/pages.h"
#include "fixmath.h"
#include "game/mem.h"
#include "game/race.h"
#include "game/pause.h"
#include "game/timer.h"
#include "platform_host.h"
#include "render/d3d7.h"
#include "render/mesh.h"
#include "render/rstate.h"
#include "fe/movie.h"
#include "vfs.h"
#include "vfs_host.h"
#include "world/stage.h"
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static uint32_t frame;
static void step(void)
{
    uint32_t ms = (uint32_t)((uint64_t)frame++ * 1000 / 60);
    Time_Set(ms);
    Timer_Sample(ms);
    Game_Frame();
}

/* a key held for 3 frames, then 40 frames for the page to change */
static void press(int code)
{
    plat_host_key(code, true);
    for (int i = 0; i < 3; i++) step();
    plat_host_key(code, false);
    for (int i = 0; i < 40; i++) step();
}

int main(void)
{
    char err[128];
    if (!vfs_mount_default() || !exe_init(err, sizeof err)) {
        printf("fe_test: no game data, skipped\n");
        return 0;
    }
    Math_InitTables();
    CHECK(Mem_Init());
    D3d_Open(640, 480, 16);
    Render_InitStates();
    VB_CreatePool();
    memset(&g_world, 0, sizeof g_world);
    g_world.kind = 3;
    /* the boot screens (5 s each, the movies skipped with select once they play, the title 5 s) to the
       language page */
    enum { ENTER_KEY = 0x1c };
    int skipped = 0;
    for (int i = 0; i < 60 * 40 && Fe_Page() != &pg_8221d8; i++) {
        if (Movie_Playing()) {
            for (int k = 0; k < 20; k++) step();   /* a few frames shown */
            press(ENTER_KEY);
            skipped++;
        }
        step();
    }
    printf("movies skipped: %d\n", skipped);
    CHECK(skipped == 2 || !vfs_exists("FrontEnd/Videos/cm.bik"));
    CHECK(Fe_Page() == &pg_8221d8);
    printf("language page at frame %u\n", frame);
    /* the menus' music plays (select1.adp, from the title's end) */
    static float mix[2 * 4410];
    Dsb_Mix(mix, 4410);
    double e = 0;
    for (int i = 0; i < 2 * 4410; i++) e += (double)mix[i] * mix[i];
    printf("music rms %.4f\n", sqrt(e / (2 * 4410)));
    CHECK(sqrt(e / (2 * 4410)) > 0.01);
    enum { ENTER = 0x1c };
    press(ENTER);
    CHECK(Fe_Page() == &pg_81d6d8);
    CHECK(pg_81d6d8.sel == 1);   /* rally */
    press(ENTER);
    CHECK(Fe_Page() == &pg_823a38);
    CHECK(pg_823a38.sel == 2);   /* single stage */
    press(ENTER);
    CHECK(Fe_Page() == &pg_823fd8);   /* difficulty */
    press(ENTER);
    CHECK(Fe_Page() == &pg_81f2f8);   /* players */
    press(ENTER);
    CHECK(Fe_Page() == &pg_8241b8);   /* driver select */
    CHECK(pg_8241b8.sel == 2);   /* play as guest */
    press(ENTER);
    CHECK(Fe_Page() == &pg_824a38);   /* tag entry */
    press(0x32), press(0x24), press(0x2d);   /* m, j, x */
    CHECK(!strcmp((const char *)gm(0x52fa18 + (gm32(0x531350) >> 0x12 & 0xf) * 0x650), "mjx"));
    press(ENTER);
    CHECK(Fe_Page() == &pg_821e18);   /* car select */
    press(ENTER);
    CHECK(Fe_Page() == &pg_81cf58);   /* transmission */
    press(ENTER);
    CHECK(Fe_Page() == &pg_81eb78);   /* country */
    press(ENTER);
    CHECK(Fe_Page() == &pg_81e3f8);   /* stage */
    press(ENTER);
    CHECK(Fe_Page() == &pg_81f4d8);   /* weather */
    press(ENTER);
    CHECK((gm32(0x52afb4) >> 3 & 0x7f) == 2);   /* single stage */
    CHECK((gm32(0x52f2ac) & 0x1f) == 0 && (gm32(0x52f2ac) >> 5 & 0x1f) == 0);   /* Finland, stage 1 */
    /* the service area (mode 2): right to Race, Enter */
    for (int i = 0; i < 300 && Game_Mode() != 2; i++) step();
    CHECK(Game_Mode() == 2);
    for (int i = 0; i < 120; i++) step();
    press(0xcd), press(0xcd), press(0xcd), press(ENTER);
    for (int i = 0; i < 300 && Game_Mode() != 3; i++) step();
    CHECK(Game_Mode() == 3);
    printf("race at frame %u\n", frame);
    for (int i = 0; i < 400 && Race_State(0) != 7 && Race_State(0) != 8; i++) step();
    CHECK(Race_State(0) == 7 || Race_State(0) == 8);
    /* restart: Escape, up twice to "restart stage", Enter; up twice on the confirmation, Enter: the
       fade, then the cars on the line again */
    for (int i = 0; i < 200; i++) step();
    int t_before = Race_StageTime(0);
    press(0x01);
    CHECK(Pause_Active());
    press(0xc8), press(0xc8), press(ENTER);
    press(0xc8), press(0xc8), press(ENTER);
    for (int i = 0; i < 200 && Pause_Active(); i++) step();
    CHECK(!Pause_Active());
    for (int i = 0; i < 30; i++) step();
    printf("after the restart: state %d, stage time %d (was %d)\n", Race_State(0), Race_StageTime(0), t_before);
    CHECK(Race_State(0) == 5 || Race_State(0) == 7);
    for (int i = 0; i < 400 && Race_State(0) != 8; i++) step();
    /* Escape pauses; up to "exit", Enter; up to "quit game", Enter: the fade, the results' exit, the
       menus */
    press(0x01);
    CHECK(Pause_Active());
    press(0xc8);
    press(ENTER);
    press(0xc8);
    press(ENTER);
    for (int i = 0; i < 400 && Fe_Page() != &pg_81d6d8; i++) step();
    CHECK(Game_Mode() == 0);
    CHECK(Fe_Page() == &pg_81d6d8);
    printf("menus again at frame %u\n", frame);
    printf("fe_test: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
