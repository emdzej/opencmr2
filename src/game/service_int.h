/* The service screens' shared state and pieces (service.c, service_ui.c, service_car.c). */
#pragma once
#include "bfl.h"
#include "fe/menu.h"
#include "game/proc.h"
#include "world/obj.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int32_t v, len;      /* +0x00 the value 0-1 (16.16), +0x04 the length in hundredths */
    uint32_t start;      /* +0x08 */
    int32_t state;       /* +0x0c: 0 idle, 1 running, 2 done */
    int32_t curve;       /* +0x10: 0 straight, 1 squared, 2 square root */
} SvTween;

typedef struct {
    int to_go;           /* DAT_0082b0a8: the players still to set up */
    bool race_to_fe;     /* DAT_0082b0ac */
    bool quit;           /* DAT_0082a938 */
    bool fresh;          /* DAT_0082af8c */
    bool first_of_day;   /* DAT_00527000 */
    uint32_t fade_at;    /* DAT_0082b0a4 */
    uint32_t turn_at;    /* DAT_0082b0a0 */
    uint32_t sound_at;   /* DAT_0083188c */
    int32_t time[4];     /* DAT_0082af90: the service time left, in hundredths (an hour) */
    int32_t time_lim[4]; /* DAT_0082af78: 60 */
    int ntw;             /* DAT_0082b1bc */
    SvTween tw[8];       /* DAT_0082b2c0 */
    uint8_t pulse[4];    /* DAT_0082b1b8 */
    int race_in, quit_in;      /* DAT_0082ac4c, DAT_0082ac50: frames to the race or the quit */
    int setup_last, repair_last, info_last;   /* DAT_00526f48, 4c, 50 */
    int info_page;       /* DAT_0082a92c */
    int blink, blink_at; /* DAT_0082ac58, 5c */
    int main_sel;        /* DAT_00526f44: the main page's selection last frame */
    int panel[6];        /* DAT_0082a90c: each item's panel state (0 in, 1 shown, 2 out, 3 gone) */
    uint32_t now, since; /* DAT_0082a928, DAT_0082a930 */
    bool save_back;      /* DAT_0082ac60 */
} SvState;

extern SvState S;
extern Menu sv_main, sv_setup, sv_info, sv_msg, sv_save;

BflSlot *Sv_Common(void);
BflSlot *Sv_Day(void);
ProcGroup *Sv_Group(void);
Obj *Sv_World(void);
Menu *Sv_Current(void);
int Sv_Player(void);
void Sv_TweenStart(int i, int len, int curve);
void Sv_TweenClear(int i);
int Sv_TweenState(int i);
void Sv_TweensFinish(void);
void Sv_RaceNow(void);
void Sv_QuitNow(bool keep_net);
void Sv_MainReset(void);

/* service_ui.c */
int Sv_InfoPages(void);                   /* 0x501230 */
void Sv_InfoInit(void);                   /* 0x50a080 */
void Sv_InfoInit2(void);                  /* 0x50a3c0 */
void Sv_MainTurn(void);                   /* 0x502520 */
int Sv_SetupView(int i);                  /* 0x4ff4b0 */
int Sv_RepairView(int i);                 /* 0x4ff4d0 */
void sv_info_go(Menu *m, MenuItem *it);   /* 0x500100 */
void sv_setup_go(Menu *m, MenuItem *it);  /* 0x5000b0 */
void sv_repair_go(Menu *m, MenuItem *it); /* 0x4fffe0 */
void sv_race_go(Menu *m, MenuItem *it);   /* 0x500110 */
void sv_save_go(Menu *m, MenuItem *it);   /* 0x4ffed0 */
void sv_quit_go(Menu *m, MenuItem *it);   /* 0x500190 */
void sv_main_enter(Menu *m, bool back);   /* 0x4ffa70 */
void sv_main_frame(Menu *m);              /* 0x4ffab0 */
void sv_main_draw(Menu *m);               /* 0x50eba0 */
void sv_main_leave(Menu *m, bool back);   /* 0x500210 */
void sv_setup_choose(Menu *m, MenuItem *it);   /* 0x5003d0 */
void sv_setup_draw(Menu *m);              /* 0x50ee10 */
void sv_info_draw(Menu *m);               /* 0x50edf0 */
void sv_msg_enter(Menu *m, bool back);    /* 0x502230 */
void sv_msg_draw(Menu *m);                /* 0x50e740 */
void sv_save_yes(Menu *m, MenuItem *it);  /* 0x500020 */
void sv_save_enter(Menu *m, bool back);   /* 0x4ffa50 */
void sv_save_draw(Menu *m);               /* 0x50e780 */

/* service_car.c */
void Sv_LoadTextures(void);               /* 0x503ea0 */
void Sv_StageInit(void);                  /* 0x5040f0 */
void Sv_TyresInit(void);                  /* 0x505e70 */
void Sv_CarShow(int p, bool on);          /* 0x506b20 */
void Sv_CarView(int p, int view, bool now);    /* 0x506930 */
void Sv_CarTyres(int p, int view, bool now);   /* 0x5009c0 */
void Sv_CarTurn(void);                    /* 0x506720 */
void Sv_Free(void);
