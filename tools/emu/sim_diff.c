/* Differential test of the port's simulation against the original's machine code: the disc's demo run
   (Game/DemoRuns/<stage>H.rpl) is played through the port as tests/replay_test.c does, and before
   each Sim_StepCars the same state is put into an x86 emulator (Unicorn) holding CMR2.exe's image,
   whose own Sim_StepCars 0x433890 then runs on it; after the port's, the car blocks are compared.

   The emulator's memory: the exe's headers, code and constants at 0x400000-0x515fff; the port's game
   state image (the original's data section, mem.h) at 0x516000-0x831fff with the port's math tables
   written at their original addresses; the cars, their damage records and the car list on a heap at
   0x10000000; a stack. An access outside them stops the run with its address.

   Build: cmake -S . -B build -DOPENCMR2_UNICORN=<dir with include/ and lib/> (the Python wheel's
   unicorn/ directory has both), then build/sim_diff [stage]. A development tool, not a test. */
#include "crt.h"
#include "exe.h"
#include "fixmath.h"
#include "game/car_load.h"
#include "game/gstate.h"
#include "game/mem.h"
#include "game/profile.h"
#include "game/race.h"
#include "game/replay.h"
#include "game/select.h"
#include "game/stage_load.h"
#include "game/timer.h"
#include "game/weather.h"
#include "audio/snd.h"
#include "render/d3d7.h"
#include "render/mesh.h"
#include "sim/car.h"
#include "sim/damage.h"
#include "sim/fade.h"
#include "sim/progress.h"
#include "sim/scenery.h"
#include "sim/sim.h"
#include "sim/walls.h"
#include "vfs_host.h"
#include "world/fin.h"
#include "world/hpc.h"
#include "world/obj.h"
#include "world/stage.h"
#include "world/section.h"
#include "world/tod.h"
#include <unicorn/unicorn.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { IMG = 0x400000, DATA = 0x516000, DATA_END = 0x832000, IMG_END = 0x834000 };
enum { HEAP = 0x10000000, HEAP_SIZE = 0x200000, CARS = HEAP, DMG = HEAP + 0x10000, LIST = HEAP + 0x20000, WX = HEAP + 0x30000, LOGS = HEAP + 0x40000, SECS = HEAP + 0x100000, COLL = HEAP + 0x60000, HPCB = 0x20000000, WALLB = 0x21000000, WALLB_SIZE = 0x100000, MESHB = 0x22000000, MESHB_SIZE = 0x800000, SCNB = 0x23000000, SCNB_SIZE = 0x1000000, OBJB = 0x24000000, REPB = 0x25000000, REPB_SIZE = 0x800000 };
enum { SLOTS = 15 };
enum { STACK = 0x00200000, STACK_SIZE = 0x100000, RET = 0x00100000 };

static uc_engine *uc;
static uint8_t *img, *data, *heap;
static const char *stage = "fra01";
static int step_no, mismatches, checked;
static bool quiet;   /* QUIET: only the summary of each pass's differing steps */
static struct { const char *name; int n, first; } tally[8];
static void count(const char *name)
{
    int k = 0;
    while (k < 8 && tally[k].name && strcmp(tally[k].name, name)) k++;
    if (k == 8) return;
    if (!tally[k].name) tally[k].name = name, tally[k].first = step_no;
    tally[k].n++;
}
static bool stop;

static void fail_uc(uc_err e, const char *what)
{
    if (e == UC_ERR_OK) return;
    fprintf(stderr, "sim_diff: %s: %s\n", what, uc_strerror(e));
    exit(2);
}

static uint32_t last_pc[8];
static uint32_t emu_seed;   /* the original's rand (msvcrt's, 0x4c6676) on the port's state at each load */
static int last_i;
static bool on_unmapped(uc_engine *u, uc_mem_type type, uint64_t addr, int size, int64_t value, void *user)
{
    (void)user, (void)value;
    uint32_t eip;
    uc_reg_read(u, UC_X86_REG_EIP, &eip);
    fprintf(stderr, "sim_diff: step %d: %s of %d bytes at %08llx (eip %08x)\n", step_no,
            type == UC_MEM_READ_UNMAPPED ? "read" : type == UC_MEM_WRITE_UNMAPPED ? "write" : "fetch", size,
            (unsigned long long)addr, eip);
    fprintf(stderr, "  the last instructions:");
    for (int k = 1; k <= 8; k++) fprintf(stderr, " %06x", last_pc[(last_i - k) & 7]);
    fprintf(stderr, "\n");
    stop = true;
    return false;
}

/* the passes' entries in the original (Sim_StepCars calls them per car) and the port's probes before
   them, by the probe's id */
typedef struct { int id; uint32_t fn; const char *name; } Pass;
static const Pass passes[] = {
    {110, 0x43b090, "Car_Coefficients"}, {111, 0x43b020, "Car_DamageLatch"}, {112, 0x4781d0, "Car_SurfaceGrip"},
    {113, 0x4340f0, "Car_ClutchCounter"}, {114, 0x46b4e0, "Damage_Misfire"}, {115, 0x493520, "Car_Controls"},
    {116, 0x434f50, "Car_CornerVelocities"}, {117, 0x432b30, "Car_WheelLoad"}, {118, 0x43b100, "Car_Suspension"},
    {119, 0x434070, "Car_TiltFactor"}, {120, 0x4348c0, "Car_GroundContact"}, {121, 0x434140, "Car_WheelHeading"},
    {102, 0x437dc0, "Car_Drivetrain"}, {103, 0x438460, "Car_CornerImpulse"}, {104, 0x435470, "Car_Integrate"},
    {105, 0x4380c0, "Car_Revs"},
};
enum { NPASS = sizeof passes / sizeof passes[0] };
static uint8_t emu_at[NPASS][CAR_SIZE], port_at[NPASS][CAR_SIZE];
static bool emu_seen[NPASS], port_seen[NPASS];

static uint32_t *reads;   /* READS: per 4 bytes of the data section, reads during the steps */
static void on_read(uc_engine *u, uc_mem_type type, uint64_t addr, int size, int64_t value, void *user)
{
    (void)u, (void)type, (void)size, (void)value, (void)user;
    if (addr >= DATA && addr < DATA_END) reads[(addr - DATA) / 4]++;
}

