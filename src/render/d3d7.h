/* The Direct3D 7 device the original renders through (docs/re/render.md), as C calls with Direct3D 7's
   values and structure layouts: IDirect3DDevice7 methods are D3d_<Method>, vertex buffers D3dVB_*. The
   state is recorded here (portable, testable without a GPU); a back end draws with it (gl_dev.c on
   OpenGL ES 3.0, adapted from OpenNFSPU's THRASH device). Without a back end, draws and clears are
   recorded only. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* D3DRENDERSTATETYPE (the ones CMR2 sets, and their neighbours the back end reads) */
enum {
    D3DRS_ANTIALIAS = 2, D3DRS_TEXTUREPERSPECTIVE = 4, D3DRS_ZENABLE = 7, D3DRS_FILLMODE = 8,
    D3DRS_SHADEMODE = 9, D3DRS_ZWRITEENABLE = 14, D3DRS_ALPHATESTENABLE = 15, D3DRS_SRCBLEND = 19,
    D3DRS_DESTBLEND = 20, D3DRS_CULLMODE = 22, D3DRS_ZFUNC = 23, D3DRS_ALPHAREF = 24, D3DRS_ALPHAFUNC = 25,
    D3DRS_DITHERENABLE = 26, D3DRS_ALPHABLENDENABLE = 27, D3DRS_FOGENABLE = 28, D3DRS_SPECULARENABLE = 29,
    D3DRS_STIPPLEDALPHA = 33, D3DRS_FOGCOLOR = 34, D3DRS_FOGTABLEMODE = 35, D3DRS_FOGSTART = 36,
    D3DRS_FOGEND = 37, D3DRS_FOGDENSITY = 38, D3DRS_EDGEANTIALIAS = 40, D3DRS_ZBIAS = 47,
    D3DRS_RANGEFOGENABLE = 48, D3DRS_STENCILENABLE = 52, D3DRS_STENCILFAIL = 53, D3DRS_STENCILZFAIL = 54,
    D3DRS_STENCILPASS = 55, D3DRS_STENCILFUNC = 56, D3DRS_STENCILREF = 57, D3DRS_STENCILMASK = 58,
    D3DRS_STENCILWRITEMASK = 59, D3DRS_TEXTUREFACTOR = 60, D3DRS_WRAP0 = 128, D3DRS_CLIPPING = 136,
    D3DRS_LIGHTING = 137, D3DRS_EXTENTS = 138, D3DRS_AMBIENT = 139, D3DRS_FOGVERTEXMODE = 140,
    D3DRS_COLORVERTEX = 141, D3DRS_LOCALVIEWER = 142, D3DRS_NORMALIZENORMALS = 143,
    D3DRS_COLORKEYBLENDENABLE = 144, D3DRS_DIFFUSEMATERIALSOURCE = 145, D3DRS_SPECULARMATERIALSOURCE = 146,
    D3DRS_AMBIENTMATERIALSOURCE = 147, D3DRS_EMISSIVEMATERIALSOURCE = 148, D3DRS_VERTEXBLEND = 151,
    D3DRS_CLIPPLANEENABLE = 152,
    D3DRS_COUNT = 160,
};

/* D3DTEXTURESTAGESTATETYPE */
enum {
    D3DTSS_COLOROP = 1, D3DTSS_COLORARG1 = 2, D3DTSS_COLORARG2 = 3, D3DTSS_ALPHAOP = 4,
    D3DTSS_ALPHAARG1 = 5, D3DTSS_ALPHAARG2 = 6, D3DTSS_BUMPENVMAT00 = 7, D3DTSS_BUMPENVMAT01 = 8,
    D3DTSS_BUMPENVMAT10 = 9, D3DTSS_BUMPENVMAT11 = 10, D3DTSS_TEXCOORDINDEX = 11, D3DTSS_ADDRESS = 12,
    D3DTSS_ADDRESSU = 13, D3DTSS_ADDRESSV = 14, D3DTSS_BORDERCOLOR = 15, D3DTSS_MAGFILTER = 16,
    D3DTSS_MINFILTER = 17, D3DTSS_MIPFILTER = 18, D3DTSS_MIPMAPLODBIAS = 19, D3DTSS_MAXMIPLEVEL = 20,
    D3DTSS_MAXANISOTROPY = 21, D3DTSS_BUMPENVLSCALE = 22, D3DTSS_BUMPENVLOFFSET = 23,
    D3DTSS_TEXTURETRANSFORMFLAGS = 24,
    D3DTSS_COUNT = 25,
};

/* D3DTOP values the combiner knows (1 DISABLE .. 24 DOTPRODUCT3) and D3DTA arguments */
enum { D3DTOP_DISABLE = 1, D3DTOP_SELECTARG1, D3DTOP_SELECTARG2, D3DTOP_MODULATE, D3DTOP_BUMPENVMAP = 22,
       D3DTOP_BUMPENVMAPLUMINANCE = 23 };
