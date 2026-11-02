/* The platform contract for the native tests and tools: log to stderr, launch parameters from the
   environment (OPENCMR2_PARAM_<name>), the keyboard the tests set (plat_host_key), no display, user files in memory only. */
#include "platform.h"
#include "platform_host.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void plat_log(const char *msg) { fprintf(stderr, "%s\n", msg); }
uint32_t plat_pad(int player) { return 0; }

/* the tests' keyboard (platform_host.h): the keys held this frame and the presses to report */
static uint8_t held[KEY_COUNT];
static uint8_t presses[32];
static int npresses;
static bool have_keys;
void plat_host_key(int code, bool down)
{
    have_keys = true;
    if (down && !held[code] && npresses < 32) presses[npresses++] = (uint8_t)code;
    held[code] = down;
}

bool plat_keys(uint8_t keys[KEY_COUNT])
{
    memcpy(keys, held, KEY_COUNT);
    return have_keys;
}

int plat_key_presses(uint8_t *codes, int cap)
{
    int n = npresses < cap ? npresses : cap;
    memcpy(codes, presses, (size_t)n);
    npresses = 0;
    return n;
}

bool plat_param(const char *name, char *dst, size_t cap)
{
    char env[96] = "OPENCMR2_PARAM_";
    size_t n = strlen(env);
    for (const char *p = name; *p && n + 1 < sizeof env; p++) env[n++] = (char)toupper((unsigned char)*p);
    env[n] = 0;
    const char *v = getenv(env);
    if (!v) return false;
    snprintf(dst, cap, "%s", v);
    return true;
}

void plat_set_frame_rate(double hz) {}
void plat_display_size(int *w, int *h) { *w = 640; *h = 480; }
bool plat_frame_shown(void) { return true; }
uint8_t *plat_load_user_file(const char *name, size_t *size) { return NULL; }
bool plat_save_user_file(const char *name, const void *data, size_t size) { return false; }
bool plat_user_file_name(int i, char *name, size_t cap) { (void)i, (void)name, (void)cap; return false; }
