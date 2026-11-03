/* The game's network layer (0x4a0c60-0x4a1cb0, 0x4aa880-0x4aad70) over the DirectPlay emulation
   (net/dplay.c). The original keeps a DPSESSIONDESC2 (0x50 bytes: size, flags, the instance and
   application GUIDs at +8 and +0x18, the players at +0x28/+0x2c, the name and password pointers at
   +0x30/+0x34, dwUser1-4 at +0x40) for its own session at 0x5a0068 (name 0x5a00b8, password 0x5a02c0)
   and one for each session found (0x59fa20, twenty, names at 0x5a03c4 + 0x104 each); its players
   (0x5a1820: seven of 0xd0 bytes, short name +0, long name +0x64, id +0xc8, in use +0xcc); the
   connections (0x664650: ten of 0x114 bytes, the name, the connection's data pointer +0x100, the
   service provider's GUID +0x104). All of it is here at those addresses; a pointer in them is a
   game-state address, and the connection's data is DirectPlay's own (its index here).

   Where it can't be the original: DirectPlay's Open of a session to join waits for the host's answer,
   which an asynchronous transport can't; Net_Join says NET_PENDING and the caller asks again. */
#include "net/session.h"
#include "net/dplay.h"
#include "exe.h"
#include "game/mem.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    HOST = 0x5a0060, DESC = 0x5a0068, DESC_NAME = 0x5a00b8, DESC_PASSWORD = 0x5a02c0,
    FOUND = 0x59fa20, FOUND_NAMES = 0x5a03c4, NFOUND = 0x5a01bc, IN_SESSION = 0x5a1814,
    NPLAYERS = 0x5a1818, PLAYERS = 0x5a1820, LOCAL = 0x5a1ea0, DPNAME = 0x5a1fa8, RECV_SIZE = 0x5a1fbc,
    CREATED = 0x5a1fc0, CONNS = 0x664650, CONN_SEL = 0x665118, NCONNS = 0x665119, DPLAY = 0x66521c,
    LOBBY = 0x665220, EMPTY = 0x52ea5c,
    APP_GUID = 0x511cd8,   /* the game's application GUID (exe .rdata) */
};

static uint8_t *recv_buf;   /* 0x5a1fb8 (GlobalAlloc'd) */

static void gstr(uint32_t va, const char *s, uint32_t cap)
{
    size_t n = strlen(s);
    if (n >= cap) n = cap - 1;
    memcpy(gm(va), s, n), gw8(va + (uint32_t)n, 0);
}
static const char *gs(uint32_t va) { return va ? (const char *)gm(va) : ""; }

/* a DPSESSIONDESC2 in the game state to DirectPlay's, and back (the strings through their pointers) */
static void desc_get(uint32_t va, DpSession *s)
{
    memset(s, 0, sizeof *s);
    s->flags = gm32(va + 4);
    memcpy(s->instance, gm(va + 8), 16);
    s->max_players = gm32(va + 0x28), s->cur_players = gm32(va + 0x2c);
    snprintf(s->name, sizeof s->name, "%s", gs(gm32(va + 0x30)));
    snprintf(s->password, sizeof s->password, "%s", gs(gm32(va + 0x34)));
    for (int k = 0; k < 4; k++) s->user[k] = gm32(va + 0x40 + 4u * (uint32_t)k);
}
static void desc_put(uint32_t va, const DpSession *s)   /* the fields, not the pointers */
{
    gw32(va, 0x50), gw32(va + 4, s->flags);
    memcpy(gm(va + 8), s->instance, 16);
    gw32(va + 0x28, s->max_players), gw32(va + 0x2c, s->cur_players);
    for (int k = 0; k < 4; k++) gw32(va + 0x40 + 4u * (uint32_t)k, s->user[k]);
}
static void desc_clear(void)
{
    memset(gm(DESC), 0, 0x50);
    for (uint32_t k = 0; k < 16; k += 4) gw32(DESC + 0x18 + k, exe_u32(APP_GUID + k));
    gw32(DESC, 0x50);
}

