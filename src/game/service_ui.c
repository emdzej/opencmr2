/* The service screens' pages (see service.h): the main page's items and frame (0x4ff4b0-0x500500), the
   set-up, information, message and save pages, and their draws (0x50a080-0x50ee10). */
#include "game/service_int.h"
#include "exe.h"
#include "game/gstate.h"
#include "game/input.h"
#include "game/mem.h"
#include "game/timer.h"
#include "pending.h"
#include "text.h"
#include <string.h>

int Sv_CarClass(void) { return (int)gm32(0x516b40 + 4u * (gm32(0x531350 + 0x30u * (uint32_t)Sv_Player()) & 0x3f)); }   /* 0x4086b0, 0x40ee90 */
static int item_value(Menu *m, int16_t id) { return m->items[Menu_IndexOf(m, id)].value; }
static void set_item_value(Menu *m, int16_t id, int v) { m->items[Menu_IndexOf(m, id)].value = (uint8_t)v; }

/* the panels a set-up or repair list position stands for (0x526f54, 0x526f70, 0x526f8c) */
int Sv_SetupView(int i) { return (int)exe_u32(0x526f54 + 4u * (uint32_t)i); }     /* 0x4ff4b0 */
int Sv_SetupPart(int i) { return (int)exe_u32(0x526f70 + 4u * (uint32_t)i); } /* 0x4ff4c0 */
int Sv_RepairView(int i) { return (int)exe_u32(0x526f8c + 4u * (uint32_t)i); }    /* 0x4ff4d0 */

/* 0x501280: a view the car's class has: 2 needs 0x516c40, 4 0x516c08 */
bool Sv_ViewAllowed(int view, int cls)
{
    if (view == 2) return gm32(0x516c40 + 4u * (uint32_t)cls) != 0;
    if (view == 4) return gm32(0x516c08 + 4u * (uint32_t)cls) != 0;
    return true;
}

/* 0x501230: two information pages, one on the super special (stage 11) and in single stages, time trials
   and the network's */
int Sv_InfoPages(void)
{
    uint32_t m = Gs_Mode();
    if ((gm32(0x52f2ac) >> 5 & 0x1f) == 10 || m == 2 || m == 3 || m == 8 || m == 9 || m == 10) return 1;
    return 2;
}

/* 0x502520: a player's turn: the set-up and repair lists to the start, the main page's state */
void Sv_MainTurn(void)
{
    sv_main.sel = 0;
    set_item_value(&sv_main, 1, 0);
    set_item_value(&sv_main, 2, 0);
    Sv_MainReset();
}

/* 0x500500, 0x500520, 0x500530, 0x5004c0: the warning blink, for 0.3 s, on and off a tenth each */
void Sv_BlinkOn(void) { S.blink = 1, S.blink_at = (int)Timer_Centis(); }
static void blink_off(void) { S.blink = 0, S.blink_at = 0; }
static void blink_step(void)
{
    if (S.blink && Timer_Centis() - (uint32_t)S.blink_at > 0x1d) blink_off();
}
bool Sv_BlinkLit(void) { return S.blink ? !((Timer_Centis() - (uint32_t)S.blink_at) / 10 & 1) : false; }

/* 0x4ff5b0: the set-up page's value count by the set-up list's position, its value the part's */
static void setup_count(void)
{
    int v = item_value(&sv_main, 1);
    switch (v) {
    case 0: sv_setup.items[0].count = 7; break;
    case 1: sv_setup.items[0].count = 5; break;
    case 2: case 3: case 4: case 5: case 6: sv_setup.items[0].count = 0xb; break;
    }
    PENDING_NOTE(0x5028a0, "the set-up part's value");
}

/* 0x4ffa70: the saved games' count; the save list reset unless back from it */
void sv_main_enter(Menu *m, bool back)
{
    (void)m, (void)back;
    PENDING_NOTE(0x4f4db0, "the saved championships' count");
    if (!S.save_back) {
        PENDING_NOTE(0x4ffa70, "the save list's position");
        return;
    }
    S.save_back = false;
}

/* 0x4ffab0: the main page's frame: the race or quit due, the panels sliding as the selection moves, the
   car shown on the set-up and repair items, the lists kept to what the car's class has */
