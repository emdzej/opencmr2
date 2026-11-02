/* The service screens' pages (see service.h): the main page's items and frame (0x4ff4b0-0x500500), the
   set-up, information, message and save pages, and their draws (0x50a080-0x50ee10). */
#include "game/service_int.h"
#include "exe.h"
#include "font.h"
#include "game/gstate.h"
#include "game/saves.h"
#include "render/d3d7.h"
#include "game/input.h"
#include "game/mem.h"
#include "game/timer.h"
#include "pending.h"
#include "text.h"
#include <stdio.h>
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
    sv_setup.items[0].value = (uint8_t)Sv_SetupValue(Sv_Player(), v);
}

/* 0x4ffa70: the saved games' count; the save list closed and at its top unless back from the message
   or the question */
void sv_main_enter(Menu *m, bool back)
{
    (void)m, (void)back;
    gw32(0x82aa40, (uint32_t)Champs_Count());
    if (!S.save_back) {
        gw8(0x82ab44, 0);
        gw32(0x82a924, 0), gw32(0x82aa3c, 0), gw32(0x82ac48, 0);
        return;
    }
    S.save_back = false;
}

/* 0x4ff630: Back on the information item turns the page back; on a repaired part or a changed set-up
   undoes it */
static void back_key(Menu *m)
{
    int rec = Sv_Player() == 1 && !Gs_ModeBit17() ? 1 : 0;
    if (!(Input_Record(rec)->pressed & MB_BACK)) return;
    int p = Sv_Player();
    if (m->sel == Menu_IndexOf(m, 0)) {
        Sv_InfoZoom(0, 1);
        return;
    }
    if (m->sel == Menu_IndexOf(m, 2)) {
        int part = Sv_RepairView(item_value(m, 2));
        if (Sv_Repaired(p, part)) Sv_Unrepair(p, part);
    } else if (m->sel == Menu_IndexOf(m, 1)) {
        int part = Sv_SetupPart(item_value(m, 1));
        if (Sv_SetupChanged(p, part)) Sv_SetupUndo(p, part);
    }
}

/* the save list's view: its top kept within 13 rows of the selection, the rows shown */
static void save_rows(void)
{
    int sel = gms32(0x82a924), top = gms32(0x82aa3c), n = gms32(0x82aa40);
    if (sel < top && --top < 0) top = 0;
    if (top + 0xd <= sel) top++;
    gw32(0x82aa3c, (uint32_t)top);
    gw32(0x82ac48, (uint32_t)(n > 0xd ? 0xd : n));
}

/* the name being typed (0x82aa44) back to 0x52ea5c's (empty) */
static void name_reset(void) { snprintf((char *)gm(0x82aa44), 0x100, "%s", (const char *)gm(0x52ea5c)); }

/* 0x4ff720: the Save item's frame. With the list open (0x82ab44) Up and Down move in it and Back
   closes it (the main page's flags off). Otherwise the item's value: 0 the list's, the name reset and
   the typed characters dropped; 1 typing a new one: the keys off the page but Up (back to the list's
   value) and Left / Right (to the next item), each resetting the name, these only once no key is
   held since the item was reached (0x526f40); a typed letter goes in while the name is under 30
   letters and narrower than 220 pixels at 640, backspace rubs out */