static void on_code(uc_engine *u, uint64_t addr, uint32_t size, void *user)
{
    (void)size, (void)user;
    last_pc[last_i++ & 7] = (uint32_t)addr;
    /* the original's calls out of the simulation, returned at once: Voice_Play (stdcall, 6 arguments:
       no voice, -1, as the port's without a device) */
    static const struct { uint32_t fn, pop, eax; } stubs[] = {{0x4b7790, 0x18, 0xffffffff}};
    if (addr == 0x4c6676) {
        uint32_t sp, ret, v;
        emu_seed = emu_seed * 214013u + 2531011u;
        v = emu_seed >> 16 & 0x7fff;
        uc_reg_read(u, UC_X86_REG_ESP, &sp);
        uc_mem_read(u, sp, &ret, 4);
        sp += 4;
        uc_reg_write(u, UC_X86_REG_ESP, &sp);
        uc_reg_write(u, UC_X86_REG_EAX, &v);
        uc_reg_write(u, UC_X86_REG_EIP, &ret);
        return;
    }
    for (unsigned k = 0; k < sizeof stubs / sizeof stubs[0]; k++)
        if (addr == stubs[k].fn) {
            uint32_t sp, ret;
            uc_reg_read(u, UC_X86_REG_ESP, &sp);
            uc_mem_read(u, sp, &ret, 4);
            sp += 4 + stubs[k].pop;
            uc_reg_write(u, UC_X86_REG_ESP, &sp);
            uc_reg_write(u, UC_X86_REG_EAX, &stubs[k].eax);
            uc_reg_write(u, UC_X86_REG_EIP, &ret);
            return;
        }
    static uint32_t trace[16];
    static int ntrace = -1, tstep;
    if (ntrace < 0) {
        ntrace = 0;
        char *e = getenv("TRACE");
        tstep = getenv("TRACE_STEP") ? atoi(getenv("TRACE_STEP")) : 0;
        while (e && *e && ntrace < 16) {
            trace[ntrace++] = (uint32_t)strtoul(e, &e, 16);
            if (*e == ',') e++;
        }
    }
    if (step_no == tstep)
        for (int k = 0; k < ntrace; k++)
            if (addr == trace[k]) {
                uint32_t r[4];
                uc_reg_read(u, UC_X86_REG_EAX, &r[0]), uc_reg_read(u, UC_X86_REG_ECX, &r[1]);
                uc_reg_read(u, UC_X86_REG_EDX, &r[2]), uc_reg_read(u, UC_X86_REG_ESI, &r[3]);
                if (addr == 0x48fbec) {
                    uint8_t b[4];
                    int32_t back, dist[4];
                    uc_mem_read(u, 0x591944, b, 2), uc_mem_read(u, 0x591934, b + 2, 2);
                    uc_mem_read(u, 0x59192c, &back, 4), uc_mem_read(u, 0x5919a4, dist, 16);
                    fprintf(stderr, "    original classify: back %d nin %d nout %d nneg %d npos %d dist %x %x %x %x\n", back, b[0], b[1], b[2], b[3], dist[0], dist[1], dist[2], dist[3]);
                }
                fprintf(stderr, "    trace %06x: eax %08x ecx %08x edx %08x esi %08x\n", (unsigned)addr, r[0], r[1], r[2], r[3]);
            }
    for (int k = 0; k < NPASS; k++)
        if (addr == passes[k].fn && !emu_seen[k]) {
            uint32_t car;
            uc_mem_read(u, 0x53cc18, &car, 4);
            uc_mem_read(u, car, emu_at[k], CAR_SIZE);
            emu_seen[k] = true;
        }
}

static void put_table(uint32_t va, const void *p, size_t n) { memcpy(data + (va - DATA), p, n); }
static void put32(uint8_t *base, uint32_t off, uint32_t v) { memcpy(base + off, &v, 4); }

static void emu_init(void)
{
    fail_uc(uc_open(UC_ARCH_X86, UC_MODE_32, &uc), "open");
    img = calloc(DATA - IMG, 1);
    data = calloc(IMG_END - DATA, 1);
    heap = calloc(HEAP_SIZE, 1);
    /* the headers and the sections below the data, as loaded */
    const uint8_t *h = exe_data(IMG, 0x400);
    if (h) memcpy(img, h, 0x400);
    for (uint32_t va = 0x401000; va < DATA; va += 0x1000) {
        const uint8_t *p = exe_data(va, 0x1000);
        if (p) memcpy(img + (va - IMG), p, 0x1000);
        else
            for (uint32_t k = 0; k < 0x1000; k += 4) {   /* a section's tail past its raw data */
                const uint8_t *q = exe_data(va + k, 4);
                if (q) memcpy(img + (va - IMG) + k, q, 4);
            }
    }
    fail_uc(uc_mem_map_ptr(uc, IMG, DATA - IMG, UC_PROT_ALL, img), "map image");
    fail_uc(uc_mem_map_ptr(uc, DATA, IMG_END - DATA, UC_PROT_READ | UC_PROT_WRITE, data), "map data");
    fail_uc(uc_mem_map_ptr(uc, HEAP, HEAP_SIZE, UC_PROT_READ | UC_PROT_WRITE, heap), "map heap");
    fail_uc(uc_mem_map(uc, STACK, STACK_SIZE, UC_PROT_READ | UC_PROT_WRITE), "map stack");
    fail_uc(uc_mem_map(uc, RET, 0x1000, UC_PROT_ALL), "map return page");
    uc_hook hk;
    fail_uc(uc_hook_add(uc, &hk, UC_HOOK_MEM_UNMAPPED, (void *)on_unmapped, NULL, 1, 0), "hook");
    if (getenv("READS")) {
        reads = calloc((DATA_END - DATA) / 4, sizeof *reads);
        uc_hook hr;
        fail_uc(uc_hook_add(uc, &hr, UC_HOOK_MEM_READ, (void *)on_read, NULL, DATA, DATA_END - 1), "read hook");
    }
    uc_hook hc;
    fail_uc(uc_hook_add(uc, &hc, UC_HOOK_CODE, (void *)on_code, NULL, 0x401000, 0x511000), "code hook");
}