enum { D3DTA_DIFFUSE = 0, D3DTA_CURRENT = 1, D3DTA_TEXTURE = 2, D3DTA_TFACTOR = 3, D3DTA_SPECULAR = 4,
       D3DTA_COMPLEMENT = 0x10, D3DTA_ALPHAREPLICATE = 0x20 };
/* D3DTSS_TEXCOORDINDEX high word */
enum { D3DTSS_TCI_CAMERASPACENORMAL = 0x10000, D3DTSS_TCI_CAMERASPACEPOSITION = 0x20000,
       D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR = 0x30000 };

enum { D3D_STAGES = 8, D3D_LIGHTS = 8 };

/* D3DPRIMITIVETYPE */
enum { D3DPT_POINTLIST = 1, D3DPT_LINELIST, D3DPT_LINESTRIP, D3DPT_TRIANGLELIST, D3DPT_TRIANGLESTRIP,
       D3DPT_TRIANGLEFAN };
/* D3DFVF */
enum {
    D3DFVF_XYZ = 0x002, D3DFVF_XYZRHW = 0x004, D3DFVF_NORMAL = 0x010, D3DFVF_DIFFUSE = 0x040,
    D3DFVF_SPECULAR = 0x080, D3DFVF_TEX1 = 0x100, D3DFVF_TEX2 = 0x200,
    /* the two the game draws with: models (docs/formats/c3d.md#vertex-0x30-bytes) and 2D */
    FVF_MODEL = 0x2d2, FVF_TL = 0x1c4,
};
/* D3DCLEAR */
enum { D3DCLEAR_TARGET = 1, D3DCLEAR_ZBUFFER = 2, D3DCLEAR_STENCIL = 4 };
/* D3DTRANSFORMSTATETYPE */
enum { D3DTS_WORLD = 1, D3DTS_VIEW = 2, D3DTS_PROJECTION = 3, D3DTS_TEXTURE0 = 16, D3DTS_TEXTURE7 = 23 };
/* D3DLIGHTTYPE */
enum { D3DLIGHT_POINT = 1, D3DLIGHT_SPOT = 2, D3DLIGHT_DIRECTIONAL = 3 };

typedef struct { float m[4][4]; } D3DMATRIX;            /* row vectors: v' = v M */
typedef struct { float r, g, b, a; } D3DCOLORVALUE;
typedef struct { float x, y, z; } D3DVECTOR;

typedef struct {                                         /* D3DLIGHT7, 0x68 bytes */
    uint32_t type;
    D3DCOLORVALUE diffuse, specular, ambient;
    D3DVECTOR position, direction;
    float range, falloff, attenuation0, attenuation1, attenuation2, theta, phi;
} D3DLIGHT7;

typedef struct {                                         /* D3DMATERIAL7, 0x44 bytes */
    D3DCOLORVALUE diffuse, ambient, specular, emissive;
    float power;
} D3DMATERIAL7;

typedef struct {                                         /* D3DVIEWPORT7 */
    uint32_t x, y, width, height;
    float minz, maxz;
} D3DVIEWPORT7;

typedef struct D3dTexture D3dTexture;                    /* a texture surface: RGBA8, rows top first */

/* A vertex buffer (IDirect3DVertexBuffer7): fvf, count, the vertices in that layout. */
typedef struct {
    uint32_t fvf, stride, count;
    uint8_t *data;
    unsigned gl;            /* the back end's buffer */
    bool dirty;
} D3dVB;

/* The device's state, as the back end reads it. */
typedef struct {
    uint32_t rs[D3DRS_COUNT];
    uint32_t tss[D3D_STAGES][D3DTSS_COUNT];
    D3dTexture *tex[D3D_STAGES];
    D3DMATRIX world, view, proj, texm[D3D_STAGES];
    D3DLIGHT7 light[D3D_LIGHTS];
    bool light_on[D3D_LIGHTS];
    D3DMATERIAL7 material;
    D3DVIEWPORT7 viewport;
    int width, height, bpp;  /* the mode */
    bool in_scene;
    D3dTexture *target;      /* the render target: NULL the back buffer, else a cube's face */
    int target_face;         /* 0-5: +x, -x, +y, -y, +z, -z */
} D3dState;

