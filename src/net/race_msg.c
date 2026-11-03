/* The race's network messages (docs/re/network.md): 0x427620-0x427aa0. Each is a type byte and the
   type's fields at the offsets the original's stack structures put them (the senders' type byte is
   written by a byte store the decompiler drops: the disassembly gives it). The guaranteed ones go
   through DirectPlay's guaranteed delivery; the relay delivers everything in order. */
#include "net/race_msg.h"
#include "fe/fe_net.h"
#include "game/gstate.h"
#include "game/mem.h"
#include "game/pause.h"
#include "game/race.h"
#include "game/results.h"
#include "game/timer.h"
#include "net/dplay.h"
#include "net/records.h"
#include "net/session.h"
#include "pending.h"
#include "sim/fade.h"
#include <string.h>

enum { SPLITS_ON = 0x539cc8, STAGE_SENT = 0x539ed0, LAP_SENT = 0x539dcc, FADE_COLOUR = 0x539ed4 };

void Net_SetSplitsOn(bool on) { gw8(SPLITS_ON, on); }

void Net_ForgetTimes(void)
{
    gw32(STAGE_SENT, 0xffffffffu);
    gw32(LAP_SENT, 0xffffffffu);
}

int32_t Net_LastStageTime(void) { return (int32_t)gm32(STAGE_SENT); }
int32_t Net_LastLapTime(void) { return (int32_t)gm32(LAP_SENT); }

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24); }
static uint32_t get32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* a one-byte message, guaranteed (0x427930, 0x427a10-0x427a80) */
static void send_type(uint8_t type) { Net_Send(0, true, &type, 1); }

/* 0x4278f0: the 30 bytes of the splits after the type and a byte of padding; 32 bytes, not guaranteed */
void Net_SendSplits(const uint8_t splits[30])
{
    uint8_t m[0x20] = {0xb};
    memcpy(m + 2, splits, 30);
    Net_Send(0, false, m, sizeof m);
}

void Net_SendReady(void) { send_type(7); }

/* 0x427950: the stage time and, after it, the rally's total with it (0x40ac30) */
void Net_SendFinished(int32_t time)
{
    uint8_t m[0xc] = {10};
    put32(m + 4, (uint32_t)time);
    put32(m + 8, NetRec_Total() + (uint32_t)time);
    Net_Send(0, true, m, sizeof m);
}

/* 0x427990, 0x4279d0: a split's (not guaranteed) or a lap's time (guaranteed), its index in the second
   byte; the index kept as the last sent */
void Net_SendStageTime(int32_t k, int32_t t)
{
    gw32(STAGE_SENT, (uint32_t)k);
    uint8_t m[8] = {8, (uint8_t)k};
    put32(m + 4, (uint32_t)t);
    Net_Send(0, false, m, sizeof m);
}

void Net_SendLapTime(int32_t k, int32_t t)
{
    gw32(LAP_SENT, (uint32_t)k);
    uint8_t m[8] = {9, (uint8_t)k};
    put32(m + 4, (uint32_t)t);
    Net_Send(0, true, m, sizeof m);
}

void Net_SendResultsGo(void) { send_type(0xc); }

/* 0x427a30-0x427a80: the results' and the pause's choices, every record marked (0x40afd0); the restart
   also stops taking the splits */
void Net_SendContinue(void)
{
    send_type(0xd);
    NetRec_MarkAll();
}

void Net_SendRestart(void)
{
    send_type(0xe);
    Net_SetSplitsOn(false);
    NetRec_MarkAll();
}

void Net_SendService(void)
{
    send_type(0xf);
    NetRec_MarkAll();
}

/* 0x41b3a0: the stage's leaderboard points (the host's, when a board is in use): the lobby's list again
   (0x40ad20); with a best time in the first row, a point to that row and to every row with the same
   best, none to the others; then every row with none (so each is on the board), and the board sent */