static void emu_call(uint32_t fn, const uint32_t *args, int nargs);
static uint32_t rep_at[16], rep_size[16];
static bool rep_run;
static uint32_t slot_at[CAR_MAX][SLOTS];
static int slot_n[CAR_MAX][SLOTS];

/* the port's state into the emulator */
static bool free_run, free_loaded;   /* FREE: the original's own state after the first load */
static void emu_load(const int16_t *list, int n)
{
    if (free_run && free_loaded) return;
    memcpy(data, gm(DATA), DATA_END - DATA);
    emu_seed = crt_seed();
    put_table(0x6e2ef4, g_sin, sizeof g_sin);
    put_table(0x6e93f4, g_tan, sizeof g_tan);
    put_table(0x6e0ef4, g_sqrt, sizeof g_sqrt);
    put_table(0x6e6ef4, g_asin, sizeof g_asin);
    put_table(0x6e8ff4, g_atan, sizeof g_atan);
    memset(heap, 0, HEAP_SIZE);
    for (int i = 0; i < CAR_MAX; i++) {
        uint8_t *cb = heap + (CARS - HEAP) + (uint32_t)i * CAR_SIZE;
        memcpy(cb, Car_Get(i)->s, CAR_SIZE);
        put32(cb, 0x750, CARS + (uint32_t)i * CAR_SIZE);   /* the orientation: the car's own first rows */
        put32(cb, 0x754, CARS + (uint32_t)i * CAR_SIZE + 0x40);   /* the drawn body: its +0x40 */
        memcpy(heap + (DMG - HEAP) + (uint32_t)i * 0x4d0, Damage_Get(i), 0x4d0);
        put32(data, 0x588990 + 4 * (uint32_t)i - DATA, DMG + (uint32_t)i * 0x4d0);   /* Damage_Get's table */
        put32(data, 0x53aba8 + 4 * (uint32_t)i - DATA, CARS + (uint32_t)i * CAR_SIZE);  /* Car_Get's */
    }
    put32(data, 0x53c9a4 - DATA, CARS);
    /* the sections: their records (x, y, z at +0, +4, +8; +0x24 the walls' count, +0x28 the first)
       behind the table 0x71f608, the grid; the walls (fin.dat's 0x30-byte records, +0x28 the next) */
    static uint8_t *walls;
    if (!walls) {
        walls = calloc(WALLB_SIZE, 1);
        fail_uc(uc_mem_map_ptr(uc, WALLB, WALLB_SIZE, UC_PROT_READ | UC_PROT_WRITE, walls), "map walls");
    }
    for (int i = 0; i < g_nsec; i++) {
        uint8_t *r = heap + (SECS - HEAP) + 0x88 * (uint32_t)i;
        memcpy(r, g_sec[i].rec, 12);
        put32(r, 0x24, (uint32_t)g_sec[i].nwalls);
        put32(r, 0x28, g_sec[i].walls ? WALLB + 0x30 * (uint32_t)(g_sec[i].walls - g_fin) : 0);
        put32(data, 0x71f608 + 4 * (uint32_t)i - DATA, SECS + 0x88 * (uint32_t)i);
    }
    for (int i = 0; i < g_nfin && 0x30 * (i + 1) <= WALLB_SIZE; i++) {
        const Wall *w = &g_fin[i];
        uint8_t *r = walls + 0x30 * i;
        memcpy(r, w->a, 12), memcpy(r + 0xc, w->b, 12), memcpy(r + 0x18, w->n, 12);
        put32(r, 0x24, (uint32_t)w->x24);
        put32(r, 0x28, w->next ? WALLB + 0x30 * (uint32_t)(w->next - g_fin) : 0);
        r[0x2c] = w->type, r[0x2d] = w->flags, r[0x2e] = w->cap[0], r[0x2f] = w->cap[1];
    }
    put32(data, 0x72d254 - DATA, WALLB), put32(data, 0x72d250 - DATA, (uint32_t)g_nfin);
    int32_t gr[5];
    Section_Grid(gr);
    put32(data, 0x72d24c - DATA, (uint32_t)gr[0]), put32(data, 0x72d248 - DATA, (uint32_t)gr[1]);
    put32(data, 0x72d55c - DATA, (uint32_t)gr[2]), put32(data, 0x6ef5f0 - DATA, (uint32_t)gr[3]);
    put32(data, 0x6ef5f4 - DATA, (uint32_t)gr[4]), put32(data, 0x71f600 - DATA, (uint32_t)g_sec_row);
    put32(data, 0x72d568 - DATA, (uint32_t)g_nsec), put32(data, 0x72d458 - DATA, (uint32_t)g_sec_rows);
    put32(data, 0x588b94 - DATA, DMG);    /* the damage records' base */
    put32(data, 0x588b98 - DATA, LOGS);   /* the hit logs, 0x290 a car */
    for (int i = 0; i < CAR_MAX; i++) memcpy(heap + (LOGS - HEAP) + 0x290 * (uint32_t)i, Damage_Log(i), 0x290);
    /* the weather's records (0x543ecc, 0xc bytes a car: +8 the wet value) */
    put32(data, 0x543ecc - DATA, WX);
    for (int i = 0; i < CAR_MAX; i++) put32(heap, WX - HEAP + 0xc * (uint32_t)i + 8, (uint32_t)Weather_CarWet(Car_Get(i)));
    memcpy(heap + (LIST - HEAP), list, sizeof(int16_t) * (size_t)n);
    /* the cars' two model objects (+0x71c, +0x720): the first's matrix (+0x98), the second's passes
       (+0x17c) */
    static uint8_t *ob;
    if (!ob) {
        ob = calloc(0x400 * CAR_MAX, 1);
        fail_uc(uc_mem_map_ptr(uc, OBJB, (0x400 * CAR_MAX + 0xfff) & ~0xfffu, UC_PROT_READ | UC_PROT_WRITE, ob), "map objects");
    }
    for (int i = 0; i < CAR_MAX; i++) {
        const Car *c = Car_Get(i);
        uint8_t *cb = heap + (CARS - HEAP) + (uint32_t)i * CAR_SIZE, *m = ob + 0x400 * i, *sd = m + 0x200;
        memset(m, 0, 0x400);
        if (c->main) memcpy(m + 0x98, c->main->local.m, 0x40);
        if (c->second) sd[0x17c] = c->second->mask;
        put32(cb, 0x71c, c->main ? OBJB + 0x400 * (uint32_t)i : 0);
        put32(cb, 0x720, c->second ? OBJB + 0x400 * (uint32_t)i + 0x200 : 0);
    }
    /* the cars' flat boxes (0x590ed0, 0x98 bytes a car) */
    for (int i = 0; i < CAR_MAX; i++) memcpy(data + 0x590ed0 + 0x98 * (uint32_t)i - DATA, Walls_BoxRec(i), 0x98);
    /* the scenery's collision tables */
    static uint8_t *sb;
    if (!sb) {
        sb = calloc(SCNB_SIZE, 1);
        fail_uc(uc_mem_map_ptr(uc, SCNB, SCNB_SIZE, UC_PROT_READ | UC_PROT_WRITE, sb), "map scenery");
    }
    {
        static const uint32_t ga[14] = {0x58c940, 0x58c93c, 0x58ca78, 0x58c954, 0x58c960, 0x58c964, 0x58c948,
                                        0x58c94c, 0x58c95c, 0x58ca6c, 0x58ca70, 0x58c944, 0x58c938, 0x58c958};
        uint32_t g[14];
        if (!Scenery_CollImage(sb, SCNB_SIZE, SCNB, g)) fprintf(stderr, "sim_diff: no scenery tables\n");
        for (int k = 0; k < 14; k++) put32(data, ga[k] - DATA, g[k]);
    }
    /* the fades (the port keeps them outside the state image); a callback as a nonzero marker */
    for (int i = 0; i < 8; i++) {
        int32_t f[8];
        Fade_State(i, f);
        uint32_t o = 4 * (uint32_t)i - DATA;
        put32(data, 0x53a0ec + o, (uint32_t)f[0]), put32(data, 0x53a04c + o, (uint32_t)f[1]);
        put32(data, 0x53a02c + o, (uint32_t)f[2]), put32(data, 0x53a08c + o, f[3] ? 1u : 0u);
        put32(data, 0x53a0ac + o, (uint32_t)f[4]), put32(data, 0x53a06c + o, (uint32_t)f[5]);
        put32(data, 0x53a20c + o, (uint32_t)f[6]), put32(data, 0x53a0cc + o, (uint32_t)f[7]);
    }
    /* the damage slots (the record's +0x00 meshes, +0x3c objects, +0x78 rest vertices): per slot a copy
       of the mesh's head with its vertices (+0x0c) after it, a zeroed object, the rest vertices */
    static uint8_t *mb;
    if (!mb) {
        mb = calloc(MESHB_SIZE, 1);
        fail_uc(uc_mem_map_ptr(uc, MESHB, MESHB_SIZE, UC_PROT_READ | UC_PROT_WRITE, mb), "map meshes");
    }
    memset(mb, 0, MESHB_SIZE);
    uint32_t at = 0;
    for (int i = 0; i < CAR_MAX; i++)
        for (int k = 0; k < SLOTS; k++) {
            uint8_t *m, *v, *r;
            int nv;
            uint8_t *rec = heap + (DMG - HEAP) + (uint32_t)i * 0x4d0;
            if (!Damage_SlotData(i, k, &m, &v, &nv, &r) || !v) continue;
            uint32_t need = 0x200 + 0x200 + (uint32_t)nv * (0x30 + 0x20);
            if (at + need > MESHB_SIZE) continue;
            uint32_t me = MESHB + at, ob = me + 0x200, vx = ob + 0x200, rs = vx + (uint32_t)nv * 0x30;
            memcpy(mb + at, m, 0x120);
            put32(mb + at, 0x0c, vx);
            memcpy(mb + (vx - MESHB), v, (size_t)nv * 0x30);
            if (r) memcpy(mb + (rs - MESHB), r, (size_t)nv * 0x20);
            slot_at[i][k] = vx, slot_n[i][k] = nv;
            put32(rec, 4 * (uint32_t)k, me), put32(rec, 0x3c + 4 * (uint32_t)k, ob), put32(rec, 0x78 + 4 * (uint32_t)k, r ? rs : 0);
            at += need;
        }
    /* the collision mesh: its image, the original's Hpc_Parse 0x490d50 setting its pointers */
    uint32_t hs;
    const uint8_t *h = Hpc_Image(&hs);
    static bool hpc_mapped;
    if (h && !hpc_mapped) {
        uint32_t len = (hs + 0xfff) & ~0xfffu;
        fail_uc(uc_mem_map(uc, HPCB, len, UC_PROT_READ), "map hpc");
        uc_mem_write(uc, HPCB, h, hs);
        hpc_mapped = true;
    }
    if (h) {
        uint32_t a[1] = {HPCB};
        emu_call(0x490d50, a, 1);
    }
}

