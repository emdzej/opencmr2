/* The exe as the port reads it (exe.h): the user's CMR2.exe, found through the cabinet layer, and the first
   tables read from it at run time: the race process states (0x5190b0: 14 {update, draw} pairs, code
   addresses or 0, docs/re/architecture.md) and the cheat codes (0x5250fc: 20 pointers to lower-case
   strings, two with spaces, matched by 0x4eb290). Nothing from the exe is stored in the source. Skips (exit 0) without
   data. */
#include "exe.h"
#include "vfs.h"
#include "vfs_host.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static bool in_text(uint32_t va) { return va >= 0x401000 && va < 0x511000; }

int main(void)
{
    char err[256];
    if (!vfs_mount_default() || !exe_init(err, sizeof err)) {
        printf("exe_test: no CMR2.exe, skipped\n");
        return 0;
    }
    int states = 0;
    for (int i = 0; i < 14; i++) {
        uint32_t upd = exe_u32(0x5190b0 + 8 * i), draw = exe_u32(0x5190b4 + 8 * i);
        CHECK(in_text(upd));                       /* every state has an update */
        CHECK(!draw || in_text(draw));
        states += in_text(upd);
    }
    CHECK(exe_u32(0x5190b0 + 7 * 8) == 0x41d2b0);   /* state 7, the stage running: FUN_0041d2b0 */
    int cheats = 0;
    for (int i = 0; i < 20; i++) {
        const char *s = exe_str(exe_u32(0x5250fc + 4 * i));
        bool ok = *s != 0;
        for (const char *p = s; *p; p++) ok &= islower((unsigned char)*p) || isdigit((unsigned char)*p) || *p == ' ';
        CHECK(ok);
        cheats += ok;
    }
    CHECK(exe_data(0x832000 + 0x2000, 1) == NULL);  /* past the last section */
    printf("race states: %d, cheat codes: %d\n", states, cheats);
    printf("exe_test: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
