/* The stage's tree shadows (see tre.h). */
#include "world/tre.h"
#include "render/d3d7.h"
#include "render/mesh.h"
#include "render/rstate.h"
#include "world/relight.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8_t *g_tre_mesh[SEC_MAX];
int16_t g_tre_node[SEC_MAX];
C3d g_tre;
int g_tre_nodes, g_tre_items, g_tre_matched;
bool g_tre_have;                  /* DAT_006dfd98 != 0 */
const uint8_t *g_tre_xhi;         /* DAT_006dfe10 */
static Scenery **item_scenery;    /* item +0x2c (a pointer in the original) */

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24); }
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8); }

void Tre_Free(void)
{
    for (int i = 0; i < SEC_MAX; i++) {
        free(g_tre_mesh[i]);
        g_tre_mesh[i] = NULL;
        g_tre_node[i] = -1;
    }
    free(item_scenery);
    item_scenery = NULL;
    Relight_Free();
    g_tre_have = false;
    g_tre_nodes = g_tre_items = g_tre_matched = 0;
}

void Tre_Load(uint8_t *data, uint32_t size, const uint8_t *xhi)
{
    Tre_Free();
    Relight_Alloc();
    if (data) Tre_Parse(data, size);
    if (xhi) g_tre_xhi = xhi;
}

static int iabs(int32_t v) { return v < 0 ? -v : v; }

/* The layout: three i32 and three 16.16 vectors (+0x00, +0x0c; the renderer's light set-up, not used
   yet), a u16 node count (+0x30), the 0x14-byte nodes, then for all nodes in turn their 0x30-byte items,
   their 0x14-byte blocks, the blocks' vertices (0x30), polygons (0x4c) and vertex colours (4). The
   original turns the arrays into pointers in place; the port writes file offsets, as C3d_Load keeps
   them, and checks they stay inside the file. */