static void emu_call(uint32_t fn, const uint32_t *args, int nargs)
{
    uint32_t sp = STACK + STACK_SIZE - 0x100;
    for (int i = nargs - 1; i >= 0; i--) sp -= 4, uc_mem_write(uc, sp, &args[i], 4);
    uint32_t ret = RET;
    sp -= 4, uc_mem_write(uc, sp, &ret, 4);
    uc_reg_write(uc, UC_X86_REG_ESP, &sp);
    uint32_t z = 0;
    uc_reg_write(uc, UC_X86_REG_EBP, &z);
    stop = false;
    uc_err e = uc_emu_start(uc, fn, RET, 0, 50000000);
    if (e != UC_ERR_OK && !stop) fprintf(stderr, "sim_diff: step %d: %s\n", step_no, uc_strerror(e));
    if (e != UC_ERR_OK) stop = true;
}

/* Car_Integrate's parts: from the port's state before it, the original's SteerBody, Tilt and the torque
   reaction one after the other, then its whole Car_Integrate; against the port's probes */
static bool sub_on;
static uint8_t sub_emu[4][CAR_SIZE], sub_port[3][CAR_SIZE];
static const uint32_t sub_fn[3] = {0x436630, 0x4373c0, 0x437d70};
static const char *sub_name[4] = {"Car_SteerBody", "Car_Tilt", "Car_EngineTorqueReaction", "Car_Integrate"};
static int ndiff(const uint8_t *a, const uint8_t *b, bool show, const char *what);
static bool pointer_field(uint32_t o) { return o >= 0x71c && o < 0x758; }

