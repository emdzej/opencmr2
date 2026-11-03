/* DirectPlay 4 as the game uses it (IDirectPlay4A, created by Net_CreateDirectPlay 0x4aaaf0), over a
   gasm-relay room (gasm:net, docs/re/network.md): one connection, the room's sessions, players, the
   messages and the system messages. The calls keep DirectPlay's results (DP_OK and the DPERR_ values
   the game tells apart) and its rules: a session's host, guaranteed and ordered sends (the relay's
   are all guaranteed), the host moving to another player when it leaves (DPSESSION_MIGRATEHOST).

   Where it can't be DirectPlay: sessions are found in the room the launch parameters name (relay=,
   room=), not on the local network; a pointer the original's structures hold is a C field here. */
#ifndef OPENCMR2_DPLAY_H
#define OPENCMR2_DPLAY_H
#include <stdbool.h>
#include <stdint.h>

/* DirectPlay's results: 0x88770000 | code (dplay.h's MAKE_DPHRESULT); the game's own checks confirm
   INVALIDPASSWORD 0x154 (0x4a10b0), CONNECTING 0x15e (0x4a12d0), BUFFERTOOSMALL and NOMESSAGES (0x4a1b90) */
#define DP_OK 0
#define DPERR_GENERIC 0x80004005u
#define DPERR_INVALIDPARAMS 0x80070057u
#define DPERR_ALREADYINITIALIZED 0x88770005u
#define DPERR_ACCESSDENIED 0x8877000au
#define DPERR_BUFFERTOOSMALL 0x8877001eu
#define DPERR_CANTCREATEPLAYER 0x8877003cu
#define DPERR_INVALIDPLAYER 0x88770096u
#define DPERR_NOCONNECTION 0x887700aau
#define DPERR_NOMESSAGES 0x887700beu
#define DPERR_NOSESSIONS 0x887700dcu
#define DPERR_UNINITIALIZED 0x88770140u
#define DPERR_NONEWPLAYERS 0x8877014au
#define DPERR_INVALIDPASSWORD 0x88770154u
#define DPERR_CONNECTING 0x8877015eu
#define DPERR_CONNECTIONLOST 0x88770168u

#define DPID_SYSMSG 0u
#define DPID_ALLPLAYERS 0u

/* the system messages (DPSYS_*), from DPID_SYSMSG */
#define DPSYS_CREATEPLAYERORGROUP 0x0003u
#define DPSYS_DESTROYPLAYERORGROUP 0x0005u
#define DPSYS_SESSIONLOST 0x0031u
#define DPSYS_HOST 0x0101u
#define DPSYS_SETPLAYERORGROUPDATA 0x0102u
#define DPSYS_SETSESSIONDESC 0x0104u

/* DPSESSION_ (DirectPlay sets PASSWORDREQUIRED in the sessions it finds) */
#define DPSESSION_MIGRATEHOST 0x4u
#define DPSESSION_JOINDISABLED 0x20u
#define DPSESSION_NODATAMESSAGES 0x80u
#define DPSESSION_PASSWORDREQUIRED 0x400u

/* DPOPEN_ */
#define DPOPEN_JOIN 1u
#define DPOPEN_CREATE 2u

enum { DP_NAME = 64, DP_DATA = 256, DP_PLAYERS = 16, DP_SESSIONS = 8 };

typedef struct {
    uint32_t flags;
    uint8_t instance[16];
    uint32_t max_players, cur_players;
    char name[DP_NAME], password[DP_NAME];
    uint32_t user[4];
} DpSession;

typedef struct {
    uint32_t id;
    char short_name[DP_NAME], long_name[DP_NAME];
    uint8_t data[DP_DATA];
    uint32_t data_len;
    bool local;
} DpPlayer;

/* a system message as Dp_Receive returns it (from DPID_SYSMSG) */
typedef struct {
    uint32_t type;
    uint32_t player;                         /* the player created or destroyed, the new host's */
    char short_name[DP_NAME], long_name[DP_NAME];
    uint8_t data[DP_DATA];                   /* the player's data (CREATEPLAYER, SETPLAYERORGROUPDATA) */
    uint32_t data_len;
    uint32_t cur_players;                    /* the session's players after it (CREATEPLAYER) */
    DpSession session;                       /* DPSYS_SETSESSIONDESC */
} DpSysMsg;

bool Dp_Create(void);   /* CoCreateInstance: false without gasm:net or a relay (relay= unset) */
void Dp_Release(void);
bool Dp_Created(void);
/* IDirectPlay4::EnumConnections: the connections' names; one, the relay */
int Dp_Connections(const char **names, int cap);
uint32_t Dp_InitializeConnection(int i);
/* IDirectPlay4::EnumSessions with DPENUMSESSIONS_ASYNC: the sessions heard of so far (the first
   call connects and asks; each call after takes in what came); DPERR_CONNECTING while connecting */
uint32_t Dp_EnumSessions(DpSession *out, int cap, int *n);
uint32_t Dp_Open(const DpSession *s, uint32_t how);
uint32_t Dp_Close(void);
uint32_t Dp_GetSessionDesc(DpSession *s);
uint32_t Dp_SetSessionDesc(const DpSession *s);
uint32_t Dp_CreatePlayer(uint32_t *id, const char *short_name, const char *long_name, const void *data, uint32_t len);
uint32_t Dp_DestroyPlayer(uint32_t id);
uint32_t Dp_SetPlayerData(uint32_t id, const void *data, uint32_t len);
/* IDirectPlay4::EnumPlayers: the session's players, the host's first */
int Dp_Players(const DpPlayer **out, int cap);
uint32_t Dp_Send(uint32_t from, uint32_t to, bool guaranteed, const void *data, uint32_t len);
/* IDirectPlay4::Receive (DPRECEIVE_ALL): the next message; a system message is a DpSysMsg from
   DPID_SYSMSG; DPERR_BUFFERTOOSMALL with *len its size when it doesn't fit */
uint32_t Dp_Receive(uint32_t *from, uint32_t *to, void *buf, uint32_t *len);
bool Dp_IsHost(void);
void Dp_Select(int k);   /* the tests: which of two DirectPlays the calls go to (0 by default) */
#endif
