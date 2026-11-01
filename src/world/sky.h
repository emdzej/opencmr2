/* The sky's meshes (0x4919a0-0x492fe0, docs/re/world.md#the-sky): the dome's, ground's, clouds' and
   the sky's three parts' vertex colours, the dome cut at the horizon, the sun's point, the turns and
   the fog factor the meshes take. The sky model (TEMP.SKY) is the dome with a child A (the sun's disc),
   A's next B (a sheet lit by the lightning) and B's next C (the sun's halo). */
#pragma once
#include <stdint.h>

/* 0x4919a0 (first in 0x460da0, the stage's horizon set): the objects and meshes, the dome's heights
   (the sun is its vertex 104), the ground's vertex 50 and its horizon height, B's colours cleared */
void Sky_Setup(void);
/* 0x4925c0: the stage's offsets for the sky, ground and clouds (16.16 heights); the dome's vertices
   below the horizon height (less the sky's, plus the ground's offset) brought up to it, all but the sun;
   the sun light placed at the sun */
void Sky_Cut(int32_t sky, int32_t ground, int32_t cloud);
void Sky_Offsets(int32_t *sky, int32_t *ground, int32_t *cloud);   /* Stage_SkyOffsets 0x4928c0 */
void Sky_SetScale(int32_t v);   /* 0x492900: the dome's row 1 (0x592134, set to 1.0 with the horizon) */
int32_t Sky_Scale(void);        /* 0x4928f0 */
/* 0x492890: the sun's vertex through the dome's local 3 x 3 */
void Sky_SunPoint(int32_t out[3]);
/* 0x491d40: the dome's colours from horizon (its lowest vertex) to zenith (its highest) by height, the
   sun's vertex its height's colour moved toward sun by mix; R, G, B bytes */
void Sky_ColourDome(const uint8_t horizon[3], const uint8_t zenith[3], const uint8_t sun[3], int32_t mix);
/* 0x4920d0: the ground's colours, its vertex 50 under the sun the second; 0x492220: the clouds' (the
   bolt's vertex the second) and the cloud top's; 0x4923d0 B's, 0x492470 A's, 0x492520 C's. R, G, B, A
   bytes. */
void Sky_ColourGround(const uint8_t c[4], const uint8_t under_sun[4]);
void Sky_ColourClouds(const uint8_t c[4], const uint8_t bolt[4]);
void Sky_ColourFlash(const uint8_t c[4]);
void Sky_ColourDisc(const uint8_t c[4]);
void Sky_ColourHalo(const uint8_t c[4]);
/* 0x4926f0: the dome turned by sky degrees (16.16; -999 none), the clouds by cloud degrees */
void Sky_Turn(int32_t sky, int32_t cloud);
/* 0x492bd0: A and C facing the view's camera (its rows, scaled by 0.046) */
struct Obj;
void Sky_FaceCamera(const struct Obj *camera);
/* 0x492fe0: the fog (Fog_Set) and the sky's fog factor, the fog at the draw distance */
void Sky_Fog(const uint8_t rgb[3], int32_t near, int32_t far);
void Sky_FogOn(uint32_t on);    /* 0x492fd0 */
/* 0x492f10: the changed meshes uploaded */
void Sky_Upload(void);
/* 0x492910: the clouds' vertex nearest the sun within 100 (in the clouds' frame), -1 none; kept as
   the bolt's vertex for the clouds' colours */
int Sky_BoltVertex(void);
/* 0x492b50: one of three bolt textures on B's polygons (none are ever loaded: only its rand()) */
void Sky_BoltTexture(void);