static void emu_select(int car)
{
    uint32_t cp = CARS + (uint32_t)car * CAR_SIZE, dp = DMG + (uint32_t)car * 0x4d0;
    uc_mem_write(uc, 0x53cc18, &cp, 4);
    uc_mem_write(uc, 0x53cc1c, &dp, 4);
}

static void sub_start(int car)
{
    static const int16_t one[1] = {0};
    bool keep = stop;
    emu_load(one, 0);
    emu_select(car);
    for (int k = 0; k < 3; k++) {
        emu_call(sub_fn[k], NULL, 0);
        memcpy(sub_emu[k], heap + (CARS - HEAP) + (uint32_t)car * CAR_SIZE, CAR_SIZE);
    }
    emu_load(one, 0);
    emu_select(car);
    emu_call(0x435470, NULL, 0);
    memcpy(sub_emu[3], heap + (CARS - HEAP) + (uint32_t)car * CAR_SIZE, CAR_SIZE);
    stop = keep;
}

static void sub_check(int car)
{
    for (int k = 0; k < 4; k++) {
        const uint8_t *p = k < 3 ? sub_port[k] : Car_Get(car)->s;
        int m = ndiff(sub_emu[k], p, true, sub_name[k]);
        printf("  after %s: %d ints differ\n", sub_name[k], m);
        if (m) break;
    }
    sub_on = false;
}

/* the pointer fields the port keeps beside the block */

static int ndiff(const uint8_t *a, const uint8_t *b, bool show, const char *what)
{
    int m = 0;
    for (uint32_t o = 0; o < CAR_SIZE; o += 4)
        if (!(o >= 0x71c && o < 0x758) && memcmp(a + o, b + o, 4)) {
            int32_t x, y;
            memcpy(&x, a + o, 4), memcpy(&y, b + o, 4);
            if (show && m < 16) printf("    after %s +0x%03x: original %08x port %08x (%+d)\n", what, o, (unsigned)x, (unsigned)y, y - x);
            m++;
        }
    return m;
}

/* the original's passes after Sim_StepCars, each from the port's state before the port's own */
/* args: 0 none, 1 (cars, the collision list, n), 2 (cars, list, n), 3 (list, n) */
typedef struct { int id; uint32_t fn, fn2; const char *name; int args; } Later;
static const Later later[] = {
    {1, 0x4877a0, 0, "Sim_Collide", 1}, {2, 0x470580, 0, "Knock_Step", 0}, {3, 0x42cd00, 0, "Sim_CarsPost", 2},
    {4, 0x42b4a0, 0x42af50, "Pose_Spin, Pose_Capture", 3}, {5, 0, 0, "Parts_Step (not run: the parts' objects are not mirrored)", 2},
    {6, 0x469e40, 0, "Damage_Scrapes", 2},
};
enum { NLATER = sizeof later / sizeof later[0] };

static void compare(const char *after, const int16_t *list, int n);
static void compare_car_only(const char *after, const int16_t *list, int n)
{
    for (int i = 0; i < n; i++) {
        const uint8_t *e = heap + (CARS - HEAP) + (uint32_t)list[i] * CAR_SIZE, *c = Car_Get(list[i])->s;
        for (uint32_t o = 0; o < CAR_SIZE; o += 4) {
            if (pointer_field(o) || o == 0x750 || o == 0x754) continue;
            if (memcmp(e + o, c + o, 4)) {
                int32_t a, b;
                memcpy(&a, e + o, 4), memcpy(&b, c + o, 4);
                printf("  step %d car %d +0x%03x after %s: original %08x port %08x\n", step_no + 1, list[i], o, after, (unsigned)a, (unsigned)b);
                count(after);
            }
        }
    }
}