static void save_frame(Menu *m)
{
    gw32(0x82aa40, (uint32_t)Champs_Count());
    int n = gms32(0x82aa40);
    if (gm8(0x82ab44)) {
        Menu_SetFlags(m, false, false, true, true);
        uint32_t k = Input_Record(0)->pressed;
        int sel = gms32(0x82a924);
        if (n < 1) {
            gw8(0x82ab44, 0);
            sel = -1;
            gw32(0x82aa3c, 0);
        } else if (sel == -1) sel = 0;
        else if (k & MB_BACK) gw8(0x82ab44, 0), S.save_back = true;
        else if ((k & MB_UP) && sel > 0) sel--;
        else if ((k & MB_DOWN) && sel < n - 1) sel++;
        gw32(0x82a924, (uint32_t)sel);
        if (n < 1) {
            /* the original's empty list: the top taken back by one (to 0) before the rows */
            int top = gms32(0x82aa3c) - 1;
            gw32(0x82aa3c, (uint32_t)(top < 0 ? 0 : top));
            if (gms32(0x82aa3c) + 0xd <= sel) gw32(0x82aa3c, gm32(0x82aa3c) + 1);
            gw32(0x82ac48, (uint32_t)(n > 0xd ? 0xd : n));
            return;
        }
        save_rows();
        return;
    }
    save_rows();
    MenuItem *it = &m->items[Menu_IndexOf(m, 4)];
    if (it->value != 1) {
        Input_FlushChars();
        name_reset();
        gw8(0x526f40, 0);
        Menu_SetFlags(m, true, true, true, true);
        return;
    }
    Menu_SetFlags(m, false, false, true, false);
    uint32_t k = Input_Record(0)->pressed;
    if (!k) gw8(0x526f40, 1);
    if (gm8(0x526f40)) {
        if (k & MB_UP) it->value = 0, name_reset();
        else if (k & MB_LEFT) m->sel--, name_reset();
        else if (k & MB_RIGHT) m->sel++, name_reset();
    }
    char b[0x100];
    snprintf(b, sizeof b, "%s", (const char *)gm(0x82aa44));
    int c;
    if (!Input_NextChar(&c)) return;
    int snd;
    size_t l = strlen(b);
    if (c == 8) {
        if (!b[0]) goto done;
        b[l - 1] = 0;
        snd = 2;
    } else {
        int sw, sh;
        D3d_ScreenSize(&sw, &sh);
        if (l > 0x1d || !strchr(exe_str(0x526fb8), c) || Font_TextWidth(0, (const char *)gm(0x82aa44)) >= sw * 0xdc / 0x280)
            goto done;
        b[l] = (char)c;
        b[l + 1] = 0;
        snd = 1;
    }
    Menu_Sound(snd);
done:
    snprintf((char *)gm(0x82aa44), 0x100, "%s", b);
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
    back_key(m);
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
    save_frame(m);
}

/* 0x500100: Information: the stage's map zoomed in or out */
void sv_info_go(Menu *m, MenuItem *it) { (void)m, (void)it; Sv_InfoZoom(0, 0); }

/* 0x5000b0: Set Up: its page with the time for the part, else the warning */
void sv_setup_go(Menu *m, MenuItem *it)
{
    (void)m, (void)it;
    setup_count();
    int v = item_value(&sv_main, 1);
    if (Sv_CanSet(Sv_Player(), v)) Menu_Goto(&sv_setup);
    else Sv_BlinkOn();
}

/* 0x500270: a repair with the time for it (the warning when the part is damaged and there is not) */
static void repair(Menu *m)
{
    int p = Sv_Player(), part = Sv_RepairView(item_value(&sv_main, 2));
    if (S.time[p] - (int)gm32(0x5270b4 + 4u * (uint32_t)part) < 0 && !Sv_Repaired(p, part)) {
        if (Sv_Damage(p, part, false) >= 1) Sv_BlinkOn();
        return;
    }
    if (!Sv_Repaired(p, part)) Sv_Repair(p, part);
    Menu_Goto(m->parent);
}

/* 0x500360: a repair undone */
static void unrepair(Menu *m)
{
    int p = Sv_Player(), part = Sv_RepairView(item_value(&sv_main, 2));
    if (Sv_Repaired(p, part)) Sv_Unrepair(p, part);
    Menu_Goto(m->parent);
}

/* 0x4fffe0: Repair: undone when repaired, else repaired */
void sv_repair_go(Menu *m, MenuItem *it)
{
    if (Sv_Repaired(Sv_Player(), Sv_RepairView(it->value))) unrepair(m);
    else repair(m);
}

