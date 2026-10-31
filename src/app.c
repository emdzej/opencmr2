/* The application (see app.h). For now: the exe check and an empty frame; the front end and the race come
   as they are ported (docs/re/port-plan.md, docs/guide/status.md). */
#include "app.h"
#include "exe.h"
#include "platform.h"
#include <stdio.h>
#include <string.h>

bool app_init(void)
{
    char err[256], m[300];
    if (!exe_init(err, sizeof err)) {
        snprintf(m, sizeof m, "OpenCMR2: %s", err);
        plat_log(m);
        return false;
    }
    plat_log("OpenCMR2: CMR2.exe v1.11 found");
    return true;
}

bool app_frame(void) { return true; }

void app_audio(float *stereo, unsigned frames) { memset(stereo, 0, (size_t)frames * 2 * sizeof *stereo); }

void app_exit(void) { exe_free(); }
