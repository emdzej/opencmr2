/* The application (see app.h). For now: the exe check and an empty frame; the front end and the race come
   as they are ported (docs/re/port-plan.md, docs/guide/status.md). */
#include "app.h"
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Until the front end exists: load a car and its textures, a font and the front-end text through the
   port's decoders, so that a headless run shows they work on the target. */
static void decoder_check(void)
{
    char m[300];
    BflSlot tex = {0};
    uint8_t *model = File_Load("Game\\Cars\\205c1.c3d", 1);
    uint32_t msize = File_LastSize();
    C3d c;
    int ok = 0, n = 0;
    if (model && C3d_Load(&c, model, msize, 0) && Bfl_Mount(&tex, "Game\\Cars\\205c1.bfl")) {
        for (int i = 0; i < c.ntex; i++, n++) {
            uint32_t sz;
            const uint8_t *t = Bfl_FindTextureMember(&tex, C3d_TexPath(&c, i), &sz);
            int w, h;
            uint8_t *px = t ? Image_Decode(t, sz, &w, &h) : NULL;
            ok += px != NULL;
            free(px);
        }
        snprintf(m, sizeof m, "OpenCMR2: 205c1.c3d: %u objects, %u polygons, %u vertices; %d/%d textures", c.nobj,
                 c.npoly, c.nvert, ok, n);
        plat_log(m);
    }
    Bfl_Unmount(&tex);
    free(model);
    BflSlot fe = {0};
    if (Bfl_Mount(&fe, "CountrySpecific\\Europe\\englishtext.bfl")) {
        uint32_t sz;
        const uint8_t *f = Bfl_FindFile(&fe, "fenglish.txt", NULL, &sz, false);
        size_t n2 = sz;
        if (f) {
            Text_Build(1, 0x215, &f, &n2);
            BflSlot res = {0};
            Font font;
            if (Bfl_Mount(&res, "FrontEnd\\res640.bfl")) {
                const uint8_t *p = Bfl_FindFile(&res, "hel_15pt.pcf", NULL, &sz, false);
                if (p && Font_Parse(&font, p, sz)) {
                    snprintf(m, sizeof m, "OpenCMR2: text 0 \"%.60s\", %d pixels in hel_15pt", Text_Get(0),
                             Font_Width(&font, Text_Get(0)));
                    plat_log(m);
                }
                Bfl_Unmount(&res);
            }
        }
        Bfl_Unmount(&fe);
    }
}

void gl_dev_register(void);   /* render/gl_dev.c */

static bool viewing;

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
    Profiles_Init();     /* 0x4d15e0's start-up work */
    decoder_check();
    char car[64], stage[64];
    bool c = plat_param("car", car, sizeof car), st = plat_param("stage", stage, sizeof stage);
    if (c || st) {
        gl_dev_register();
        viewing = viewer_init(c ? car : NULL, st ? stage : NULL);   /* both: the car parked on the stage */
    }
    return true;
}

bool app_frame(void)
{
    if (viewing) viewer_frame();
    return true;
}

void app_audio(float *stereo, unsigned frames) { memset(stereo, 0, (size_t)frames * 2 * sizeof *stereo); }

void app_exit(void)
{
    Text_Free();
    exe_free();
}
