# The challenge (mode 4)

A knockout of head-to-head matches. Every round runs on the rally's super special (stage 10, set by
`Sel_ChallengeStage` 0x4070c0 from the record's rally, bits 16-19). Port: `src/game/challenge.c`,
`src/game/tween.c`, the mode 4 branches of `src/game/race.c` and `src/game/results.c`.

## The record (0x52f2b4)

| Bits | What |
|---|---|
| 0-2 | the depth (1: the draw starts at the final) |
| 3-5 | the round: 1 the last sixteen, 2 the quarter finals, 3 the semi finals, 4 the final |
| 6-8 | the opponents' level |
| 12-15 | the match in the round |
| 16-19 | the rally |
| 21 | the round over (on to the next round) |
| 22 | the round's matches all run |
| 23 | the challenge given up |

The matches follow, 12 bytes each: the final at 0x52f2b8, the semi finals at 0x52f2c4, the quarter
finals at 0x52f2dc, the last sixteen at 0x52f30c. A match's word: bits 0-4 the first driver, 5-9 the
second (0x1f: nobody, a bye), 10 run, 11 the first won, 12 the second won; then the two times
(1/100 s). The drivers are the record's slots at 0x52f3e0 (0xc4 bytes apart; the name first). In
mode 4 `Player_Driver` 0x408500 is -1 for a slot below 8 whose name is not empty, and `Player_Name`
0x408400 is that name (confirmed).

## Race state 6 (0x472e00, drawn by 0x4759d0)

State 5's end in mode 4 sets the menus' input (0x49ea90) and gives event 5 (to state 6). Each step
holds both fades full (0x428410 family), steps the pages' input (0x473470: polls 0x49eab0, no input
while the round tweens run) once they are built, and reads the grow (0x58ca80) and lines (0x58cf60)
tweens. Its sub-state is 0x58cf7c:

| Sub | What |
|---|---|
| 0 | tweens cleared; the round's grow and lines started in the last sixteen; the pages built (0x473360); pads beyond the players cleared |
| 1 | waits for "begin round" (0x58cf64); the opponents' times new (0x455470(1)) |
| 2 | a bye ends at once (to 5); else the match's players and pads (0x472cb0), the replays set up again; a match with a player to 4, two computers' match ended (0x472a30) to 3 |
| 3 | the times count up (0x4752f0, 0x27 a call) until both arrive; the lines again; to 5 |
| 4 | the race rebuilt at the line (0x41b310 until 0x41b320); then the fade to the countdown (0x473540) |
| 5 | while the round has matches left, to 2; the final to 7; else the grow over the next round (0x58cc70), to 6 |
| 6 | the round's tween, then the next's (0x58ca8c) |
| 7 | the round over: tweens cleared, the round's number up (bits 3-5), bit 21 set, the stage again (0x4070c0), the quit asked (0x41f2a0) |

The player's match ends in the results (0x44a1b0 case 4): the match noted (0x541cd0), ended
(0x472a30), the title on to the times (0x44cdb0), then the result page (0x53f1e8, drawn by 0x4529c0),
then the choices (0x44af70 with 0x4728e0, restart text 0x27). The lost result's line names the
match's first side whoever it is (a quirk kept).

## The ways out of the results (0x41e8d0, mode 4)

`Race_EndReplay` 0x41f2b0 asks at once: unless given up (bit 23), the match's restart kept
(0x4728b0) and asked (0x41f280); else the quit (0x41f2a0).

- Restart asked, not kept (the pause's): the carried damage dropped, every process to state 0, the
  challenge's draw again (0x4069c0) and state 6 from its start (0x472ca0).
- Restart kept: the stage again from the line (0x41e6b0 with 1: event 4, then state 5 to 6).
- Quit: unless the challenge is over (0x4729f0) the next round's cars (event 7: state 4, the stage
  kept); over, every process to the front end.
- Else (kinds 2 and 3): unless kept, every process to state 0; the keep cleared (0x4728c0).

The port rebuilds the race on each of these (Play_End and Race_Frame); `Race_Reentry` says which
way it came back in, so the set-up state and Stage_Load skip what the original's way does not do
again (the resets; on event 7 also the opponents' times).

## Not ported

- The computer opponent's car in mode 4 (Car_Load: a random car other than 0x51a8b8's, 0x40eea0,
  0x408600).
- The name entry of a second player and the two players' match (kind 3) are untested.
