# World

The scene: objects in trees, brought up to date and drawn each frame, placed in the stage's sections.
See [c3d.md](../formats/c3d.md#object-0x18c-bytes) for the record and [render.md](render.md) for the
drawing below it.

## Objects

A model's object records are loaded with the model. `C3d_Fixup` registers them in the object table at
0x67f36c and writes each one's slot number into it. With a parent object given, the model's roots
(`+0x24` = -1) are appended under that parent (`Obj_Attach` 0x4ac200): as its first child, or after the
last sibling of its first child. A root keeps its own `next` link, so a model with several linked roots
would bring its siblings along twice. Every object's `+0x24` is then -1.

The four matrices:

| Offset | Matrix |
|---|---|
| `+0x58` | rest pose |
| `+0x98` | current local (object to parent) |
| `+0xd8` | world (16.16) |
| `+0x118` | world in floats, set as the world transform when drawing |

`+0x174` is a dirty flag; `+0x178` is the kind (0 mesh, 1 light, 2, 3 node).

`Obj_UpdateTree` 0x4ac280 walks a sibling list and its children. It skips inactive objects (0x4ac390;
objects in a section ask the section) along with what hangs under them. An object with a parent and a
kind of 0-3 is recomputed when it or its parent is dirty:
- under a parent of type `0xfd` (byte `+0x30`), world = local;
- with `+0x184` set, world = the parent's world;
- otherwise world = local x parent's world (`Mat_Mul` 0x4b9f20).

A placed object (`+0x24` not -1) then finds its section again (`Obj_UpdateSection` 0x4b8690), and the
object and all its ancestors are marked dirty. A root (no parent) is never recomputed: whoever owns it
sets its world matrix.

`Obj_Commit` 0x4ad8d0 then handles each dirty object by kind:
- kinds 0 and 3: the float matrix is made;
- kind 1: `+0x0c` points to a `D3DLIGHT7` (index at `+0x68`). A point or spot light gets the world
  position, a spot also the world x axis as its direction, and is set; a directional light is left as
  it is.

The flag is then cleared.

`Obj_Draw` 0x49cad0 draws one object for a pass (0-7, a bit in `+0x17c`), when its kind is 0
(`+0x178`) and it has a mesh, then its children through `Obj_DrawList` 0x49ca50. `Obj_DrawList` only
descends under kind-0 objects, so a car is drawn by calling `Obj_Draw` on its kind-3 root.

`Obj_Frame` 0x49ce10 runs the update, the commit and `View_Set` 0x4ade00 for a camera object.
`View_Set` makes the view the 16.16 inverse of the camera's world (`Mat_Inverse` 0x4ba440: cofactors
over `2^32 / det`), kept at `PTR_DAT_00520b78 + 0x2424`, when the camera is dirty and a counter at
0x6838d0 is below 5.

## Sections

The stage mesh's 0x88-byte section records are registered at 0x71f608 (count at 0x72d568). They form
a grid whose row length is at 0x71f600, with a half size at 0x72d248. Each section keeps a list of the
objects in it (head `+0x1c`, count `+0x20`, linked through the objects' `+0x170`).

`Obj_UpdateSection` 0x4b8690 starts from the object's section and, when the world position (`+0x108`,
the world matrix's translation) has left it, finds another (0x4b85f0). It records up to three
neighbouring sections within 4.5 units of an edge at `+0x28` and moves the object between the
sections' lists. The section draw (0x49ce40) walks a list of visible sections (0x6ed5f0, count
0x72d570) and draws their objects with the z-buffer off.

## Loading a stage

`Stage_Load` 0x41f930, in order:
1. the country's texture archive (0x4b23c0) and the loading bar (0x40fec0);
2. the horizons, the BSP and `TEMP.C3D` (`Stage_LoadC3d` 0x41fc50);
3. `.hpc`, `.srf`, `fin.dat`, 0x471af0, `.csp`, `.cat` and the co-driver's `.cod`;
4. the `.tsc` splines (0x48caa0) and the sections (0x4b8270);
5. `.tre` and `.xhi`.

Each model goes through `C3d_Load` with two arguments:
- the parent object at `0x536be0`, under which the model's roots hang;
- the texture archive slot at `0x538858`, used only when it is loaded.

`TEMP.OBJ` (the scenery, `Stage_LoadObj` 0x472830), `TEMP.SHT` (0x472870) and the sky and ground
(`Stage_LoadSkyGround` 0x46f060) load the same way from elsewhere.

## The port

`src/world/obj.c` copies each record into an `Obj` with real pointers, since links cross models. The
record stays reachable for the fields not yet understood. The update, commit, draw and view are
ported; the section test and `Obj_UpdateSection` come with the stage. The car viewer hangs the car
under a turning stand and takes the view from a camera object, so a car on screen goes through these
functions. `tests/d3d7_test.c` checks the 205's tree: world = local plus the stand's move, the float
matrix, the inverse view, and all 672 triangles drawn through `Obj_Draw`.
