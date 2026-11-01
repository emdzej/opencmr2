# Front end

The menus: the top-level mode 0 of `Game_Frame` 0x4d0780. They use 917 functions (0x4cf000-0x510000,
248 KB of code), most of them page callbacks. The port is in `src/fe/`.

## The process

Mode 0 runs one process: group 0x817da0, state table 0x523bc8, transitions 0x523c18. Each transition is
`(state, kind, event, next)`. The states are:

| State | Update | Draw | What |
|---|---|---|---|
| 0 | 0x4d15e0 | none | The start-up. The first time (0x516120 set) it does the full loading (event 1). Back from a game it reloads (event 0, or 6 for the demo). |
| 1 | 0x4d1b40 | 0x4d1080 | The title and copyright textures, for 5 s |
| 2 | 0x4d1b40 | 0x4d1370 | The Bink logo and credit line, for 5 s |
| 3, 4 | 0x4d1ba0, 0x4d1c30 | none | The `cm.bik` and `Intro.bik` movies |
| 5 | 0x4d1a90 | 0x4d0ea0 | The title, until select or 5 s; then the menu music starts |
| 6 | 0x4d1c90 | 0x4d0a80 | The menus: `Menu_Input` on the current page, and the page's draw |
| 7 | 0x4d1cc0 | 0x4d0ba0 | A rally's movie, before the first stage of a championship or single rally |
| 8 | 0x4d1e10 | none | Three pauses of 5 s |
| 9 | 0x4d1e90 | none | Out of the front end, to mode 2 (the stage set-up) or mode 3 (the race) |

Event 0 from state 6 goes to state 7, and event 2 goes to state 9. The start page's frame callback
(0x4ef590) calls 0x4d2070, which posts event 0 with the way out recorded: on to the game, or for the
demo, to the set-up or to the race.

## The start-up

The first run of 0x4d15e0:

1. The territory comes from the registry's `Sku_Type`. The CD's is Europe, which gives five languages.
2. The options' defaults (0x510410) and the input devices.
3. The archives (0x4d2380): `FrontEnd\Common.bfl`, `FrontEnd\Res640.bfl`,
   `CountrySpecific\<region>\FERes640E.bfl` and each language's text archive.
4. The textures: title, copyright, Bink, arrows, dot matrices, banners, flags, medals and the cars'
   blueprints (0x4e2e50, 0x4d2590).
5. The five menu sounds from `Sounds\menu`. Four fonts: `hel_12pt`, `hel_15pt`, `hel_36pt` and `dot`.
   The 533 strings of `f<language>.txt`. The credits.
6. Every page (`Pages_Build` 0x4e9f70).
7. The stage maps (0x4d5ca0): `<rally><nn>.dmd` from `Common.bfl`.

Paths are built from the formats in the exe and the directories of `Paths_Init` 0x40e8d0 (`src/game/paths.c`).

## The menu engine

The engine is at 0x49ff50-0x4a0c50 (`src/fe/menu.c`). A page is a 0x1e0-byte record in the BSS:

| Offset | Field |
|---|---|
| +0x00 | A title string, or NULL |
| +0x04 | The title's text number |
| +0x06 | The item count |
| +0x07 | The selection |
| +0x08 | The parent page |
| +0x0c | The first selection for `Menu_Reset`, -1 none |
| +0x0d | 1: the items run up and down. 0: they run across. |
| +0x0e | Flags: 1 the selection moves, 2 values change, 4 select works, 8 back works, 0x10 wrap, 0x40 no parent |
| +0x0f | The last move's direction |
| +0x10 | The value shared by type 6 items |
| +0x14 | 22 items of 0x14 bytes each |
| +0x1cc | enter |
| +0x1d0 | frame |
| +0x1d4 | draw |
| +0x1d8 | leave |
| +0x1dc | The hooks |

An item has these fields: a label pointer, the text number (+4), flags (+6: 1 enabled, 2 selectable,
4 wraps, 8 select steps the value), the type (+7), an id (+8), the value count and the value (+0xa,
+0xb), the target page (+0xc) and a function (+0x10). The types are 1 back, 2 page, 3 list, 4 action
and 6 shared list.

The input bits are those of the input record in the menus' key mode (0x49f480): 1 left, 2 right, 4 up,
8 down, 0x10 Enter, 0x20 Escape. A page whose items run across moves its selection with left and right
and changes values with up and down; one whose items run up and down does the opposite. A pending page
change runs the old page's leave callback and the new page's enter callback (with "back" set when the
move goes back), and plays the set's sounds.

## The pages

There are 94 constructors (0x4f5520-0x4fa780). Each one calls the builders (0x49ffd0, 0x4a00a0-0x4a02e0,
0x4a0430). The port's `src/fe/pages.c` was translated from them, then checked call by call against the
push sequences in the disassembly. The way to a stage:

