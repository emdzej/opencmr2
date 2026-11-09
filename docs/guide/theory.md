# How Colin McRae Rally 2.0 works

A theory of operation of Colin McRae Rally 2.0 (Codemasters, 2000): how the game is built, from the
frame loop to the stage times. It comes from porting the Windows executable function by function. It
is meant for anyone who wants to learn how a rally game of its time is put together. The details, with
the exe's addresses and layouts, are in the repository's reverse-engineering notes
([`docs/re`](https://github.com/emdzej/opencmr2/tree/main/docs/re)) and format pages
([`docs/formats`](https://github.com/emdzej/opencmr2/tree/main/docs/formats)). Where something is not
understood yet, this page says so.

## The shape of the program

The game is one Windows executable of 1.2 MB, written in C-style code: free functions working on
global state, with no game objects behind virtual tables. Next to the game's own code are a few
statically linked libraries: zlib for reading every data file, Microsoft's D3DX helper for setting up
Direct3D, and the C runtime. The movies are played by the Bink library, a DLL beside the game. The
platform is Win32 with DirectX 7: DirectDraw and Direct3D for the picture, DirectSound for the sound,
DirectInput for the keyboard, pads and wheels, and DirectPlay for the network game. The game starts
no threads of its own.

About 3,600 functions make up the program. By size, its parts are:

| Part | What it does |
|---|---|
| Front end | The menus, car and stage selection, options, driver profiles, saves, credits, the network lobby. The largest part by far |
| World mode | Between the menus and the race: the stage's information, the weather forecast, the service area, the loading screens |
| Race flow | The race's state machine, the stage loading, the replays |
| Simulation | The cars: controls, suspension, tyres, drivetrain, the body's motion, collisions, damage |
| Effects | Particles, weather, lamps, skid marks, mud, the sky and the time of day |
| AI | The computer drivers of the arcade races, and the times of the rally's opponents |
| HUD and co-driver | The clock, splits, gauges, pace-note arrows, and the co-driver's voice |
| Renderer | The device, textures, models, the triangle queues, 2D |
| Platform | The window, files, registry, timers, sound buffers, input |

Almost all state lives in fixed global arrays: there is no heap of game objects. A car is a block of a
little over 3 KB in an array of cars, and every subsystem reads and writes its fields by offset. Around
the arrays are hundreds of tiny accessor functions (get the game mode, get the number of players, is
this option on). This makes the program easy to save and restore, and its memory use is known before
it starts.

### The loop and the modes

`WinMain` creates the window and runs the usual Windows message loop. When there is no message, it
polls the input and calls the game's frame function once. Nothing blocks inside a frame except
loading, which runs from a state's update and holds the frame until it is done. This shape, one frame
per call, is what lets the port run on a runtime that calls the game once per displayed frame.

The frame function dispatches on the top-level mode:

```
mode 0  front end    the menus, from the start-up movies to "start the stage"
mode 2  world        stage information, forecast, service area, loading screens
mode 3  race         the stage itself, its replay and results
```

Each mode is run by the same small **process framework**. A group of processes shares a table of
states, each state a pair of functions (update and draw), and a table of transitions: (state, kind,
event, next state). Every frame the framework calls each process's update, then each process's draw,
then applies a pending transition. Code moves a process on by posting an event; the transition table,
not the code, decides where it goes. A transition has a priority, so a stronger event (a failed load,
a quit) wins over a weaker one posted in the same frame.

The race runs **one process per player** in the same table. With two players on a split screen, or
several taking turns, each player's view has its own state: one can still be driving while the other
has finished and is watching the replay. The race's states, in order: clear, the loading screen, mount
the stage's archive, load the stage, load the cars, place them on the line, the countdown, the stage,
after the finish, the replay, the results. A side branch runs the intro camera that flies along the
stage before the start.

## Time

The car simulation runs at a **fixed 25 steps a second**: one step is 40 ms. Each race frame asks how
many steps are due since the last frame, from three latched times (the race's start, the last frame,
this frame). It runs at most five, so a long stall does not make the cars jump. On a pause or a
restart the last frame's time is reset, so no steps are owed afterwards.

Each step, every car's controls are produced (from the player's input, the AI or a replay, depending
on who drives the car). The replay records or plays that step's controls. Then the whole world is
stepped once: the cars, the collisions, the damage, the progress along the stage. The fraction of the
next step that has passed is kept. After the steps, the cars' and wheels' drawn poses are blended
between the last two steps by that fraction, so the picture is smooth at any frame rate while the
physics stay at 25 Hz. The cameras step at the same rate and are blended the same way.

The **stage clock is the step count**: the time shown is the steps since the countdown's start, times
4 hundredths of a second. The countdown and the timing are exact in steps, whatever the frame rate. The
five-second countdown is 500 hundredths of a second of steps with the cars held on the line.

What fades per frame rather than per step (the lamps' lenses, the weather's drift) is scaled by the
measured frame rate instead.

The simulation is **16.16 fixed point** throughout: a 32-bit integer with 16 bits after the point,
multiplied through a 64-bit product shifted back by 16. The renderer is floating point, as Direct3D
wants. A handful of conversions in the simulation go through the FPU (a degree-to-angle scale, the
step count from milliseconds) and round to nearest. The port has to round them the same way, or a car
drifts from its recorded path. Fixed point gives the same results on every machine, which is what
makes the next point possible.

**Replays are input recordings.** A replay holds the car's state at its start and then the controls
of every step, run-length coded in four bytes. To play one back, the game restores the car and feeds
the recorded controls into the same simulation, which drives the same run again. The disc's demo runs
and the championship's closing sequence are replays of this kind, and the time trial's ghost is
another car driven by the best run's recording. The only randomness in a stage is the C runtime's
`rand()`. The computer drivers seed it from the clock, the demo from zero. The port's checks rest on
this: a replay played through the ported simulation must give back the original's run. One open
question: the disc's demo runs leave their recorded path before the finish, in the original's own code
run on the same state too, so they may come from another build of the game.

## The data

Every data file goes through one loader. The file is a gzip stream, which is optional: zlib passes a
plain file through. Inside it is a four-letter `CMPR` tag, the payload's size and the payload. Most
payloads are **archives**: the members back to back, then a directory of (size, offset, name) at the
end, found from the last four bytes. The game reads a whole file into memory and uses most of it in
place, as arrays of fixed-size records. A collision mesh, a centre line, a wall list or a model is used
straight from the bytes it was loaded into. Pointers inside a model are fixed up once after loading.
The layout is [BFL](https://github.com/emdzej/opencmr2/blob/main/docs/formats/bfl.md).

Archives are mounted into numbered slots (the common data, the front end's resolution set, the stage,
the cars), and members are found by name in the slot. A texture is found by name across the mounted
archives. This is how the high and low detail versions of a stage, or the 640 and 1024 resolution
sets of the menus, swap without the code knowing.

What is **data** and what is **code** is telling:

- **Data:** the stages (meshes, collision, walls, centre line, pace notes, scenery, lights, horizon
  colours for twelve times of day and four kinds of weather), the cars' models in several levels of
  detail, their light and effect emitters, the computer drivers' time tables and the arcade's racing
  lines, the sounds, the speech, the music, the movies, the fonts, the text in five languages.
- **Code:** the handling. Each car has a handling class, and the class's dozen base values are
  constants in the car set-up function, not a file. The set-up (suspension, gears, brakes,
  differential) the player picks in the service area adjusts them. The surfaces' grip, the walls'
  restitution and friction, and the camera offsets are tables inside the executable.

So the handling was tuned in the code, and the content (stages, cars, voices) was built as files by
the art and design side. The port loads the exe's own tables from the player's copy at run time rather
than carrying them.

The models are one format for everything: stages, scenery, cars, the sky and the ground. A model is a
tree of named objects (a car's wheels, bonnet camera, light effects, interior), each with meshes of
textured polygons. A vertex carries two sets of texture coordinates and two colours. The format is
[C3D](https://github.com/emdzej/opencmr2/blob/main/docs/formats/c3d.md).

## The world

### The stage

A stage is a strip of land a few kilometres long, not an open world. Its mesh is cut into a square
grid of cells, 64 units a side on the stages checked, each cell holding one mesh. Only the cells along
the route exist. The grid is the spatial index for everything:

- **drawing**: the cells near the camera are listed each frame, and only their meshes and objects are
  drawn. World culling is by distance (the draw distance option and the stage's own hints along the
  route); no view-frustum test has been found;
- **collision**: each cell lists its walls and scenery volumes, and a car tests only the cells it
  touches;
- **lighting**: a cell's vertex colours are relit when the light colours change.

The ground under a car comes from a separate **collision mesh**: a grid of 256-unit cells, each split
down into 4 x 4 children up to five levels deep, the leaves holding triangles with a surface code. A
query finds the triangle under a point and its plane, and keeps the last triangle so the next query
usually finds it at once.

The sides of the road are **walls**: vertical line segments from one point to another, spanning every
height, each with a normal, a type and caps at its ends. The type chooses the restitution and friction
of a hit, and a few types mean something else: off the stage, a soft bank that bumps the wheels and
slows the car, or a timer that marks the car off the stage after a second.

Trees, posts and other scenery carry **collision volumes**: upright cylinders (most of them,
hundreds of thousands over the disc) or boxes. Some are fixed; some can be knocked over, and those
fall when a car hits them and stay down for that car.

The **centre line** is a list of points along the route. Every car's progress is the point it has
reached: it steps on when the next point is behind it along the line's direction. The flagged points
are the splits and the finish. The centre line also drives the co-driver (each point can carry a pace
note), the recovery (where to put a car back), the weather's changes (from a fifth of the way to the
finish), and the computer drivers' lines.

### Light and weather

A stage is run at a time of day and with weather from the rally's programme. Each stage carries
horizon records, colour and fog sets, for twelve times from 5:00 to 24:00 and four kinds of sky (clear,
cloudy, storm, blizzard). The race blends the records for the start's time and weather, and blends
again towards the end's as the car progresses, so a stage can get darker or start to rain on the way.
The weather sets a wet value per car that reduces grip. Rain and snow are drawn per player, and the
lamps shine on a wet road. At night the cars' headlights, the stage's lamps and the glows of lights
are drawn. These are a large part of the game's look.

## The gameplay

### The car

A car is a rigid body with four wheels, and its state is the large per-car block. Each step runs the
same passes over every car, in an order the game keeps strictly:

```
grip         each corner's grip from its surface, blended towards the wet surface by the weather
controls     the inputs (steering, throttle, brake, handbrake, gear) into the car's controls
suspension   two body-sway springs driven by the change of velocity; the wheels' loads
ground       the ground's normal force and friction at each touching corner
heading      the front wheels turned by the steering angle
drivetrain   the engine's torque split to the wheels; then the tyres
tyres        per wheel: spin against ground speed, slip, force inside a friction circle
integrate    forces and torques summed; velocity, spin, position and orientation stepped
revs         the engine's revs from the driven wheels through the gear, or free when out of gear
```

Then the collisions run (walls, scenery, other cars). Then the **road-following pass**, the heart of
how the car feels. While a car is on the road in the normal way, its body is re-aligned each step to
the ground under its corners. The up axis is eased half way to the ground's normal, the car is lifted
out of the ground by its deepest corner, the velocity into the ground is removed, and the pitch and
roll spins are cut hard. The car follows the road's shape and stays settled. When a corner leaves the
ground, the change of the ground's slope gives it a turn instead. A slope too steep, a hard knock or a
slide past a limit **breaks the car loose**: from then on a free rigid body takes it, tumbling and
rolling, until it settles on its wheels again. Two models, a constrained one for driving and a free
one for crashes, each simple, together give a car that is easy to drive and still rolls convincingly.

The tyres are the classic shape: each wheel's longitudinal and lateral slip make a force, limited by a
friction circle and the grip ellipse of the wheel's surface, with rolling resistance. The port keeps
two quirks of the original here: a wheel without a valid heading reuses the previous wheel's slips,
and the grip limit compares the force against one axis's limit but takes the other's. Both are part of
how the cars drive.

The handling classes, the set-up and the damage all feed the same passes as factors: softer springs,
other gear ratios, a damaged steering rack that pulls to one side, a worn brake.

### Collisions

A car is a box. For the walls and objects the game uses a flat version of it: its forward and side
axes on the ground, its half lengths, its four corners at the top. Against a wall, the corners past
the wall's line are found and the box is pushed back out by the deepest. If the car moves into the
wall, its velocity bounces by the wall type's restitution and loses some of its slide to friction. A
wall's end is handled as a post. Against a cylinder or a box volume, the car is pushed back along its
motion by the time it overlapped.

Every impulse also gives the car a turn about the contact point, and a strong one makes it loose. A
hard knock is taken lower on the car and lifts it a little, which is what flips a car that hits a tree
at speed. Car-to-car contacts share each push between the two cars, and the hit is fully inelastic:
the cars leave the contact at their common, mass-weighted velocity. Sliding contact throws sparks.

### Damage

Damage has three layers, each driven by the hits the collisions report:

- **Dents.** The car's model carries its crumple zones in its vertex colours: each vertex's red, green
  and blue (less a middle value) are how far it may move in x, y and z. A hit moves the vertices near
  it inwards along the hit's normal, within those limits. A flat hit makes a flat dent with a falloff;
  a post or a tree makes a round one. Each hit is first written to a log in a compact form, then read
  back from the log to make the dent, so the dent from the log is exactly the dent made in the race.
  The log is how damage carries over to the next stage and is replayed onto the fresh model there.
- **Handling.** A 3 x 3 grid over the car collects the hits' strength, and 34 panels read it, each
  with a sensitivity. The panels give the factors the simulation reads: the steering's pull, the
  wheels' drag, the brakes, the engine, the body's crumple. A badly damaged engine **misfires**: it
  runs for a while and cuts out for a while, the length of each phase random and scaled by the damage.
- **Parts and glass.** Glass panels crack and then break past their limits (their textures change and
  a sound plays). Up to four panels per car (bonnet, boot, doors, depending on the model) come loose
  past a threshold: they swing on a hinge with a spring, hang from one end, scrape the road, and fall
  off.

Between stages of a rally the damage is kept: the log and the panels go with the player to the service
area. There the player repairs parts in the time the service allows, and the next stage starts with
what is left.

### Off the road and back

A car is marked off the stage by certain walls, by too long on a surface that means "off", with no
corner on the collision mesh, or upside down for too long. The screen then fades out, the car is put
back on the centre line at the point it had reached, stepped back past points another car is near,
facing along the line, at rest, in first gear. The fade hides the jump.

### Rallies, opponents and scoring

In a rally the player drives alone: the other drivers are **not simulated**. Their stage times are
made up from a table per stage and difficulty. Each of sixteen drivers has a base, a random range and
adjustments for dry and wet, with times at each split. The stage's weather and a random draw within the
range give each driver's time at each split. The player's split times go into the same list as they
are reached, so the HUD can show the place at each split. This is cheap, cannot go wrong on a narrow
stage, and gives a credible field. The championship keeps points and total times for the sixteen
drivers, with a count kept as the tie-break of equal points.

What a run leaves behind: a player's profile keeps the medals per stage and rally, the cars driven,
the podiums, and the best times. The game's tables keep the stage records and the top fives. Some
results open bonus cars and stages.

### The arcade's computer drivers

The arcade's circuits are the only place where computer cars drive. They are driven by **small neural
networks**. Each circuit has three sets of data, one per difficulty, each giving every car a set of
racing lines (heading, speed and position at each centre-line point) and maps that tie the outputs of
two tiny networks to the controls. One network steers, from the car's speed, its slide and its heading
against the line's here and two points on. The other sets throttle and brake from the speed against
the line's. Every layer goes through the same squashing curve, a tanh-like table. The code looks
meant to skip it on the last layer but never does, a quirk the port keeps.

Around the networks are rules. A car near a verge or turned across the road gets a fixed recovery
reply instead of the networks' output. A car overtakes another that is slower and close ahead, within
an angle and a clear width. Three rules cut throttle or steering. An output above zero presses its
control. The AI drives through the same control inputs as the player, so its car runs through the same
physics.

At the start the computer cars blip their throttles at random on the line, and some get revs up for a
better launch. That draw comes from `rand()` seeded from the clock, so each race differs.

### The co-driver

The pace notes are data: each stage has a list of calls (severity, direction, distance, joins,
modifiers), tied to centre-line points. As the car passes a point, its call is queued and spoken from
some sixty recorded words and phrases, and shown as arrows on the HUD. The countdown is spoken too.
The notes follow the car's progress, not time, so they stay right whatever the speed.

### Game modes

The same race code serves every mode. A **selection word** holds the rally, the stage and a set of
bits that turn features on: AI cars, the ghost, head to head, laps. The race's code asks those bits
rather than the mode. A rally is stages with service in between. A time trial is one stage with the
ghost. The arcade is lap races against the AI. The challenge is a knockout on the super special
stages against computer drivers, head to head. Two players drive on a split screen or take turns.
The network game is a rally with each player's car driven on their own machine.

## The network game

Each player's machine simulates **only its own car**. Each step it sends its car's state (place, orientation, velocity, spin, revs, steering, a few flags)
in a 30-byte record, without guaranteed delivery. Every other machine shows that car through a
predictor: it extrapolates from the last state received and, when a new one arrives, eases the shown
car towards the new prediction over a few frames rather than jumping. The remote cars are moved from
outside the simulation and take part in its contacts. Split times, finishes, stage and rally results,
the ready and continue choices, chat and the leaderboards travel as small typed messages. The host
decides the rally, its stages and the weather. If the host leaves, another player takes over.

This is the usual design for a racing game of its time: no lockstep, no shared physics. A late packet
costs a little smoothness on the screen of the player watching, never the driver's own handling.

## Saving

The game writes a few kinds of files: the options and controller set-up, the driver profiles (with
their records and medals), the championships in progress (the stage reached, the standings) and the replays and ghosts. A save is the image of the game's own structure, written
and read whole. A championship is saved from the service area, between stages, never during one.

## Presentation

### Rendering

The renderer is fixed-function Direct3D 7 with hardware transform and lighting. Models are drawn from
vertex buffers built at load time, in batches by texture, with two texture stages: a base texture and
a second (the reflections among its uses). The stage's vertex colours carry its lighting (prelit when the
stage was built), and the device's lights add the dynamic ones (the cars' headlights, the stage's
lamps).

A frame of the race is drawn in a fixed order:

```
sky and ground       z-buffer off, behind everything
terrain              the visible cells, with linear fog
tree shadows         polygons in the trees' textures laid on the ground
glows, queues        world-space triangles queued by the effects (skids, mud, shadows, particles)
scenery and cars     the objects, then the sorted ones far to near
sprites              particles, rain, snow
2D                   the HUD, the messages, the fades, in layers
```

The effects do not draw directly. They queue world-space triangles into four buckets, each drawn at
its own point in the frame with its own blending. This keeps the render state changes few and the
order fixed.

Shadows are a mix: tree shadows are polygons prepared with the stage; the cars' shadows are drawn onto
the ground under them. The cars' reflections use an environment map, from a fixed texture or, with the
advanced option, a six-face cube map rendered around the car each frame (the most expensive thing the
game draws).

### HUD, menus and movies

The HUD is 2D drawn over the view: the stage clock, the split times, the place,
the rev counter and speedometer, the gear, the progress map, and the co-driver's arrows. All are
placed from the screen's width and height, against a 640 x 480 layout scaled up.

The menus run on a small **menu engine**. A page is a record: a title, up to 22 items, a parent page,
flags for how the selection moves, and four callbacks (enter, frame, draw, leave). An item is a back
link, a link to a page, a list of values, or an action. Some 94 pages are built at start-up, and the
engine handles moving, values, entering and leaving with their sounds. Everything specific is in the
callbacks. The menus come in two artwork sets, for 640 and 1024 pixels wide. The 3D in the menus (the
cars, the letters running round the main menu) uses the same renderer as the race.

The movies (the logos, the intro, each rally's introduction) are Bink files, decoded frame by frame.

### Sound

Every sound is a buffer: the effects are WAV members of the archives, loaded into up to 200 sounds and
played on up to 32 voices, with volume, pan and pitch. The **engine** is three pairs of loops (slow,
medium off the throttle, fast on the throttle), each pitched and mixed by tables of revs. Gear whine
and detonations go over them, and everything fades with the camera's distance. Each wheel's
**surface** picks one of the stage's sound sets, inside or outside the car, with skids and transitions
when the surface changes. Collisions pick a sound by the hit's strength from swipes, crashes, rolls,
landings and breaking glass, on four slots that take the oldest when full. The **music** is one ADPCM
file streamed into a looping buffer in eight segments, refilled behind the play cursor each frame. The
co-driver's speech is queued so calls don't talk over each other.

## Lessons for building such a game

- **Step the physics at a fixed rate and blend the picture.** 25 steps a second with interpolated poses
  gives smooth drawing at any frame rate, exact timing, and runs that can be replayed.
- **Record inputs, not positions.** With a deterministic simulation a replay is four bytes a step, the
  ghost is a replay, and the attract mode is a replay. The same recording is the best test of a port.
- **Two models for a car.** A road-following model for driving and a free rigid body for crashes are each
  simple. Switching between them on clear thresholds gives handling that is stable and crashes that
  look right.
- **Fake what the player can't check.** The rally's opponents are a table and a random draw, not cars on
  the stage. The player only sees their times, and the times are credible.
- **Let the data carry the tuning the artists own.** Crumple zones in the vertex colours, pace notes on
  the centre line and horizon colours per hour put the look and feel in the hands of the people
  building the cars and stages, while the handling stays in code with the programmers.
- **One spatial grid for everything.** The same cells serve drawing, collision, lighting and scenery,
  so a stage is cheap to query wherever the car is.
- **A small state machine framework, data-driven transitions.** States as function pairs and
  transitions as a table keep the flow of a race, with its many players and modes, readable and easy to
  change.

## Further reading

- The status of the port and what it adds to the original: [Status](/guide/status).
- The formats: [`docs/formats`](https://github.com/emdzej/opencmr2/tree/main/docs/formats) (the BFL
  archives, the C3D models, the stages, the cars, the media).
- The reverse-engineering notes: [`docs/re`](https://github.com/emdzej/opencmr2/tree/main/docs/re)
  (architecture with the timing, the collisions, damage, cameras and sound; the world; the renderer;
  the front end; the AI; the challenge; the network game).
