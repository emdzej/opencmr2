/* The game's network layer over DirectPlay (0x4a0c60-0x4a1cb0, 0x4aa880-0x4aad70; docs/re/network.md):
   the connections, the session the game hosts or joins (its DPSESSIONDESC2 at 0x5a0068, the settings in
   its dwUser1-4), the sessions found (0x59fa20), the other players (0x5a1820), the local player
   (0x5a1ea0), and the messages. Its state is the original's, in the game state at its addresses
   (game/mem.h); DirectPlay is src/net/dplay.c. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "net/dplay.h"

enum { NET_PENDING = 2 };   /* the port's: a join waiting for the host's answer (the original's Open waits) */

bool Net_Init(void);                          /* 0x4aaa40 */
void Net_Shutdown(void);                      /* 0x4aaac0 */
bool Net_CreateDirectPlay(void);              /* 0x4aaaf0 */
void Net_ReleaseDirectPlay(void);             /* 0x4aab40 */
bool Net_CreateLobby(void);                   /* 0x4aab60 */
void Net_ReleaseLobby(void);                  /* 0x4aabb0 */
bool Net_EnumConnections(void);               /* 0x4aac00 */
uint32_t Net_InitializeConnection(int i);     /* 0x4aac40: 1, 0x88770001 already initialised, or 0 */
int Net_Connections(void);                    /* 0x4aacf0 */
const char *Net_ConnectionName(int i);        /* 0x4aad00 */
bool Net_HaveDirectPlay(void);                /* 0x4aad40 (non-null) */

void Net_ClearSessions(void);                 /* 0x4a0c60 */
bool Net_GetSessionDesc(void);                /* 0x4a0d60 */
bool Net_Host(uint32_t name, uint32_t password, uint32_t u1, uint32_t u2, uint32_t u3, uint32_t u4, uint32_t max);   /* 0x4a0ec0 (names are game-state addresses) */
int Net_Join(int i, uint32_t password, bool *bad_password);   /* 0x4a10b0: 0, 1, NET_PENDING */
bool Net_Close(void);                         /* 0x4a1280 */
int Net_EnumSessions(uint32_t password);      /* 0x4a12d0: 1, 0, -1 connecting, -2 cancelled */
int Net_StopEnumSessions(void);               /* 0x4a13b0 */
int Net_Sessions(void);                       /* 0x4a1480 */
uint32_t Net_SessionName(void);               /* 0x4a14c0: the address of the session's name */
uint32_t Net_SessionPassword(void);           /* 0x4a14d0 */
bool Net_SetSessionDesc(void);                /* 0x4a14e0 */
bool Net_SetSessionName(uint32_t name);       /* 0x4a1510 */
bool Net_SetPassword(uint32_t password);      /* 0x4a1530 */
bool Net_SetMaxPlayers(uint32_t n);           /* 0x4a1550 */
bool Net_SetJoinable(bool on);                /* 0x4a1570 */
uint32_t Net_IsHost(void);                    /* 0x4a15a0 */
void Net_SetHost(uint32_t v);                 /* 0x4a15b0 */
bool Net_SessionInstance(int i, uint32_t out[4]);   /* 0x4a15c0 */
uint32_t Net_FoundUser(int i, int k);         /* 0x4a1610: a found session's dwUser1-4 */
uint32_t Net_User(int k);                     /* 0x4a1680 */
void Net_SetUser(int k, uint32_t v);          /* 0x4a16c0 */
uint32_t Net_FoundPlayers(int i);             /* 0x4a1720 (-1: the session's) */
uint32_t Net_FoundMaxPlayers(int i);          /* 0x4a1740 */
bool Net_FoundPassworded(int i);              /* 0x4a1790 */
void Net_TakeSessionDesc(const DpSession *s); /* 0x4a1760 */

void Net_FreeReceive(void);                   /* 0x4a17b0 */
void Net_ClearPlayers(bool all);              /* 0x4a17f0 */
void Net_AddPlayer(const char *short_name, const char *long_name, uint32_t id);   /* 0x4a1850 */
void Net_RemovePlayer(uint32_t id);           /* 0x4a1940 */
bool Net_PlayerSlot(uint32_t id, uint8_t *slot);   /* 0x4a19c0 */
uint32_t Net_LocalPlayer(void);               /* 0x4a1a00 */
bool Net_CreatePlayer(const char *short_name, const char *long_name, const void *data, uint32_t len);   /* 0x4a1a10 */
bool Net_DestroyPlayer(void);                 /* 0x4a1a90 */
bool Net_EnumPlayers(void);                   /* 0x4a1af0 */
const char *Net_PlayerShortName(int slot);    /* 0x4a1b30 */
const char *Net_PlayerLongName(int slot);     /* 0x4a1b60 */
/* 0x4a1b90: the next message; *from 0 for a system message (a DpSysMsg, net/dplay.h) */
bool Net_Receive(uint32_t *from, const uint8_t **msg);
bool Net_Send(uint32_t to, bool guaranteed, const void *data, uint32_t len);   /* 0x4a1c50 */
bool Net_SetPlayerData(const void *data, uint32_t len);   /* 0x4a1cb0 */