static void probe(int where, const int16_t *list, int n)
{
    if (stop) return;
    for (int k = 0; k < NPASS; k++)
        if (passes[k].id == where && !port_seen[k]) {
            memcpy(port_at[k], Car_Get(list[0])->s, CAR_SIZE);
            port_seen[k] = true;
            if (where == 104 && sub_on) sub_start(list[0]);
            if (where == 105 && sub_on) sub_check(list[0]);
            return;
        }
    if (where >= 100 && where < 200) return;
    if (where >= 201 && where <= 203) {
        if (sub_on) memcpy(sub_port[where - 201], Car_Get(list[0])->s, CAR_SIZE);
        return;
    }
    if ((where == 8 || where == 0) && getenv("CTRL") && step_no < 60) {
        const uint8_t *q = Car_Get(list[0])->s + 0x1d0;
        printf("ctrl %s step %d:", where == 8 ? "before" : "after ", where == 8 ? step_no + 1 : step_no + 1);
        for (int k = 0; k < 0x18; k++) printf(" %02x", q[k]);
        printf("\n");
    }
    if (where == 8) {
        /* the original's Replay_PlayStep 0x46d270 on copies of the replays (0x588d40: 16 pointers to
           the made and loaded tables' slots, 0x588e80 and 0x588ea0) */
        emu_load(list, n);
        static uint8_t *rb;
        if (free_run && free_loaded) goto run_play;
        if (!rb) {
            rb = calloc(REPB_SIZE, 1);
            fail_uc(uc_mem_map_ptr(uc, REPB, REPB_SIZE, UC_PROT_READ | UC_PROT_WRITE, rb), "map replays");
        }
        uint32_t at = 0;
        for (int i = 0; i < 16; i++) {
            uint32_t slot = i < 8 ? 0x588e80 + 4 * (uint32_t)i : 0x588ea0 + 4 * (uint32_t)(i - 8);
            put32(data, 0x588d40 + 4 * (uint32_t)i - DATA, slot);
            put32(data, slot - DATA, 0);
            rep_at[i] = 0;
            Replay *r = Replay_Slot(i);
            uint32_t sz;
            const uint8_t *img = r ? Replay_Image(r, &sz) : NULL;
            if (!img || at + sz > REPB_SIZE) continue;
            uint32_t so, fo, co, base = REPB + at;
            Replay_Layout(r, &so, &fo, &co);
            memcpy(rb + at, img, sz);
            int16_t seg;
            memcpy(&seg, img + 0x108, 2);
            put32(rb + at, 0x24, base + so), put32(rb + at, 0x28, base + so + 0x114c * (uint32_t)seg);
            put32(rb + at, 0x3c, base + fo), put32(rb + at, 0x104, base + co);
            put32(data, slot - DATA, base);
            rep_at[i] = base, rep_size[i] = sz;
            at = (at + sz + 0xfff) & ~0xfffu;
        }
    run_play:
        if (free_run && free_loaded) {
            /* the step's controls as set before the replay's step, the steps due, the fraction */
            for (int i = 0; i < n; i++) {
                uint8_t *cb = heap + (CARS - HEAP) + (uint32_t)list[i] * CAR_SIZE;
                const Car *c = Car_Get(list[i]);
                memcpy(cb + 0x1d0, c->s + 0x1d0, 0x14);
                memcpy(cb + 0xa90, c->s + 0xa90, 4);
                cb[0xb43] = c->s[0xb43];
            }
        }
        if (free_run) free_loaded = true;
        emu_call(0x46d270, NULL, 0);
        rep_run = !stop && !free_run;
        stop = false;
        return;
    }
    static uint8_t end_car[CAR_SIZE], end_dmg[0x4d0];
    static uint32_t end_seed;
    static bool have_end;
    if (where == 7) {
        memcpy(end_car, Car_Get(list[0])->s, CAR_SIZE), memcpy(end_dmg, Damage_Get(list[0]), 0x4d0);
        end_seed = crt_seed(), have_end = true;
    }
    if (where == 0 && have_end) {
        /* what changed between the steps (outside Sim_Step) */
        const uint8_t *c = Car_Get(list[0])->s, *d = Damage_Get(list[0]);
        for (uint32_t o = 0; o < CAR_SIZE; o += 4)
            if (!pointer_field(o) && memcmp(c + o, end_car + o, 4)) {
                int32_t a, b;
                memcpy(&a, end_car + o, 4), memcpy(&b, c + o, 4);
                printf("  between steps %d and %d: car +0x%03x %08x -> %08x\n", step_no, step_no + 1, o, (unsigned)a, (unsigned)b);
            }
        for (uint32_t o = 0xb4; o < 0x4d0; o += 4)
            if (memcmp(d + o, end_dmg + o, 4)) printf("  between steps %d and %d: damage +0x%03x\n", step_no, step_no + 1, o);
        if (end_seed != crt_seed()) printf("  between steps %d and %d: rand drawn\n", step_no, step_no + 1);
    }
    if (where == 0 && rep_run) {
        /* the replays' step against the port's: the car, the replays' images (but their pointers) */
        rep_run = false;
        compare_car_only("Replay_PlayStep", list, n);
        for (int i = 0; i < 16; i++) {
            Replay *r = Replay_Slot(i);
            uint32_t sz;
            const uint8_t *img = r ? Replay_Image(r, &sz) : NULL;
            if (!img || !rep_at[i]) continue;
            static uint8_t e[0x110];
            uc_mem_read(uc, rep_at[i], e, 0x110);
            for (uint32_t o = 0; o < 0x110; o++) {
                if ((o >= 0x24 && o < 0x44) || (o >= 0x104 && o < 0x108)) continue;
                if (e[o] != img[o]) {
                    printf("  step %d replay %d +0x%03x: original %02x port %02x\n", step_no + 1, i, o, e[o], img[o]);
                    count("the replay's step (its block)");
                    break;
                }
            }
        }
    }
    if (where == 0) {
        memset(emu_seen, 0, sizeof emu_seen);
        memset(port_seen, 0, sizeof port_seen);
        step_no++;
        if (getenv("TRACE_STEP") && atoi(getenv("TRACE_STEP")) == step_no) setenv("WDBG", "1", 1);
        else unsetenv("WDBG");
        sub_on = getenv("SUB_STEP") && atoi(getenv("SUB_STEP")) == step_no;
        emu_load(list, n);
        uint32_t args[3] = {CARS, LIST, (uint32_t)n};
        emu_call(0x433890, args, 3);   /* Sim_StepCars */
        return;
    }
    if (stop) return;
    for (int k = 0; k < NLATER; k++)
        if (later[k].id == where) {
            compare(k ? later[k - 1].name : "Sim_StepCars", list, n);
            if (stop) return;
            if (k == 0 && getenv("TRACE_STEP") && atoi(getenv("TRACE_STEP")) == step_no) {
                const Car *c = Car_Get(list[0]);
                int16_t cell = (int16_t)(c->s[0xb00] | c->s[0xb01] << 8);
                int32_t r = fx_mul(CI32(c, 0x758), 0x13333);
                printf("    port: cell %d, r %x, pos %x %x\n", cell, r, CI32(c, 0x2d0), CI32(c, 0x2d8));
                for (const Wall *w = cell >= 0 ? g_sec[cell].walls : NULL; w; w = w->next) {
                    int32_t ax = w->a[0] - CI32(c, 0x2d0), az = w->a[2] - CI32(c, 0x2d8), bx = w->b[0] - CI32(c, 0x2d0), bz = w->b[2] - CI32(c, 0x2d8);
                    bool sx = (ax < 0) == (bx < 0), sz = (az < 0) == (bz < 0);
                    if (sx && r <= abs(ax) && r <= abs(bx)) continue;
                    if (sz && r <= abs(az) && r <= abs(bz)) continue;
                    printf("    port wall %d type %d passes\n", (int)(w - g_fin), w->type);
                }
            }
            /* the next pass from the port's state */
            emu_load(list, n);
            uint32_t args[3] = {CARS, later[k].args == 1 ? COLL : LIST, (uint32_t)n};
            if (later[k].args == 1) {
                memcpy(heap + (COLL - HEAP), g_collide.car, sizeof(int16_t) * (size_t)g_collide.n);
                args[2] = (uint32_t)g_collide.n;
            }
            if (later[k].args == 3) {
                uint32_t a2[2] = {LIST, (uint32_t)n};
                emu_call(later[k].fn, a2, 2);
                if (!stop && later[k].fn2) emu_call(later[k].fn2, a2, 2);
            } else if (later[k].fn)
                emu_call(later[k].fn, args, later[k].args ? 3 : 0);
            return;
        }
    if (where == 7) compare(later[NLATER - 1].name, list, n);
}

