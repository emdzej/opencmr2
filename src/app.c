/* The application (see app.h): the exe check, then the game (Game_Frame 0x4d0780: the front end, the
   race) each frame; with --param car= or stage= the viewer instead (viewer.h). */
#include "app.h"
#include "audio/dsound.h"
#include "audio/snd.h"
#include "bfl.h"
#include "c3d.h"
#include "exe.h"
#include "game/mem.h"
#include "game/profile.h"
#include "fixmath.h"
#include "font.h"
#include "image.h"
#include "platform.h"
#include "text.h"
#include "viewer.h"
#include "game/input.h"
#include "fe/fe.h"
#include "game/timer.h"
#include "render/dlight.h"
#include "render/d3d7.h"
#include "render/mesh.h"
#include "render/rstate.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void gl_dev_register(void);   /* render/gl_dev.c */

static bool viewing, playing;
static uint32_t frame;

bool app_init(void)
{
    char err[256], m[300];
    if (!exe_init(err, sizeof err)) {
        snprintf(m, sizeof m, "OpenCMR2: %s", err);
        plat_log(m);
        return false;
    }
    plat_log("OpenCMR2: CMR2.exe v1.11 found");
    Math_InitTables();   /* the original fills them at start-up */
    Mem_Init();          /* the game state: the data section from the exe */
    Snd_Init();
    char car[64], stage[64];
    bool c = plat_param("car", car, sizeof car), st = plat_param("stage", stage, sizeof stage);
    if (c || st) {
        Options_Defaults();   /* the front end's start-up work (0x4d15e0), for the viewer's race */
        Profiles_Init();
        Input_Init();         /* 0x49eaf0 */
        gl_dev_register();
        viewing = viewer_init(c ? car : NULL, st ? stage : NULL);   /* both: the car parked on the stage */
        return true;
    }
    /* the display (WinMain 0x4a9720's set-up: 640 x 480, the render states, the vertex buffers) */
    gl_dev_register();
    if (!D3d_Open(640, 480, 32)) return false;
    Render_InitStates();
    Dlight_Restore();   /* 0x4b2e50, in the device's set-up 0x4a78a0 */
    VB_CreatePool();
    playing = true;
    return true;
}

bool app_frame(void)
{
    if (viewing) viewer_frame();
    else if (playing) {
        /* the clocks from the frame count (60 Hz): timeGetTime() and the game's sampled clock */
        uint32_t ms = (uint32_t)((uint64_t)frame++ * 1000 / 60);
        Time_Set(ms);
        Timer_Sample(ms);
        Voices_Update();   /* 0x4b7a40 */
        Game_Frame();
        D3d_Present();
        if (gm8(0x663db8)) return false;   /* the quit (0x4a9a40): the message loop ends */
    }
    return true;
}

void app_audio(float *stereo, unsigned frames) { Dsb_Mix(stereo, frames); }

void app_exit(void)
{
    Text_Free();
    exe_free();
}
