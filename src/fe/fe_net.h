/* The front end's network game (fe/fe_net.c). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

void Fe_NetSendMe(void);         /* 0x4ec2b0: the local player's data */
void Fe_NetSendSettings(void);   /* 0x4ec460: message 3, the host's settings */
void Fe_NetMessages(void);       /* 0x4ec8d0 */
void Fe_NetChatClear(void);      /* 0x4d05f0 */
bool Fe_NetChat(uint32_t from, const char *text, bool local);   /* 0x4d0620 */
const char *Fe_NetChatLine(void);   /* 0x4edb50: the line being typed */
int Fe_BoardCurrent(void);   /* 0x40e4f0: the stored leaderboard in use, -1 none */
void Fe_BoardScore(int k, const char *name, int n);   /* 0x40e660 */
void Fe_NetSay(const char *text);     /* 0x4d0700: the local player's chat line sent and shown */
const char *Fe_NetChatRow(int i);    /* 0x4d06f0: chat line i (0 the newest) */