/* the message (0x82a93c) shown */
static void message(int text)
{
    snprintf((char *)gm(0x82a93c), 0x100, "%s", Text_Get(text));
    Menu_Goto(&sv_msg);
}

/* 0x4ffed0: Save: with the list open the question (over the chosen one); on the list's value with
   saves the list opened; on the new name's a new save, the name reset, the list read again with the
   new one chosen, the message saved (0x143) or not (0x144) */
void sv_save_go(Menu *m, MenuItem *it)
{
    (void)it;
    if (gm8(0x82ab44)) {
        Menu_Goto(&sv_save);
        return;
    }
    if (m->items[4].value == 0 && Champs_Count() > 0) {
        gw8(0x82ab44, 1);
        Menu_SetFlags(m, false, false, false, false);
        return;
    }
    if (m->items[4].value != 1) return;
    if (!Champ_Save(-1, (const char *)gm(0x82aa44))) {
        message(0x144);
        return;
    }
    name_reset();
    message(0x143);
    Champs_Scan();
    gw32(0x82a924, (uint32_t)(Champs_Count() - 1));
}

/* 0x5003d0: a set-up value chosen: back to the original undoes the change; another sets it if there is
   the time or it was changed already (no warning otherwise) */
void sv_setup_choose(Menu *m, MenuItem *it)
{
    (void)it;
    int p = Sv_Player(), part = Sv_SetupPart(item_value(&sv_main, 1)), v = m->items[0].value;
    if (Sv_SetupChanged(p, part) && Sv_SetupIsOriginal(p, part, v)) {
        Sv_SetupUndo(p, part);
        Menu_Goto(m->parent);
        return;
    }
    if (!Sv_SetupIsOriginal(p, part, v)) {
        if (S.time[p] - (int)gm32(0x527098 + 4u * (uint32_t)part) < 0 && !Sv_SetupChanged(p, part)) return;
        Sv_SetupSet(p, part, v);
    }
    Menu_Goto(m->parent);
}

/* 0x500020: Yes: the chosen save written over, the message, the list read again when saved */
void sv_save_yes(Menu *m, MenuItem *it)
{
    (void)m, (void)it;
    if (!Champ_Save(gms32(0x82a924), NULL)) {
        message(0x144);
        return;
    }
    message(0x143);
    Champs_Scan();
}


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

/* ---- the set-up's values (0x82bee8 the edited, 0x82bf04 the original, 7 bytes a player; the first two
   choices, the others tenths) and the repairs ---- */

static uint32_t edited(int p, int part) { return 0x82bee8 + 7u * (uint32_t)p + (uint32_t)part; }
static uint32_t original(int p, int part) { return 0x82bf04 + 7u * (uint32_t)p + (uint32_t)part; }
static uint32_t changed(int p, int part) { return 0x82bf20 + 7u * (uint32_t)p + (uint32_t)part; }

/* 0x5028a0 */
int Sv_SetupValue(int p, int part)
{
    if (part < 0 || part > 6) return 0;
    return part < 2 ? gm8(edited(p, part)) : gms8(edited(p, part)) / 10;
}

/* 0x502a00 */
bool Sv_SetupIsOriginal(int p, int part, int v)
{
    if (part < 0 || part > 6) return false;
    return part < 2 ? gms8(original(p, part)) == v : gms8(original(p, part)) == v * 10;
}

bool Sv_SetupChanged(int p, int part) { return gm8(changed(p, part)) != 0; }   /* 0x502990 */

/* 0x502670: a value set, its time taken the first time */
void Sv_SetupSet(int p, int part, int v)
{
    if (part >= 0 && part <= 6) gw8(edited(p, part), (uint8_t)(part < 2 ? v : v * 10));
    if (!gm8(changed(p, part))) {
        S.time[p] -= (int)gm32(0x527098 + 4u * (uint32_t)part);
        gw8(changed(p, part), 1);
    }
}

