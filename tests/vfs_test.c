/* File layer and cabinet against the user's CD (OPENCMR2_DATA: the CD folder or CMR2.ISO; default ./cd).
   Checks names in any case and with either separator, listing, the cabinet layer (CMR2.exe and the demo
   runs exist only in data1.cab/data2.cab), and the exe check. Skips (exit 0) without data. */
#include "bfl.h"
#include "exe.h"
#include "vfs.h"
#include "vfs_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void count(const char *name, bool is_dir, void *user) { (void)name; (void)is_dir; ++*(int *)user; }

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists("Game/Bigfiles/common.bfl")) {
        printf("vfs_test: no game data, skipped\n");
        return 0;
    }
    printf("mounted: %s\n", vfs_describe());
    CHECK(vfs_exists("game\\bigfiles\\COMMON.BFL"));                 /* any case, either separator */
    CHECK(!vfs_exists("Game/Bigfiles"));                            /* a directory is not a file */
    CHECK(vfs_exists("Game/Tracks/Japan/CPU_runs/champend.rpl"));
    int k = 0;
    CHECK(vfs_list("Game/Cars", count, &k) && k > 400);

    /* From the cabinet: the program, the demo runs (34,702-byte replays, docs/formats/stage.md). */
    char err[256];
    bool ok = exe_init(err, sizeof err);
    CHECK(ok);
    if (!ok) printf("%s\n", err);
    k = 0;
    CHECK(vfs_list("Game/DemoRuns", count, &k) && k == 6);
    uint8_t *r = File_Load("Game\\DemoRuns\\Fra01H.rpl", 1);
    CHECK(r && File_LastSize() == 34702);
    free(r);
    k = 0;
    CHECK(vfs_list("", count, &k));
    printf("root: %d entries; exe .rdata starts \"%.8s\"\n", k, (const char *)exe_data(0x511000, 8));
    printf("vfs_test: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
