/* The fixed-point tables (Math_InitTables 0x4b7b20): the constants they are built from against the
   exe's, and values that follow from the definitions. */
#include "exe.h"
#include "fixmath.h"
#include "vfs.h"
#include "vfs_host.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static double exe_f64(uint32_t va)
{
    uint64_t u = exe_u32(va) | (uint64_t)exe_u32(va + 4) << 32;
    double d;
    memcpy(&d, &u, 8);
    return d;
}

int main(void)
{
    char err[200];
    if (vfs_mount_default() && exe_init(err, sizeof err)) {
        CHECK(exe_f64(0x511d08) == 2.0 * M_PI / 4096.0);
        CHECK(exe_f64(0x511d00) == 1.0 / 4095.0);
        CHECK(exe_f64(0x511cf8) == 1.0 / 511.0);
        CHECK(exe_f64(0x511380) == 1.0 / (32.0 * M_PI));
        CHECK(exe_f64(0x511310) == 1.0 / 65536.0 && exe_f64(0x5112e0) == 65536.0);
    } else {
        printf("math_test: no exe, constants not checked\n");
    }
    Math_InitTables();
    CHECK(g_sin[0] == 0 && g_sin[1024] == 65536 && g_sin[2048] == 0 && g_sin[3072] == -65536);
    CHECK(g_sin[512] == 46341);                    /* sqrt(1/2) * 65536 = 46340.95 */
    CHECK(g_tan[512] == 65536 && g_tan[0x400] == 0x7fffffff && g_tan[0xc00] == 0x7fffffff);
    CHECK(g_sqrt[0] == 724 && g_sqrt[4095] == 65532);   /* sqrt(8 / 65536), sqrt(65528 / 65536) */
    CHECK(g_asin[0] == 0 && g_asin[4095] == 1024);
    CHECK(g_atan[0] == 0 && g_atan[511] == 512);
    int odd = 0;
    for (int i = 0; i < 4096; i++) odd += g_sin[i] != -g_sin[(4096 - i) & 0xfff];
    CHECK(odd == 0);
    FxMat m;
    Mat_RotZ(&m, 1024);   /* a quarter turn: x to y */
    FxVec v = {10 << 16, 0, 0}, o;
    Vec_Transform(&o, &v, &m);
    CHECK(o.x == 0 && o.y == 10 << 16);
    printf("math_test: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
