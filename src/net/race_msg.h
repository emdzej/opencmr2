/* The race's network messages (0x427620-0x427aa0; net/race_msg.c): what each player sends the others
   during a stage (a type byte, then the type's fields) and the loop that takes them in. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

void Net_SetSplitsOn(bool on);                   /* 0x427640: DAT_00539cc8, type 0xb taken */
void Net_ForgetTimes(void);                      /* 0x427650: the last stage and lap time sent, none */
int32_t Net_LastStageTime(void);                 /* 0x427660: DAT_00539ed0 */
int32_t Net_LastLapTime(void);                   /* 0x427670: DAT_00539dcc */
void Net_SendSplits(const uint8_t splits[30]);   /* 0x4278f0: type 0xb */
void Net_SendReady(void);                        /* 0x427930: type 7 */
void Net_SendFinished(int32_t time);             /* 0x427950: type 10, the time and the total with it */
void Net_SendStageTime(int32_t k, int32_t t);    /* 0x427990: type 8 */
void Net_SendLapTime(int32_t k, int32_t t);      /* 0x4279d0: type 9 */
void Net_StageBoards(void);                      /* 0x41b3a0: the stage's leaderboard points, sent */
void Net_SendResultsGo(void);                    /* 0x427a10: type 0xc */
void Net_SendContinue(void);                     /* 0x427a30: type 0xd */
void Net_SendRestart(void);                      /* 0x427a50: type 0xe */
void Net_SendService(void);                      /* 0x427a80: type 0xf */
void Net_Messages(void);                         /* 0x427890 */
void Results_NetDone(int ch);                    /* 0x449090: the network results' fade end */