/* 0x4a0c60 */
void Net_ClearSessions(void)
{
    gw32(DESC + 0x30, DESC_NAME);
    memset(gm(FOUND), 0, 400 * 4);
    gw32(DESC + 0x34, DESC_PASSWORD);
    gw32(HOST, 0), gw32(IN_SESSION, 0);
    gw8(NFOUND, 0);
}

/* 0x4a0ca0: a session found, kept if it has a name and there is room */
static void add_session(const DpSession *s)
{
    int n = gm8(NFOUND);
    if (n >= 20 || !s->name[0]) return;
    uint32_t e = FOUND + 0x50u * (uint32_t)n;
    desc_put(e, s);
    for (uint32_t k = 0; k < 16; k += 4) gw32(e + 0x18 + k, exe_u32(APP_GUID + k));
    gstr(FOUND_NAMES + 0x104u * (uint32_t)n, s->name, 0x104);
    gw32(e + 0x30, FOUND_NAMES + 0x104u * (uint32_t)n);
    gw8(NFOUND, (uint8_t)(n + 1));
}

/* 0x4a0d60: the session's description from DirectPlay (the name copied, its pointer kept) */
bool Net_GetSessionDesc(void)
{
    if (!gm32(DPLAY)) return false;
    DpSession s;
    if (Dp_GetSessionDesc(&s) != DP_OK) return false;
    uint32_t name = gm32(DESC + 0x30), pw = gm32(DESC + 0x34);
    desc_put(DESC, &s);
    gw32(DESC + 0x30, name), gw32(DESC + 0x34, pw);
    gstr(DESC_NAME, s.name, 0x208);
    return true;
}

/* 0x4a0ec0: host a session (flags 0x2064: joining disabled until 0x4a1570, keep-alive, the host moving) */
bool Net_Host(uint32_t name, uint32_t password, uint32_t u1, uint32_t u2, uint32_t u3, uint32_t u4, uint32_t max)
{
    desc_clear();
    gw32(DESC + 0x28, max);
    gw32(DESC + 4, 0x2064);
    gstr(DESC_NAME, gs(name), 0x208);
    gw32(DESC + 0x30, DESC_NAME);
    gstr(DESC_PASSWORD, gs(password), 0x104);
    gw32(DESC + 0x34, DESC_PASSWORD);
    gw32(DESC + 0x40, u1), gw32(DESC + 0x44, u2), gw32(DESC + 0x48, u3), gw32(DESC + 0x4c, u4);
    if (!gm32(DPLAY)) return false;
    DpSession s;
    desc_get(DESC, &s);
    if (Dp_Open(&s, DPOPEN_CREATE) != DP_OK) return false;
    gw32(HOST, 1), gw32(IN_SESSION, 1);
    return true;
}

/* 0x4a10b0: what Open's result means to the game: 0 failed, 1 joined; the errors it doesn't list
   count as joined (the original's quirk) */
static int join_result(uint32_t r)
{
    switch (r) {
    case 0x887700f0: case 0x88770118: case 0x88770140: case 0x8877014a: case DPERR_NOCONNECTION:
    case 0x88770078: case DPERR_ACCESSDENIED: case DPERR_INVALIDPARAMS: case DPERR_ALREADYINITIALIZED:
    case 0x887707ee: case 0x887707da: case DPERR_CONNECTING: case 0x887707d0: case 0x887707e4:
    case 0x88770820: case 0x887707f8: case 0x8877080c:
        return 0;
    }
    return 1;
}

/* 0x4a10b0: join found session i with a password (a game-state address) */
int Net_Join(int i, uint32_t password, bool *bad_password)
{
    *bad_password = false;
    if (i >= gm8(NFOUND)) return 0;
    memset(gm(DESC), 0, 0x50);
    gw32(DESC, 0x50);
    gw32(DESC + 0x30, DESC_NAME);
    memcpy(gm(DESC + 8), gm(FOUND + 0x50u * (uint32_t)i + 8), 16);
    gw32(DESC + 0x34, password);
    if (!gm32(DPLAY)) return 0;
    DpSession s;
    desc_get(DESC, &s);
    uint32_t r = Dp_Open(&s, DPOPEN_JOIN);
    if (r == DPERR_CONNECTING) return NET_PENDING;   /* the port's (see the top) */
    if (r == DPERR_INVALIDPASSWORD) {
        *bad_password = true;
        return 0;
    }
    if (r == DP_OK) {
        gw32(IN_SESSION, 1);
        Net_GetSessionDesc();
        return 1;
    }
    return join_result(r);
}