/* The back end (gl_dev.c registers itself with D3d_SetBackend). */
typedef struct {
    bool (*open)(int width, int height, int bpp);
    void (*draw)(const D3dState *s, int prim, uint32_t fvf, const void *verts, uint32_t nverts,
                 const uint16_t *indices, uint32_t nindices, D3dVB *vb, uint32_t first);
    void (*clear)(const D3dState *s, int x0, int y0, int x1, int y1, uint32_t flags, uint32_t colour,
                  float z, uint32_t stencil);
    void (*present)(void);
    void (*texture_upload)(D3dTexture *t);
    void (*texture_free)(D3dTexture *t);
    void (*vb_free)(D3dVB *vb);
    /* the back buffer's pixels in x, y, w, h of the mode (rows from the top), RGBA */
    void (*read)(int x, int y, int w, int h, uint8_t *rgba);
} D3dBackend;

struct D3dTexture {
    int width, height;
    uint8_t *rgba;          /* width * height * 4 (NULL for a cube) */
    unsigned gl;
    bool dirty;
    bool cube;              /* a cube of six width x width faces that can be drawn into */
    unsigned fbo[6], depth; /* the back end's targets for the faces */
};

void D3d_SetBackend(const D3dBackend *b);
const D3dState *D3d_State(void);
uint32_t D3d_FvfStride(uint32_t fvf);

/* The device on a mode (DirectDraw's SetDisplayMode and CreateDevice reduced to this): defaults set. */
bool D3d_Open(int width, int height, int bpp);
/* The display mode's size (PTR_DAT_00520b74 +0x00, +0x04). */
void D3d_ScreenSize(int *width, int *height);
/* The back buffer read back (the original blits it to an offscreen surface and locks that): RGBA rows
   from the top; zeros without a back end. */
void D3d_ReadBack(int x, int y, int w, int h, uint8_t *rgba);
void D3d_Close(void);

/* IDirect3DDevice7 */
void D3d_BeginScene(void);                                          /* 0x14 */
void D3d_EndScene(void);                                            /* 0x18 */
void D3d_Clear(uint32_t nrects, const int32_t *rects, uint32_t flags, uint32_t colour, float z,
               uint32_t stencil);                                   /* 0x28; rects: x1, y1, x2, y2 */
void D3d_SetTransform(uint32_t type, const D3DMATRIX *m);           /* 0x2c */
void D3d_GetTransform(uint32_t type, D3DMATRIX *m);                 /* 0x30 */
void D3d_SetViewport(const D3DVIEWPORT7 *v);                        /* 0x34 */
void D3d_SetMaterial(const D3DMATERIAL7 *m);                        /* 0x40 */
void D3d_SetLight(uint32_t i, const D3DLIGHT7 *l);                  /* 0x48 */
void D3d_SetRenderState(uint32_t rs, uint32_t value);               /* 0x50 */
uint32_t D3d_GetRenderState(uint32_t rs);                           /* 0x54 */
void D3d_DrawPrimitive(uint32_t prim, uint32_t fvf, const void *verts, uint32_t n, uint32_t flags);   /* 0x64 */
void D3d_DrawIndexedPrimitive(uint32_t prim, uint32_t fvf, const void *verts, uint32_t nverts,
                              const uint16_t *indices, uint32_t nindices, uint32_t flags);            /* 0x68 */
void D3d_DrawPrimitiveVB(uint32_t prim, D3dVB *vb, uint32_t first, uint32_t n, uint32_t flags);       /* 0x7c */
void D3d_DrawIndexedPrimitiveVB(uint32_t prim, D3dVB *vb, uint32_t first, uint32_t nverts,
                                const uint16_t *indices, uint32_t nindices, uint32_t flags);          /* 0x80 */
void D3d_SetTexture(uint32_t stage, D3dTexture *t);                 /* 0x8c */
void D3d_SetTextureStageState(uint32_t stage, uint32_t tss, uint32_t value);   /* 0x94 */
void D3d_LightEnable(uint32_t i, bool on);                          /* 0xb0 */

/* IDirect3DVertexBuffer7 (created by IDirect3D7::CreateVertexBuffer) */
D3dVB *D3dVB_Create(uint32_t fvf, uint32_t count);
void *D3dVB_Lock(D3dVB *vb);                                         /* 0x0c */
void D3dVB_Unlock(D3dVB *vb);                                        /* 0x10 */
void D3dVB_Free(D3dVB *vb);

/* Textures: the decoders' RGBA8 (image.h) taken over. */
D3dTexture *D3dTex_Create(int width, int height, uint8_t *rgba);
/* a cube map whose faces are render targets with their own depth buffer (DDSCAPS2_CUBEMAP, all faces) */
D3dTexture *D3dTex_CreateCube(int size);
void D3d_SetRenderTarget(D3dTexture *cube, int face);   /* 0x20 (NULL: the back buffer) */
void D3dTex_Free(D3dTexture *t);

void D3d_Present(void);   /* the flip */

/* Counters for tests: draw calls and primitives since D3d_Open. */
typedef struct { uint32_t draws, prims, clears, scenes; } D3dStats;
D3dStats D3d_Stats(void);