bool Tre_Parse(uint8_t *d, uint32_t size)
{
    if (size < 0x32) return false;
    uint32_t n = rd16(d + 0x30), nodes = 0x32, o = nodes + n * 0x14;
    if (o > size) return false;
    uint32_t nitems = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t *nd = d + nodes + i * 0x14;
        wr32(nd + 0x0c, o + nitems * 0x30);
        nitems += rd16(nd + 2);
    }
    o += nitems * 0x30;
    uint32_t blocks = o;
    o += n * 0x14;
    if (o > size) return false;
    for (uint32_t i = 0; i < n; i++) wr32(d + nodes + i * 0x14 + 0x10, blocks + i * 0x14);
    for (int pass = 0; pass < 3; pass++)   /* vertices (+0x08), polygons (+0x0c), colours (+0x10) */
        for (uint32_t i = 0; i < n; i++) {
            uint8_t *b = d + blocks + i * 0x14;
            uint32_t cnt = pass == 1 ? rd32(b) : rd32(b + 4), rec = pass == 0 ? 0x30 : pass == 1 ? 0x4c : 4;
            wr32(b + 8 + 4 * pass, o);
            if (cnt > (size - o) / rec) return false;
            o += cnt * rec;
        }
    g_tre = (C3d){.data = d, .size = size};
    g_tre_nodes = (int)n;
    g_tre_items = (int)nitems;
    item_scenery = calloc(nitems ? nitems : 1, sizeof *item_scenery);
    if (!item_scenery) return false;

    /* each node to the first cell at exactly its x and z, 0xffff when none */
    for (uint32_t i = 0; i < n; i++) {
        uint8_t *nd = d + nodes + i * 0x14;
        int32_t kx = (int32_t)rd32(nd + 4), kz = (int32_t)rd32(nd + 8);
        uint16_t s = 0xffff;
        for (int k = 0; k < g_nsec; k++)
            if (g_sec[k].x == kx && g_sec[k].z == kz) { s = (uint16_t)k; break; }
        wr16(nd, s);
    }
    /* each item to the first scenery record (cells in order, then each cell's list) within 0x28f of
       its x and z; +0x20 to 1.0 */
    for (uint32_t i = 0, it = 0; i < n; i++) {
        uint8_t *nd = d + nodes + i * 0x14;
        for (uint32_t j = 0; j < rd16(nd + 2); j++, it++) {
            uint8_t *item = d + rd32(nd + 0x0c) + j * 0x30;
            int32_t ix = (int32_t)rd32(item), iz = (int32_t)rd32(item + 4);
            Scenery *hit = NULL;
            for (int k = 0; k < g_nsec && !hit; k++)
                for (Scenery *s = g_sec[k].scenery; s && !hit; s = s->next)
                    if (iabs(s->pos[0] - ix) < 0x28f && iabs(s->pos[2] - iz) < 0x28f) hit = s;
            if (hit) item_scenery[it] = hit, g_tre_matched++;
            wr32(item + 0x20, 0x10000);
        }
    }
    /* each shadow polygon's +0x04 (the node's item it belongs to) becomes the texture of that tree's
       first polygon. An unmatched item leaves the original reading through the file's stale value; the
       port leaves the polygon untextured (-1). */
    for (uint32_t i = 0, first = 0; i < n; i++) {
        uint8_t *nd = d + nodes + i * 0x14, *b = d + rd32(nd + 0x10);
        uint32_t ni = rd16(nd + 2);
        for (uint32_t p = 0; p < rd32(b); p++) {
            uint8_t *poly = d + rd32(b + 0x0c) + p * 0x4c;
            uint32_t idx = rd32(poly + 4), tex = 0xffffffffu;
            Scenery *s = idx < ni ? item_scenery[first + idx] : NULL;
            const uint8_t *tp = s && s->mesh ? C3d_Ref(s->model, s->mesh + 0x24, s->model->polys) : NULL;
            if (tp) tex = rd32(tp + 4);
            wr32(poly + 4, tex);
        }
        first += ni;
    }
    /* a shadow mesh for every node with a cell. Its flags are 0x402d plus the bits 0xffffe02d keeps of a
       stack value the original leaves there: the node loop's last offset, the node count times 0x14. */
    g_tre_have = true;
    uint32_t flags = ((n * 0x14) & 0xffffe02du) | 0x402du;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t *nd = d + nodes + i * 0x14, *b = d + rd32(nd + 0x10);
        uint16_t s = rd16(nd);
        if (s == 0xffff || s >= SEC_MAX) continue;
        uint8_t *me = calloc(1, C3D_MESH);   /* FUN_004ab8a0: a 0x120 record in the table at 0x67b124 */
        if (!me) continue;
        free(g_tre_mesh[s]);
        g_tre_mesh[s] = me;
        g_tre_node[s] = (int16_t)i;
        snprintf((char *)me, 12, "SHAD%u", i);
        memset(me + 0x108, 0xff, 3);
        wr32(me + 0x10, rd32(b + 4));
        wr32(me + 0x0c, rd32(b + 8));
        wr32(me + 0x28, rd32(b));
        wr32(me + 0x24, rd32(b + 0x0c));
        wr32(me + 0x34, rd32(b + 0x10));
        wr32(me + 0x30, flags);
        Mesh_Batches(&g_tre, me);
        Mesh_AddToVB(&g_tre, me);
    }
    return true;
}

/* The original first steps a time-driven value (FUN_004bcc60, from the centisecond clock) that nothing
   in the release reads; the port leaves it out. */
void Stage_DrawOverlay(void)
{
    D3DMATRIX id = {{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}};   /* 0x5207b8 */
    D3d_SetTransform(D3DTS_WORLD, &id);
    if (!g_tre_have) return;
    Render_ZWrite(0);
    for (int k = 0; k < g_nvisible; k++) {
        uint8_t *me = g_tre_mesh[g_visible[k]];
        if (me) Mesh_Draw(&g_tre, me, 1, 0, 1);
    }
    Render_ZWrite(1);
}