/* 0x4a1280 */
bool Net_Close(void)
{
    if (!gm32(DPLAY) || Dp_Close() != DP_OK) return false;
    gw32(IN_SESSION, 0);
    return true;
}

/* 0x4a12d0, 0x4a13b0: the sessions (DPENUMSESSIONS_ASYNC: the ones heard of so far, each through
   0x4a12b0 into 0x4a0ca0), or the asking stopped (DPENUMSESSIONS_STOPASYNC) */
static int enum_sessions(uint32_t password, bool stop)
{
    if (gm32(IN_SESSION)) return 0;
    Net_ClearSessions();
    desc_clear();
    gw32(DESC + 0x34, password);
    gw32(DESC + 0x30, DESC_NAME);
    if (!gm32(DPLAY)) return 0;
    DpSession found[DP_SESSIONS];
    int n = 0;
    uint32_t r = Dp_EnumSessions(found, DP_SESSIONS, &n);
    if (r == DP_OK && !stop)
        for (int i = 0; i < n; i++)
            if (!(found[i].flags & DPSESSION_JOINDISABLED)) add_session(&found[i]);   /* DPENUMSESSIONS_AVAILABLE */
    if (r == DP_OK) return 1;
    if (r == DPERR_CONNECTING) return -1;
    if (r == 0x88770118) return -2;   /* DPERR_USERCANCEL */
    return 0;
}
int Net_EnumSessions(uint32_t password) { return enum_sessions(password, false); }
int Net_StopEnumSessions(void) { return enum_sessions(gm32(DESC + 0x34), true); }

int Net_Sessions(void) { return gm8(NFOUND); }               /* 0x4a1480 */
uint32_t Net_SessionName(void) { return gm32(DESC + 0x30); }      /* 0x4a14c0 */
uint32_t Net_SessionPassword(void) { return gm32(DESC + 0x34); }  /* 0x4a14d0 */

/* 0x4a14e0 */
bool Net_SetSessionDesc(void)
{
    if (!gm32(DPLAY)) return false;
    DpSession s;
    desc_get(DESC, &s);
    return Dp_SetSessionDesc(&s) == DP_OK;
}

/* 0x4a1510, 0x4a1530, 0x4a1550, 0x4a1570: a field changed, the description read first and set after */
void Net_SetSessionName(uint32_t name) { Net_GetSessionDesc(); gw32(DESC + 0x30, name); Net_SetSessionDesc(); }
void Net_SetPassword(uint32_t password) { Net_GetSessionDesc(); gw32(DESC + 0x34, password); Net_SetSessionDesc(); }
void Net_SetMaxPlayers(uint32_t n) { Net_GetSessionDesc(); gw32(DESC + 0x28, n); Net_SetSessionDesc(); }
void Net_SetJoinable(bool on)
{
    Net_GetSessionDesc();
    if (on) gw32(DESC + 4, gm32(DESC + 4) & ~DPSESSION_JOINDISABLED);
    else gw32(DESC + 4, gm32(DESC + 4) | DPSESSION_JOINDISABLED);
    Net_SetSessionDesc();
}

uint32_t Net_IsHost(void) { return gm32(HOST); }       /* 0x4a15a0 */
void Net_SetHost(uint32_t v) { gw32(HOST, v); }        /* 0x4a15b0 */

/* 0x4a15c0: a found session's instance GUID */
bool Net_SessionInstance(int i, uint32_t out[4])
{
    if (i >= gm8(NFOUND)) return false;
    for (uint32_t k = 0; k < 4; k++) out[k] = gm32(FOUND + 0x50u * (uint32_t)i + 8 + 4 * k);
    return true;
}