static void stage_boards(void)
{
    NetRec_Lobby();
    int k = Fe_BoardCurrent();
    if (k == -1) return;
    if (Net_IsHost() && NetRec_LobbyBest(0) != 0xffffffffu) {
        Fe_BoardScore(k, NetRec_LobbyName(0), 1);
        for (int i = 1; i < (int)NetRec_LobbyCount(); i++)
            Fe_BoardScore(k, NetRec_LobbyName(i), NetRec_LobbyBest(i) == NetRec_LobbyBest(0));
    }
    for (int i = 0; i < (int)NetRec_LobbyCount(); i++) Fe_BoardScore(k, NetRec_LobbyName(i), 0);
    Net_SendBoard();
}

/* 0x449090, the results' fade's end in the network game (types 0xc and 0x10): the pause left, every
   race process to the results; the host the leaderboards (0x41b3a0) and the go to the others; the
   lobby's records, and the exit asked */
void Results_NetDone(int ch)
{
    (void)ch;
    if (Pause_Active()) Pause_End();   /* 0x404f20, 0x404ef0 */
    Race_EventAll(1, 3);
    if (Net_IsHost()) {
        stage_boards();
        Net_SendResultsGo();
    }
    NetRec_Lobby();   /* 0x40ad20 */
    Pause_Ask(false, false, true);   /* 0x41f2a0 */
}

/* 0x427680: the system messages: a player gone (DPSYS_DESTROYPLAYERORGROUP, 5) out of the players
   and the records; this game the host (DPSYS_HOST, 0x101): the session's, the pause's (0x402c90) and
   the results' pages again (0x44a1b0) */
static void sys_message(const DpSysMsg *s)
{
    if (s->type == 5) {
        Net_RemovePlayer(s->player);
        NetRec_Remove(s->player);
    } else if (s->type == 0x101) {
        Net_SetHost(1);
        Pause_NewHost();   /* 0x402c90 */
        Results_Start(true);
    }
}

/* 0x4276c0 */
static void message(uint32_t from, const uint8_t *m)
{
    int32_t colour = (int32_t)gm32(0x5199b0);
    switch (m[0]) {
    case 0:
        Fe_NetChat(from, (const char *)m + 1, false);   /* 0x4d0620 */
        return;
    case 6: NetRec_SetState(from, m[1]); return;                 /* 0x40ac70 */
    case 7: NetRec_SetReady(from); return;                       /* 0x409e90 */
    case 8: NetRec_StageTime(from, m[1], get32(m + 4)); return;  /* 0x409fd0 */
    case 9: NetRec_LapTime(from, m[1], get32(m + 4)); return;    /* 0x40a0e0 */
    case 10:
        NetRec_SetFinished(from);                        /* 0x409f80 */
        NetRec_Finished(from, get32(m + 4), get32(m + 8));   /* 0x409f00 */
        return;
    case 0xb:
        if (gm8(SPLITS_ON)) NetRec_Splits(from, m + 2);   /* 0x409d50 */
        return;
    case 0x10:
        gw32(0x537f34, Timer_Centis());
        /* fall through */
    case 0xc:
        Fade_OutIn(0, Results_NetDone, 1, 0, colour, true);
        return;
    case 0xd:
        Fade_OutIn(0, Results_ContinueDone, 1, 0, colour, true);
        return;
    case 0xe:
        Pause_Ask(true, false, false);   /* 0x41f280 */
        if (Gs_Mode() == 8) Pause_Ask(false, true, false);   /* 0x41f290 */
        else colour = (int32_t)gm32(FADE_COLOUR);
        Fade_OutIn(0, Pause_RestartDone, 1, 0, colour, true);
        NetRec_ClearReady();   /* 0x409bc0 */
        return;
    case 0xf:
        Pause_Ask(true, true, false);
        Fade_OutIn(0, Pause_ServiceDone, 1, 0, colour, true);
        return;
    case 0x11:
        NetRec_TakeBoard(m[1] != 0, m + 4);   /* 0x40afb0 */
        NetRec_Lobby();                       /* 0x40ad20 */
        return;
    }
}

/* 0x427890: every message waiting, a system message (from 0) or a player's */
void Net_Messages(void)
{
    uint32_t from;
    const uint8_t *m;
    while (Net_Receive(&from, &m)) {
        if (from == DPID_SYSMSG) sys_message((const DpSysMsg *)m);
        else message(from, m);
    }
}
