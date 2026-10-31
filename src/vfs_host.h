/* Host-filesystem backends of the file layer (POSIX): directories and disc-image files. Used by the
   tests and tools; a backend without a filesystem (gasm) mounts a VfsSource instead. */
#pragma once
#include "vfs.h"

/* Mounts a data root: a directory (the installed game folder), a disc image of it (.iso, raw .bin), or a
   .cue sheet (its FILE line names the image, its TRACK line the sector format). False if it can't be
   mounted; whether it holds the game is up to the caller (vfs_exists("CMR2.exe")). */
bool vfs_mount_path(const char *path);
/* $OPENCMR2_DATA if set, else ./cd (the CD's files; tests and tools run from the project root): the CD
   folder, CMR2.ISO, or an installed game folder. A CD gets its installer cabinet layered on top
   (vfs_cab_layer), so CMR2.exe is found either way. */
bool vfs_mount_default(void);