/* 0x4a1610: a found session's dwUser1-4 (k past 2 the fourth) */
uint32_t Net_FoundUser(int i, int k)
{
    return gm32(FOUND + 0x50u * (uint8_t)i + 0x40 + 4u * (uint32_t)(k > 3 || k < 0 ? 3 : k));
}

/* 0x4a1680, 0x4a16c0: the session's dwUser1-4 (read from DirectPlay first) */
uint32_t Net_User(int k)
{
    Net_GetSessionDesc();
    return gm32(DESC + 0x40 + 4u * (uint32_t)(k > 3 || k < 0 ? 3 : k));
}
void Net_SetUser(int k, uint32_t v)
{
    Net_GetSessionDesc();
    gw32(DESC + 0x40 + 4u * (uint32_t)(k > 3 || k < 0 ? 3 : k), v);
    Net_SetSessionDesc();
}

/* 0x4a1720: a found session's players, or (i < 0) the session's */
uint32_t Net_FoundPlayers(int i) { return i < 0 ? gm32(DESC + 0x2c) : gm32(FOUND + 0x50u * (uint32_t)i + 0x2c); }
uint32_t Net_FoundMaxPlayers(int i) { return gm32(FOUND + 0x50u * (uint8_t)i + 0x28); }   /* 0x4a1740 */
/* 0x4a1790: DPSESSION_PASSWORDREQUIRED */
bool Net_FoundPassworded(int i) { return gm32(FOUND + 0x50u * (uint8_t)i + 4) >> 10 & 1; }

/* 0x4a1760: the description a DPSYS_SETSESSIONDESC brings */
void Net_TakeSessionDesc(const DpSession *s)
{
    desc_put(DESC, s);
    for (uint32_t k = 0; k < 16; k += 4) gw32(DESC + 0x18 + k, exe_u32(APP_GUID + k));
    /* the pointers back to the game's buffers, the name in them not copied (the original's) */
    gw32(DESC + 0x30, DESC_NAME), gw32(DESC + 0x34, DESC_PASSWORD);
}

/* 0x4a17b0 */
void Net_FreeReceive(void)
{
    gw32(RECV_SIZE, 0);
    free(recv_buf);
    recv_buf = NULL;
}

/* 0x4a17f0: the players emptied (all: the local player's made flag too) */
void Net_ClearPlayers(bool all)
{
    if (all) gw8(CREATED, 0);
    for (uint32_t i = 0; i < 7; i++) {
        uint32_t p = PLAYERS + 0xd0 * i;
        gstr(p, gs(EMPTY), 100), gstr(p + 0x64, gs(EMPTY), 100);
        gw32(p + 0xc8, 0), gw32(p + 0xcc, 0);
    }
    gw32(NPLAYERS, 0);
}

/* 0x4a1850: another player (not the local one, not twice) into the first free slot */
void Net_AddPlayer(const char *short_name, const char *long_name, uint32_t id)
{
    if (gm32(LOCAL) == id) return;
    for (uint32_t i = 0; i < 7; i++)
        if (gm32(PLAYERS + 0xd0 * i + 0xc8) == id) return;
    if (gms32(NPLAYERS) >= 7) return;
    uint32_t i = 0;
    while (gm32(PLAYERS + 0xd0 * i + 0xcc)) if (++i == 7) return;
    uint32_t p = PLAYERS + 0xd0 * i;
    if (short_name) gstr(p, short_name, 100);
    if (long_name) gstr(p + 0x64, long_name, 100);
    gw32(p + 0xc8, id), gw32(p + 0xcc, 1);
    gw32(NPLAYERS, gm32(NPLAYERS) + 1);
}