/* 0x502790: the original back, the time given back */
void Sv_SetupUndo(int p, int part)
{
    if (part >= 0 && part <= 6) gw8(edited(p, part), gm8(original(p, part)));
    S.time[p] += (int)gm32(0x527098 + 4u * (uint32_t)part);
    gw8(changed(p, part), 0);
}

/* a part's damage bytes in the set-up record (0x148 bytes; 0x82c070 the car as it will race, 0x52ebc0
   as it came in); the body (6) has more */
static const uint16_t dmg_at[12] = {0, 0x116, 0x117, 0x10e, 0x118, 0x112, 0x119, 0x10c, 0x11f, 0x120, 0x121, 0x108};
static const uint8_t dmg_n[12] = {0, 1, 1, 4, 1, 4, 6, 2, 1, 1, 1, 4};

static uint32_t kept(int p) { return 0x82c070 + 0x148u * (uint32_t)p; }
static uint32_t came(int p) { return 0x52ebc0 + 0x148u * (uint32_t)p; }
static uint32_t repaired_at(int p, int part) { return 0x82c040 + 12u * (uint32_t)p + (uint32_t)part; }

/* 0x503010: part's damage cleared and its time taken, when it has any; the body also its panels (20
   of 13 bytes, the last 0xff), the dents and 0x104 */
void Sv_Repair(int p, int part)
{
    if (part < 1 || part > 11) return;
    uint32_t b = kept(p);
    bool any = false;
    for (int k = 0; k < dmg_n[part]; k++) any |= gm8(b + dmg_at[part] + (uint32_t)k) != 0;
    if (part == 6) {
        for (uint32_t a = 0x122; a < 0x12a; a++) any |= gm8(b + a) != 0;
        for (uint32_t a = 0x12c; a < 0x148; a += 4) any |= gm32(b + a) != 0;
        any |= gm8(b + 0x104) != 0;
    }
    if (any) {
        for (int k = 0; k < dmg_n[part]; k++) gw8(b + dmg_at[part] + (uint32_t)k, 0);
        if (part == 6) {
            for (uint32_t a = 0x122; a < 0x12a; a++) gw8(b + a, 0);
            for (uint32_t a = 0x12c; a < 0x148; a += 4) gw32(b + a, 0);
            gw8(b + 0x104, 0), gw8(b + 0x105, 0);
            for (uint32_t k = 0; k < 20; k++) {
                for (uint32_t j = 0; j < 12; j++) gw8(b + 13 * k + j, 0);
                gw8(b + 13 * k + 12, 0xff);
            }
        }
        S.time[p] -= (int)gm32(0x5270b4 + 4u * (uint32_t)part);
        gw8(repaired_at(p, part), 1);
        if (part == 3) Sv_SuspRepaired(p);    /* 0x509d00 */
        if (part == 11) Sv_AxlesRepaired(p);  /* 0x509d90 */
    }
    if (part == 6) Sv_DmgRest(p);   /* 0x5074d0 */
}

/* 0x5034f0: part's damage as it came in, the time given back */
void Sv_Unrepair(int p, int part)
{
    if (part < 1 || part > 11) return;
    uint32_t b = kept(p), c = came(p);
    for (int k = 0; k < dmg_n[part]; k++) gw8(b + dmg_at[part] + (uint32_t)k, gm8(c + dmg_at[part] + (uint32_t)k));
    if (part == 6) {
        for (uint32_t a = 0; a < 0x106; a++) gw8(b + a, gm8(c + a));
        for (uint32_t a = 0x122; a < 0x12a; a++) gw8(b + a, gm8(c + a));
        for (uint32_t a = 0x12c; a < 0x148; a++) gw8(b + a, gm8(c + a));
    }
    S.time[p] += (int)gm32(0x5270b4 + 4u * (uint32_t)part);
    gw8(repaired_at(p, part), 0);
    if (part == 3) Sv_SuspDamaged(p);   /* 0x509be0 */
    if (part == 6) Sv_DmgApply(p);      /* 0x507650 */
    if (part == 11) Sv_AxlesDamaged(p); /* 0x509d30 */
}
