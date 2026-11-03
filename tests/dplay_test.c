/* The DirectPlay emulation (src/net/dplay.c) over the native build's in-memory relay: a host and a
   joiner in one process (Dp_Select), the session found, a wrong and a right password, the players and
   their system messages, directed and broadcast messages, a player's data, and the host leaving (the
   session's host moves to the joiner). Needs no game data. */
#include "net/dplay.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "dplay_test: line %d: %s\n", __LINE__, #c); fails++; } } while (0)

/* the next message for the selected DirectPlay (DPERR_NOMESSAGES when none) */
static uint32_t next(uint32_t *from, uint32_t *to, uint8_t *buf, uint32_t *len)
{
    *len = 4096;
    return Dp_Receive(from, to, buf, len);
}

int main(void)
{
    setenv("OPENCMR2_PARAM_RELAY", "ws://relay.test:9000/", 1);
    setenv("OPENCMR2_PARAM_ROOM", "cmr2", 1);
    uint8_t buf[4096];
    uint32_t from, to, len;

    /* the host */
    Dp_Select(0);
    CHECK(Dp_Create());
    const char *names[2];
    CHECK(Dp_Connections(names, 2) == 1);
    CHECK(Dp_InitializeConnection(0) == DP_OK);
    DpSession s[DP_SESSIONS];
    int n;
    CHECK(Dp_EnumSessions(s, DP_SESSIONS, &n) == DP_OK && n == 0);
    DpSession mine;
    memset(&mine, 0, sizeof mine);
    mine.flags = 0x2064, mine.max_players = 4;
    snprintf(mine.name, sizeof mine.name, "Rally");
    snprintf(mine.password, sizeof mine.password, "colin");
    mine.user[0] = 0x1234;
    CHECK(Dp_Open(&mine, DPOPEN_CREATE) == DP_OK);
    CHECK(Dp_IsHost());
    uint32_t h1;
    CHECK(Dp_CreatePlayer(&h1, "Ana", "Ana A", "car7", 4) == DP_OK);

    /* the joiner finds it */
    Dp_Select(1);
    CHECK(Dp_Create());
    CHECK(Dp_InitializeConnection(0) == DP_OK);
    CHECK(Dp_EnumSessions(s, DP_SESSIONS, &n) == DP_OK);   /* connects and says HERE */
    Dp_Select(0);
    CHECK(next(&from, &to, buf, &len) == DPERR_NOMESSAGES);   /* the host answers HERE */
    Dp_Select(1);
    CHECK(Dp_EnumSessions(s, DP_SESSIONS, &n) == DP_OK && n == 1);
    CHECK(n == 1 && !strcmp(s[0].name, "Rally") && s[0].user[0] == 0x1234 && s[0].max_players == 4);
    CHECK(n == 1 && (s[0].flags & 0x80000000u));   /* a password is needed */

    /* a wrong password, then the right one */
    DpSession want = s[0];
    snprintf(want.password, sizeof want.password, "mcrae");
    CHECK(Dp_Open(&want, DPOPEN_JOIN) == DPERR_CONNECTING);
    Dp_Select(0), next(&from, &to, buf, &len), Dp_Select(1);
    CHECK(Dp_Open(&want, DPOPEN_JOIN) == DPERR_INVALIDPASSWORD);
    snprintf(want.password, sizeof want.password, "colin");
    CHECK(Dp_Open(&want, DPOPEN_JOIN) == DPERR_CONNECTING);
    Dp_Select(0), next(&from, &to, buf, &len), Dp_Select(1);
    CHECK(Dp_Open(&want, DPOPEN_JOIN) == DP_OK);
    CHECK(!Dp_IsHost());
    const DpPlayer *pl[DP_PLAYERS];
    CHECK(Dp_Players(pl, DP_PLAYERS) == 1 && pl[0]->id == h1 && !strcmp(pl[0]->short_name, "Ana"));
    CHECK(pl[0]->data_len == 4 && !memcmp(pl[0]->data, "car7", 4) && !pl[0]->local);
    CHECK(next(&from, &to, buf, &len) == DPERR_NOMESSAGES);   /* the roster brings no system messages */
    uint32_t j1;
    CHECK(Dp_CreatePlayer(&j1, "Bo", "Bo B", NULL, 0) == DP_OK);

    /* the host hears of Bo */
    Dp_Select(0);
    CHECK(next(&from, &to, buf, &len) == DP_OK && from == DPID_SYSMSG);
    const DpSysMsg *m = (const DpSysMsg *)buf;
    CHECK(m->type == DPSYS_CREATEPLAYERORGROUP && m->player == j1 && !strcmp(m->short_name, "Bo"));
    CHECK(Dp_Players(pl, DP_PLAYERS) == 2 && pl[0]->id == h1);
    DpSession now;
    CHECK(Dp_GetSessionDesc(&now) == DP_OK && now.cur_players == 2);

    /* messages: directed, broadcast */
    CHECK(Dp_Send(h1, j1, true, "\x05hello", 6) == DP_OK);
    CHECK(Dp_Send(h1, DPID_ALLPLAYERS, true, "\x06", 1) == DP_OK);
    Dp_Select(1);
    CHECK(next(&from, &to, buf, &len) == DP_OK && from == h1 && to == j1 && len == 6 && !memcmp(buf, "\x05hello", 6));
    CHECK(next(&from, &to, buf, &len) == DP_OK && from == h1 && to == j1 && len == 1 && buf[0] == 6);
    CHECK(next(&from, &to, buf, &len) == DPERR_NOMESSAGES);
    len = 2;
    CHECK(Dp_Send(j1, h1, false, "0123456789", 10) == DP_OK);
    Dp_Select(0);
    CHECK(Dp_Receive(&from, &to, buf, &len) == DPERR_BUFFERTOOSMALL && len == 10);
    CHECK(next(&from, &to, buf, &len) == DP_OK && from == j1 && len == 10);

    /* a player's data; the session's description from the host */
    Dp_Select(1);
    CHECK(Dp_SetPlayerData(j1, "car3", 4) == DP_OK);
    CHECK(Dp_SetSessionDesc(&now) == DPERR_NOTHOST);
    Dp_Select(0);
    CHECK(Dp_Players(pl, DP_PLAYERS) == 2 && pl[1]->data_len == 4 && !memcmp(pl[1]->data, "car3", 4));
    now.user[1] = 77;
    CHECK(Dp_SetSessionDesc(&now) == DP_OK);
    Dp_Select(1);
    CHECK(next(&from, &to, buf, &len) == DP_OK && from == DPID_SYSMSG && ((DpSysMsg *)buf)->type == DPSYS_SETSESSIONDESC);
    CHECK(Dp_GetSessionDesc(&now) == DP_OK && now.user[1] == 77);

    /* the host leaves: Ana destroyed, Bo the host (DPSESSION_MIGRATEHOST, 0x4 of 0x2064) */
    Dp_Select(0);
    Dp_Release();
    Dp_Select(1);
    CHECK(next(&from, &to, buf, &len) == DP_OK && ((DpSysMsg *)buf)->type == DPSYS_DESTROYPLAYERORGROUP && ((DpSysMsg *)buf)->player == h1);
    CHECK(next(&from, &to, buf, &len) == DP_OK && ((DpSysMsg *)buf)->type == DPSYS_HOST && ((DpSysMsg *)buf)->player == j1);
    CHECK(Dp_IsHost());
    CHECK(Dp_Players(pl, DP_PLAYERS) == 1 && pl[0]->id == j1);
    Dp_Release();

    if (fails) return 1;
    printf("dplay_test: ok\n");
    return 0;
}
