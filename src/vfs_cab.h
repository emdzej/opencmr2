/* InstallShield cabinet layer of the file layer. The CMR2 disc keeps the game program (CMR2.exe), the demo
   runs and a few small files only inside the installer's cabinet (data1.hdr, data1.cab, data2.cab), so the
   port reads them from there and the user needs no install step. Layout: docs/formats/index.md, the
   reference reader tools/iscab.py.

   vfs_cab_layer puts the cabinet's files on top of the current mount, at their installed paths
   ("CMR2.exe", "Game/DemoRuns/Fra01H.rpl"): a path is looked up on the medium first, then in the cabinet.
   Compressed files are decoded whole when opened (the largest is 2.4 MB). */
#pragma once
#include <stdbool.h>

/* If the mounted medium has data1.hdr and no CMR2.exe (a CD or its image, not an installed game), layers
   the cabinet over it. False only if a cabinet is there but can't be read; true when it was layered or not
   needed. */
bool vfs_cab_layer(void);