void sv_main_frame(Menu *m)
{
    if (S.race_in > 0 && --S.race_in == 0) Sv_RaceNow();
    if (S.quit_in > 0 && --S.quit_in == 0) Sv_QuitNow(false);
    S.now = Timer_Centis();
    if (m->sel != S.main_sel) {
        static const int clear[7] = {0, 1, 3, 2, 5, 6, 7};
        for (int i = 0; i < 7; i++) Sv_TweenClear(clear[i]);
        if (!((m->sel == 2 && S.main_sel == 1) || (m->sel == 1 && S.main_sel == 2))) Sv_TweenClear(4);
        S.since = S.now;
        S.panel[S.main_sel] = 2;
        S.main_sel = m->sel;
        blink_off();
    }
    for (int i = 0; i < 6; i++) {
        switch (S.panel[i]) {
        case 0:
            if (i == m->sel) Menu_SetFlags(m, false, false, false, false);
            if (S.now - S.since > 1) S.panel[i] = 1;
            break;
        case 1:
            if (i == m->sel && m->sel != Menu_IndexOf(m, 4)) Menu_SetFlags(m, true, true, true, true);
            break;
        case 2:
            if (i == m->sel) Menu_SetFlags(m, false, false, false, false);
            if (S.now - S.since > 1) {
                S.since = S.now;
                S.panel[i] = 3;
                S.panel[m->sel] = 0;
            }
            break;
        case 3:
            if (i == m->sel) Menu_SetFlags(m, false, false, false, false);
            break;
        }
    }
    bool car = m->sel == Menu_IndexOf(m, 2) || m->sel == Menu_IndexOf(m, 1);
    for (int p = 0; p < (int)Gs_Players(); p++) Sv_CarShow(p, car && p == Sv_Player());
    if (m->sel != Menu_IndexOf(m, 5)) set_item_value(&sv_main, 5, 1);
    PENDING_NOTE(0x4ff630, "the main page's Escape on the information, set-up and repair items");
    int v = item_value(&sv_main, 1);
    while (!Sv_ViewAllowed(Sv_SetupView(v), Sv_CarClass())) v += S.setup_last < v ? 1 : -1;
    set_item_value(&sv_main, 1, v);
    if (S.setup_last != v) blink_off(), Sv_TweenClear(5), Sv_TweenClear(7);
    S.setup_last = v;
    v = item_value(&sv_main, 2);
    while (!Sv_ViewAllowed(Sv_RepairView(v), Sv_CarClass())) v += S.repair_last < v ? 1 : -1;
    set_item_value(&sv_main, 2, v);
    if (S.repair_last != v) blink_off(), Sv_TweenClear(5), Sv_TweenClear(7);
    S.repair_last = v;
    v = item_value(&sv_main, 0);
    if (S.info_last != v) Sv_TweenClear(7);
    S.info_last = v;
    blink_step();
    setup_count();
    if (m->sel != Menu_IndexOf(m, 4)) {
        gw32(0x526f40, 0);
        return;
    }
    PENDING_NOTE(0x4ff720, "the save list's frame");
}

void sv_info_go(Menu *m, MenuItem *it) { (void)m, (void)it; PENDING_NOTE(0x503960, "the information pages"); }
void sv_setup_go(Menu *m, MenuItem *it) { (void)m, (void)it; PENDING_NOTE(0x5000b0, "the set-up item"); }
void sv_repair_go(Menu *m, MenuItem *it) { (void)m, (void)it; PENDING_NOTE(0x4fffe0, "the repair item"); }
void sv_save_go(Menu *m, MenuItem *it) { (void)m, (void)it; PENDING_NOTE(0x4ffed0, "the save item"); }
void sv_setup_choose(Menu *m, MenuItem *it) { (void)m, (void)it; PENDING_NOTE(0x5003d0, "a set-up value"); }
void sv_save_yes(Menu *m, MenuItem *it) { (void)m, (void)it; PENDING_NOTE(0x500020, "the championship saved"); }

void Sv_CarsInit(void) { PENDING_NOTE(0x50a3c0, "each player's car on its stand (0x5062d0)"); }

/* 0x502230 */
void sv_msg_enter(Menu *m, bool back) { (void)m, (void)back; S.save_back = true; }

/* 0x4ffa50: No first */
void sv_save_enter(Menu *m, bool back)
{
    (void)back;
    m->sel = 1;
    S.save_back = true;
}

/* ---- the damage and the set-up's state ---- */

/* 0x502df0: part's damage in percent (1 for any), from the kept set-up (0x82c070) or the car's own
   (live, 0x52ebc0); the bytes at +0x108-0x121, the largest of a part's four (or two) */
int Sv_Damage(int p, int part, bool live)
{
    uint32_t b = (live ? 0x52ebc0u : 0x82c070u) + 0x148u * (uint32_t)p;
    static const uint16_t first[12] = {0, 0x116, 0x117, 0x10e, 0x118, 0x112, 0x119, 0x10c, 0x11f, 0x120, 0x121, 0x108};
    static const uint8_t count[12] = {0, 1, 1, 4, 1, 4, 1, 2, 1, 1, 1, 4};
    uint32_t v = 0;
    if (part >= 1 && part <= 11)
        for (int k = 0; k < count[part]; k++)
            if (gm8(b + first[part] + (uint32_t)k) > v) v = gm8(b + first[part] + (uint32_t)k);
    uint32_t pc = v * 100 / 0xff;
    if (pc == 0) return v ? 1 : 0;
    return pc > 100 ? 100 : (int)pc;
}

/* 0x503940: part repaired this service */
bool Sv_Repaired(int p, int part) { return gm8(0x82c040 + 12u * (uint32_t)p + (uint32_t)part) != 0; }

/* 0x502fc0: the time for part's repair (0x5270b4) left and the part damaged, or repaired already */
bool Sv_CanRepair(int p, int part)
{
    if ((int)gm32(0x5270b4 + 4u * (uint32_t)part) <= S.time[p]) return Sv_Damage(p, part, true) != 0;
    return Sv_Repaired(p, part);
}

/* 0x502b10: set-up value i as it came in (0x82bf04 against the kept 0x82bee8) */
bool Sv_SetupKept(int p, int i)
{
    if (i < 0 || i > 6) return false;
    return gm8(0x82bf04 + 7u * (uint32_t)p + (uint32_t)i) == gm8(0x82bee8 + 7u * (uint32_t)p + (uint32_t)i);
}

/* 0x502630: the time for the set-up's change (0x527098) left, or changed already (0x82bf20) */
bool Sv_CanSet(int p, int part)
{
    if ((int)gm32(0x527098 + 4u * (uint32_t)part) <= S.time[p]) return true;
    return gm8(0x82bf20 + 7u * (uint32_t)p + (uint32_t)part) != 0;
}