/* 0x4a1940 */
void Net_RemovePlayer(uint32_t id)
{
    for (uint32_t i = 0; i < 7; i++) {
        uint32_t p = PLAYERS + 0xd0 * i;
        if (gm32(p + 0xc8) != id) continue;
        gstr(p, gs(EMPTY), 100), gstr(p + 0x64, gs(EMPTY), 100);
        gw32(p + 0xc8, 0), gw32(p + 0xcc, 0);
        gw32(NPLAYERS, gm32(NPLAYERS) - 1);
        return;
    }
}

/* 0x4a19c0 */
bool Net_PlayerSlot(uint32_t id, uint8_t *slot)
{
    for (uint32_t i = 0; i < 7; i++)
        if (gm32(PLAYERS + 0xd0 * i + 0xc8) == id) {
            *slot = (uint8_t)i;
            return true;
        }
    *slot = 0;
    return false;
}

uint32_t Net_LocalPlayer(void) { return gm32(LOCAL); }   /* 0x4a1a00 */

/* 0x4a1a10: the local player (its DPNAME at 0x5a1fa8) */
bool Net_CreatePlayer(uint32_t short_name, uint32_t long_name, const void *data, uint32_t len)
{
    gw32(DPNAME, 0x10), gw32(DPNAME + 4, 0), gw32(DPNAME + 8, short_name), gw32(DPNAME + 12, long_name);
    if (!gm32(DPLAY)) return false;
    uint32_t id = 0;
    if (Dp_CreatePlayer(&id, gs(short_name), gs(long_name), data, len) != DP_OK) return false;
    gw32(LOCAL, id);
    gw8(CREATED, 1);
    return true;
}

/* 0x4a1a90 (the made flag stays) */
bool Net_DestroyPlayer(void)
{
    return gm8(CREATED) && gm32(DPLAY) && Dp_DestroyPlayer(gm32(LOCAL)) == DP_OK;
}

/* 0x4a1af0: the players, each through 0x4a1ad0 into 0x4a1850 */
bool Net_EnumPlayers(void)
{
    Net_ClearPlayers(false);
    if (!gm32(DPLAY)) return false;
    const DpPlayer *pl[DP_PLAYERS];
    int n = Dp_Players(pl, DP_PLAYERS);
    for (int i = 0; i < n; i++) Net_AddPlayer(pl[i]->short_name, pl[i]->long_name, pl[i]->id);
    return true;
}

/* 0x4a1b30, 0x4a1b60 */
const char *Net_PlayerShortName(int slot)
{
    uint32_t p = PLAYERS + 0xd0u * (uint8_t)slot;
    return gm32(p + 0xcc) ? (const char *)gm(p) : NULL;
}
const char *Net_PlayerLongName(int slot)
{
    uint32_t p = PLAYERS + 0xd0u * (uint8_t)slot;
    return gm32(p + 0xcc) ? (const char *)gm(p + 0x64) : NULL;
}

/* 0x4a1b90: the next message into the buffer; one that doesn't fit grows the buffer and waits for
   the next call (so a loop over the messages stops there until the next frame) */
bool Net_Receive(uint32_t *from, const uint8_t **msg)
{
    if (!gm32(DPLAY)) return false;
    uint32_t size = gm32(RECV_SIZE), to = 0;
    uint32_t r = Dp_Receive(from, &to, recv_buf, &size);
    if (r == DPERR_BUFFERTOOSMALL) {
        free(recv_buf);
        recv_buf = malloc(size);
        if (recv_buf) gw32(RECV_SIZE, size);
        return false;
    }
    if (r != DP_OK) return false;
    *msg = recv_buf;
    return true;
}

/* 0x4a1c50: to a player (0 all), guaranteed if asked (DPSEND_GUARANTEED) */
bool Net_Send(uint32_t to, bool guaranteed, const void *data, uint32_t len)
{
    return gm32(DPLAY) && Dp_Send(gm32(LOCAL), to, guaranteed, data, len) == DP_OK;
}

/* 0x4a1cb0 (DPSET_GUARANTEED) */
bool Net_SetPlayerData(const void *data, uint32_t len)
{
    return gm32(DPLAY) && Dp_SetPlayerData(gm32(LOCAL), data, len) == DP_OK;
}