| Page | What |
|---|---|
| 0x8221d8 | The language, first run only; a waving flag in dots (0x4d28c0) |
| 0x81d6d8 | The main menu: drivers, rally, arcade, network, options, credits, quit |
| 0x823a38 | Mode select: championship, single rally, single stage, time trial, challenge |
| 0x823fd8 | Difficulty (Expert locked until unlocked, 0x52b03c) |
| 0x81f2f8 | Number of players, drawn as dot figures |
| 0x8241b8 | Driver select: load a profile, create one, or play as a guest |
| 0x824a38 | Tag entry: three letters, typed or picked from the letter grid |
| 0x821e18 | Car select: the profile's open cars, with front and back blueprints, livery, power and handling bars |
| 0x81cf58 | Transmission |
| 0x81eb78 | Country select, with the rallies open (0x4f02e0) |
| 0x81e3f8 | Stage select: the map, location, length, surfaces and records |
| 0x81f4d8 | Weather (single stage): random, or the rally's weathers |
| 0x8214b8 | The start |

Most pages share two drawings:

- The item strip (0x4d2cd0 and the main menu's 0x4d45b0): the items in a row near the bottom of the
  screen, the selected one at x = 102 of 640. A new selection slides into place over 250 ms. The slide
  is kept in twelve scroller records (0x819140, 0x78 bytes apart).
- The column list (0x4d3360): arrowed rows between separator lines.

## Leaving

State 9 does the following before it sets mode 2 or 3:

1. It sets up the championship's tables (0x4cf060) and the options' mode byte (0x4eabc0).
2. It chooses the rally's weather (0x40dc30, `src/game/weather.c`): each stage's start and end
   weather, from the rally's three tables of ten types; the light levels; the rain.
3. It copies each car's set-up from the rally's table (0x406820).
4. It writes the selection into the mode word.
5. It frees the front end's memory and textures.

## In the port

The port differs from the original in these places:

- The Bink movies are skipped.
- The music streams as the original (8 segments of 0xfe80 bytes), decoded by the port's MS ADPCM decoder in place of the ACM.
- Nothing is saved: the game info, the profiles, the leaderboards and the controller configuration.
- Mode 2 (`World_Frame` 0x4ff450, the set-up and service screens) is not ported. The race starts
  straight away through `src/game/play.c`, and returns to the menus after the stage.
- 206 page callbacks are still logged stubs (`src/fe/stubs.c`): mostly the options, the profiles, the
  championship, the arcade and the network pages.

## The race's menus

The race uses the same menu engine for two more sets of pages.

The pause menu is at 0x401000-0x405500 (`src/game/pause.c`). Its pages are built at the race's start
(0x403500):

- 0x52ad60, the main page: graphics, sound, controller, restart, exit and continue. The controller row
  is passed over: 0x402bf0 moves the selection off it.
- 0x52a670: the music, effects and co-driver volumes.
- 0x529af8 and 0x52a0c0: the restart and exit confirmations.
- The graphics, camera and display pages.

Escape (key binding 10, bit 0x400 of the race's input) pauses the game on the line, in the countdown and
on the stage. While paused the race is not updated, but the fades still run. Restart and exit start a
fade on every car. At its end the fade posts the race's event 1, which goes to the results state, and
sets a flag: 0x53810c for a restart, 0x53810d for an exit. The results state (0x41e8d0) then either puts
the cars back on the line (0x41e6b0, then event 4) or leaves for the front end.

The stage's results are at 0x448e00-0x454400 (`src/game/results.c`). They are drawn over the replay
(0x44b330 in state 10's draw). Their input runs in each step of state 10 (0x448e70). They are started by
state 9 once every player has finished (0x44a1b0). For one player on a single stage the pages are:

1. The replay's title and the driver (0x44b7b0).
2. The stage time page (0x44bcd0): each split's time and the total, beside the stage record's.
3. "Congratulations" (0x44d790), only when the time beat the record. 0x4cfa10 writes the new record.
4. The standings board against the computer drivers (0x44d960).
5. The choices (0x44af70, 0x453c50): restart, restart at the service area, quit. Each fades out and posts
   the race's event 1, as the pause menu does.

The computer drivers' times come from the stage's `.tm` file for the difficulty (Stage_LoadTm 0x455300,
`src/game/opponents.c`). The file holds sixteen drivers, each with a base, a random range and dry and
wet adjustments, and each split's dry and wet times. From these, 0x455470-0x456960 work out:

- each driver's rating, whose order is the final standings;
- each driver's split times, shaken within the difficulty's limits.

The players' times go in at their splits (0x455590). The weather's change along the stage is not ported,
so the model takes the stage as dry throughout. The other modes' result pages are not ported yet.