static void compare(const char *after, const int16_t *list, int n)
{
    if (free_run) {
        if (strcmp(after, "Damage_Scrapes")) return;
        static int first = -1;
        const uint8_t *e = heap + (CARS - HEAP) + (uint32_t)list[0] * CAR_SIZE;
        const Car *c = Car_Get(list[0]);
        int32_t ep[3], pp[3];
        memcpy(ep, e + 0x2d0, 12), memcpy(pp, c->s + 0x2d0, 12);
        double dx = (ep[0] - pp[0]) / 65536.0, dz = (ep[2] - pp[2]) / 65536.0;
        if (first < 0 && memcmp(e + 0x2d0, c->s + 0x2d0, 12)) {
            first = step_no;
            printf("free: the positions first differ after step %d\n", step_no);
        }
        if (step_no % 25 == 0 || step_no < 5)
            printf("free: step %d original %.2f %.2f %.2f port %.2f %.2f %.2f (%.3f apart)\n", step_no, ep[0] / 65536.0, ep[1] / 65536.0,
                   ep[2] / 65536.0, pp[0] / 65536.0, pp[1] / 65536.0, pp[2] / 65536.0, sqrt(dx * dx + dz * dz));
        return;
    }
    checked++;
    for (int i = 0; i < n; i++) {
        const uint8_t *e = heap + (CARS - HEAP) + (uint32_t)list[i] * CAR_SIZE;
        const Car *c = Car_Get(list[i]);
        int shown = 0, nd = 0;
        for (uint32_t o = 0; o < CAR_SIZE; o += 4) {
            if (pointer_field(o) || o == 0x750 || o == 0x754) continue;
            int32_t a, b;
            memcpy(&a, e + o, 4);
            memcpy(&b, c->s + o, 4);
            if (a == b) continue;
            nd++;
            if (shown++ < 80) printf("  step %d car %d +0x%03x: original %08x port %08x (%+d)\n", step_no, list[i], o, (unsigned)a, (unsigned)b, b - a);
        }
        if (nd) {
            printf("step %d: car %d differs in %d ints after %s\n", step_no, list[i], nd, after);
            /* where it starts: the first pass whose input already differs, and the one before it */
            for (int k = 0; k < NPASS; k++) {
                if (!emu_seen[k] || !port_seen[k]) continue;
                int m = ndiff(emu_at[k], port_at[k], false, passes[k].name);
                if (!m) continue;
                printf("  first differing input: before %s (%d ints)\n", passes[k].name, m);
                ndiff(emu_at[k], port_at[k], true, "the pass before it");
                if (k > 0 && getenv("SHOW")) {
                    char *e = getenv("SHOW");
                    while (*e) {
                        unsigned o = (unsigned)strtoul(e, &e, 16);
                        int32_t a, b;
                        memcpy(&a, emu_at[k - 1] + o, 4), memcpy(&b, port_at[k - 1] + o, 4);
                        printf("    input +0x%x: original %08x port %08x\n", o, a, b);
                        if (*e == ',') e++;
                    }
                }
                break;
            }
            count(after);
            if (++mismatches >= 3 && !quiet) stop = true;
        }
        /* the damage record */
        const uint8_t *ed = heap + (DMG - HEAP) + (uint32_t)list[i] * 0x4d0, *pd = Damage_Get(list[i]);
        for (uint32_t o = 0xb4, shown = 0; o < 0x4d0; o += 4)
            if (memcmp(ed + o, pd + o, 4) && shown++ < 20) {
                int32_t a, b;
                memcpy(&a, ed + o, 4), memcpy(&b, pd + o, 4);
                printf("  step %d car %d damage +0x%03x: original %08x port %08x\n", step_no, list[i], o, (unsigned)a, (unsigned)b);
                if (shown == 1) count("damage");
                if (shown == 1 && ++mismatches >= 3 && !quiet) stop = true;
            }
    }
    /* the records the port keeps outside the image: the flat boxes, the fades */
    for (int i = 0; i < n; i++) {
        const uint8_t *eb = data + 0x590ed0 + 0x98 * (uint32_t)list[i] - DATA;
        const uint8_t *pb = (const uint8_t *)Walls_BoxRec(list[i]);
        for (uint32_t o = 0; o < 0x90; o += 4)   /* +0x90, +0x94: pointers */
            if (memcmp(eb + o, pb + o, 4)) {
                int32_t a, b;
                memcpy(&a, eb + o, 4), memcpy(&b, pb + o, 4);
                printf("  step %d car %d box +0x%02x: original %08x port %08x\n", step_no, list[i], o, (unsigned)a, (unsigned)b);
                count("the boxes");
                break;
            }
    }
    for (int i = 0; i < 8; i++) {
        int32_t f[8];
        Fade_State(i, f);
        static const uint32_t fa[8] = {0x53a0ec, 0x53a04c, 0x53a02c, 0x53a08c, 0x53a0ac, 0x53a06c, 0x53a20c, 0x53a0cc};
        for (int k = 0; k < 8; k++) {
            int32_t e;
            memcpy(&e, data + fa[k] + 4 * (uint32_t)i - DATA, 4);
            if (k == 3) e = e != 0, f[k] = f[k] != 0;
            if (e != f[k]) {
                printf("  step %d fade %d field %d: original %08x port %08x\n", step_no, i, k, (unsigned)e, (unsigned)f[k]);
                count("the fades");
                break;
            }
        }
    }
    if (emu_seed != crt_seed() && !strstr(after, "not run")) {
        printf("  step %d: rand's state after %s: original %08x port %08x\n", step_no, after, emu_seed, crt_seed());
        count("rand");
    }
    /* the slots' vertices */
    for (int i = 0; i < n; i++)
        for (int k = 0; k < SLOTS; k++) {
            uint8_t *m, *v, *r;
            int nv;
            if (!slot_at[list[i]][k] || !Damage_SlotData(list[i], k, &m, &v, &nv, &r) || !v) continue;
            uint8_t ev[0x30];
            for (int j = 0; j < nv; j++) {
                uc_mem_read(uc, slot_at[list[i]][k] + 0x30 * (uint32_t)j, ev, 0x30);
                if (memcmp(ev, v + 0x30 * j, 0x30)) {
                    printf("  step %d car %d slot %d vertex %d differs\n", step_no, list[i], k, j);
                    for (int f = 0; f < 12; f++) {
                        float a, b;
                        memcpy(&a, ev + 4 * f, 4), memcpy(&b, v + 0x30 * j + 4 * f, 4);
                        if (memcmp(&a, &b, 4)) printf("    +0x%02x: original %.6f (%08x) port %.6f (%08x)\n", 4 * f, a, *(uint32_t *)&a, b, *(uint32_t *)&b);
                    }
                    count("the dents");
                    break;
                }
            }
        }
    /* the globals the step wrote, against the port's (the slots emu_load set aside) */
    static uint8_t gseen[(DATA_END - DATA) / 4];
    for (uint32_t va = DATA; va < DATA_END - 0x1000; va += 4) {
        if (va >= 0x6e0ef4 && va < 0x6ed5f0) continue;   /* the math tables */
        if (va == 0x59226c || va == 0x592270) continue;  /* the controls' car and damage */
        if (va >= 0x71f608 && va < 0x71f608 + 4 * SEC_MAX) continue;
        if (va == 0x72d254 || va == 0x72d250) continue;
        if (va >= 0x58c938 && va < 0x58ca7c) continue;   /* the scenery tables */
        if (va >= 0x588990 && va < 0x588990 + 4 * CAR_MAX) continue;
        if (va >= 0x53aba8 && va < 0x53aba8 + 4 * CAR_MAX) continue;
        if (va == 0x53c9a4 || va == 0x588b94 || va == 0x588b98 || va == 0x543ecc || va == 0x53cc18 || va == 0x53cc1c)
            continue;
        if (va == 0x72d24c || va == 0x72d248 || va == 0x72d55c || va == 0x6ef5f0 || va == 0x6ef5f4 || va == 0x71f600 ||
            va == 0x72d568 || va == 0x72d458)
            continue;
        const uint8_t *e = data + (va - DATA), *q = gm(va);
        if (!memcmp(e, q, 4)) continue;
        int32_t a, b;
        memcpy(&a, e, 4), memcpy(&b, q, 4);
        if (gseen[(va - DATA) / 4]++) continue;
        printf("  step %d global 0x%06x: original %08x port %08x\n", step_no, va, (unsigned)a, (unsigned)b);
    }
}