/* ---- the connections (0x4aa880-0x4aad70) ---- */

/* 0x4aa880 */
static void clear_connections(void)
{
    for (uint32_t i = 0; i < 10; i++) {
        uint32_t c = CONNS + 0x114 * i;
        gstr(c, gs(EMPTY), 0x100);
        memset(gm(c + 0x100), 0, 0x14);
    }
    gw8(NCONNS, 0), gw8(CONN_SEL, 10);
}

/* 0x4aa930: a connection (name, its data, the service provider), the list sorted after by 0x4aa8e0
   (DirectPlay's TCP/IP provider, GUID 0x511a38, first; the port's one connection has no GUID) */
static void add_connection(const char *name, uint32_t data)
{
    uint32_t n = gm8(NCONNS);
    if (n >= 10) return;
    uint32_t c = CONNS + 0x114 * n;
    gstr(c, name, 0x100);
    memset(gm(c + 0x104), 0, 16);
    gw32(c + 0x100, data + 1);   /* (a pointer there: non-zero) */
    gw8(NCONNS, (uint8_t)(n + 1));
}

/* 0x4aaa40 (CoInitialize; 0x4aaac0 at exit) */
bool Net_Init(void)
{
    gw32(DPLAY, 0), gw32(LOBBY, 0);
    Net_ClearSessions();
    Net_ClearPlayers(true);
    Net_FreeReceive();
    for (uint32_t i = 0; i < 10; i++) gstr(CONNS + 0x114 * i, gs(EMPTY), 0x100), gw32(CONNS + 0x114 * i + 0x100, 0);
    gw8(NCONNS, 0);
    return true;
}

/* 0x4aaac0 */
void Net_Shutdown(void)
{
    Net_DestroyPlayer();
    Net_ReleaseLobby();
    Net_ReleaseDirectPlay();
    for (uint32_t i = 0; i < 10; i++) gw32(CONNS + 0x114 * i + 0x100, 0);   /* 0x4aaa10 */
    Net_FreeReceive();
}

/* 0x4aaaf0, 0x4aab40 */
bool Net_CreateDirectPlay(void)
{
    if (!Dp_Create()) return false;
    gw32(DPLAY, 1);
    return true;
}
void Net_ReleaseDirectPlay(void)
{
    if (!gm32(DPLAY)) return;
    Dp_Release();
    gw32(DPLAY, 0);
}

/* 0x4aab60, 0x4aabb0: the lobby object (IDirectPlayLobby3A), made and released; the port has no lobby */
bool Net_CreateLobby(void) { gw32(LOBBY, 1); return true; }
void Net_ReleaseLobby(void) { gw32(LOBBY, 0); }

/* 0x4aac00: the connections, each through 0x4aabd0 into 0x4aa930 */
bool Net_EnumConnections(void)
{
    clear_connections();
    if (!gm32(DPLAY)) return false;
    const char *names[10];
    int n = Dp_Connections(names, 10);
    for (int i = 0; i < n; i++) add_connection(names[i], (uint32_t)i);
    return true;
}

/* 0x4aac40: connection i initialised (and chosen) */
uint32_t Net_InitializeConnection(int i)
{
    if (i >= gm8(NCONNS)) return 0;
    uint32_t r = Dp_InitializeConnection((int)gm32(CONNS + 0x114u * (uint32_t)i + 0x100) - 1);
    if (r == DP_OK) {
        gw8(CONN_SEL, (uint8_t)i);
        return 1;
    }
    if (r == DPERR_ALREADYINITIALIZED) {
        gw8(CONN_SEL, (uint8_t)i);
        return 0x88770001;
    }
    return r & 0xffffff00;
}

int Net_Connections(void) { return gm8(NCONNS); }   /* 0x4aacf0 */
const char *Net_ConnectionName(int i) { return i < gm8(NCONNS) ? (const char *)gm(CONNS + 0x114u * (uint32_t)i) : NULL; }   /* 0x4aad00 */
bool Net_HaveDirectPlay(void) { return gm32(DPLAY) != 0; }   /* 0x4aad40 */
