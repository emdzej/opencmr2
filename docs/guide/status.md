# Status

OpenCMR2 plays Colin McRae Rally 2.0 from your own CD, offline, from the start-up movies to the end of a
championship. The network game is being ported: it is not playable yet.

## What works

| Area | State |
|---|---|
| Rally | Single stages, full rallies and championships: the service area (repairs, set-up, stage information, saving), times, points, unlocks, the rally and championship tables, saved championships |
| Arcade | The circuits against computer drivers, the location's races, points and standings |
| Time trial | With the ghost of the best run |
| Challenge | The knockout on the super special stages, against the computer drivers |
| Two players | Split screen, and two or more players taking turns on a stage |
| Driving | The original's handling model, damage (dents, worn handling, broken glass, a misfiring engine), rolls and recovery onto the road, car-to-car contacts, scenery that falls over |
| On the stage | Every stage of the eight rallies, the weather (rain, snow, fog), the time of day and the headlights, the start lights, dust, mud, splashes and skid marks, the reflections on the cars |
| Cameras and HUD | Chase, interior, cockpit and trackside cameras; the clock, splits, standings, map, gauges and the pace-note arrows |
| Sound | Engines, gearbox, surfaces, collisions, the co-driver's pace notes, the menu music and sounds |
| Front end | Every menu, the options, controller set-up (keyboard and game controllers), driver profiles, high scores, the movies |
| Replays | Watch a stage again after the finish |
| Display | 640 x 480 to 2048 x 1536 at 16 or 32 bits, with the original's 1024 artwork from 1024 x 768 up |

## Not yet

- **The network game.** The menus, the lobby and the race messages are in; the other players' cars on
  the stage are being finished.

## Known issues

- **The disc's demo runs** (shown when the main menu is left alone) leave their recorded path before the
  finish. The original's own code, run on the same state, does the same, so the recordings may come from
  another version of the game.
- **The menus don't grow with the resolution:** at high resolutions they are small, as in the original.

Found something that plays differently from the original? Open an issue on
[GitHub](https://github.com/emdzej/opencmr2/issues).