static bool load_stage(void)
{
    char a[64], b[64];
    const char *country = stage[0] == 'k' ? "Kenya" : stage[0] == 's' ? "Sweden" : "France";
    snprintf(a, sizeof a, "Game\\Tracks\\%s\\%shi.bfl", country, stage);
    snprintf(b, sizeof b, "Game\\Tracks\\%s\\%.3shit.bfl", country, stage);
    return Stage_Load(stage, a, b);
}

static bool load_cars(void)
{
    if (!Car_Load(0)) return false;
    Sim_InitLists(1);
    Damage_CarsLoaded();
    Frame_SetRate(0x190000);
    Car_ProgressStart(Car_Get(0));
    return true;
}

static void input(int player, uint32_t *held, uint32_t *pressed) { (void)player, *held = 0, *pressed = 0; }
static Obj camera;
static Obj *host_camera(int player) { (void)player; return &camera; }
static const RaceHost host = {load_stage, load_cars, NULL, input, NULL, NULL, host_camera};

static int frame;
static void step(void)
{
    Timer_Sample((uint32_t)((uint64_t)frame * 1000 / 60));
    Race_Frame();
    frame++;
}

int main(int argc, char **argv)
{
    char err[128];
    quiet = getenv("QUIET") != NULL;
    free_run = getenv("FREE") != NULL;
    if (argc > 1) stage = argv[1];
    if (!vfs_mount_default() || !exe_init(err, sizeof err)) {
        printf("sim_diff: no game data\n");
        return 1;
    }
    Math_InitTables();
    if (getenv("DUMP_TABLES")) {
        static const struct { const char *f; const void *p; size_t n; } t[] = {
            {".scratch/sin.bin", g_sin, sizeof g_sin}, {".scratch/tan.bin", g_tan, sizeof g_tan}, {".scratch/sqrt.bin", g_sqrt, sizeof g_sqrt},
            {".scratch/asin.bin", g_asin, sizeof g_asin}, {".scratch/atan.bin", g_atan, sizeof g_atan}};
        for (unsigned k = 0; k < 5; k++) {
            FILE *f = fopen(t[k].f, "wb");
            if (f) fwrite(t[k].p, 1, t[k].n, f), fclose(f);
        }
        return 0;
    }
    Mem_Init();
    D3d_Open(640, 480, 16);
    VB_CreatePool();
    memset(&g_world, 0, sizeof g_world);
    g_world.kind = 3;
    Options_Defaults();
    Profiles_Init();
    Snd_Init();
    Player_SetProfile(0, 0);
    Profile_Create(0);
    Tod_Select(stage);
    gw32(0x52afb4, (gm32(0x52afb4) & 0xfffe1fffu) | 1u << 13);
    Sel_ModeFlags();
    Race_Begin(&host);
    while (frame < 60 * 20 && Race_State(0) != 8) step();
    char dn[64];
    snprintf(dn, sizeof dn, "Game\\DemoRuns\\%c%.4sH.rpl", stage[0] - 32, stage + 1);
    Replay *demo = Replay_Load(dn);
    if (!demo || !Replay_Play(demo, 0, 0, 0)) {
        printf("sim_diff: no demo run %s\n", dn);
        return 1;
    }
    emu_init();
    g_sim_probe = probe;
    while (frame < 60 * 60 * 5 && Replay_Playing(demo) && !stop) step();
    if (reads) {
        FILE *f = fopen(".scratch/reads.txt", "w");
        for (uint32_t i = 0; i < (DATA_END - DATA) / 4; i++)
            if (reads[i] && f) fprintf(f, "%06x %u %08x\n", DATA + 4 * i, reads[i], gm32(DATA + 4 * i));
        if (f) fclose(f);
    }
    for (int k = 0; k < 8 && tally[k].name; k++)
        printf("sim_diff: after %s: %d steps differ, the first %d\n", tally[k].name, tally[k].n, tally[k].first);
    printf("sim_diff: %d steps compared, %d with differences\n", checked, mismatches);
    return mismatches ? 1 : 0;
}
