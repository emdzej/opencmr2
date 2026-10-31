/* PP_F models (see c3d.h). */
#include "c3d.h"
#include <string.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24); }

/* A relocated field: -1, or an offset inside [base, end] (the end itself for an empty range). */
static bool field_ok(const C3d *m, uint32_t at, uint32_t base, uint32_t end)
{
    if ((uint64_t)at + 4 > m->size) return false;
    uint32_t v = rd32(m->data + at);
    return v == 0xffffffffu || v <= end - base;
}

/* A field with a record count: offset + count * record size inside the array. */
static bool range_ok(const C3d *m, uint32_t at, uint32_t count, uint32_t rec, uint32_t base, uint32_t end)
{
    uint32_t v = rd32(m->data + at);
    if (v == 0xffffffffu) return true;
    return v <= end - base && (uint64_t)count * rec <= end - base - v;
}

bool C3d_Load(C3d *m, uint8_t *d, size_t size, int32_t tex_base)
{
    memset(m, 0, sizeof *m);
    if (size < 0x30 || memcmp(d, "PP_F", 4)) return false;
    m->data = d;
    m->size = size;
    m->kind = rd32(d + 4);
    m->slots = rd32(d + 8);
    m->paths = rd32(d + 0x0c);
    m->n10 = rd32(d + 0x10);
    m->nvert = rd32(d + 0x14);
    m->nobj = rd16(d + 0x18);
    m->nmesh1 = rd16(d + 0x1a);
    m->nmesh2 = rd16(d + 0x1c);
    m->nsec = rd16(d + 0x1e);
    m->npoly = rd32(d + 0x20);
    m->ntex = rd16(d + 0x24);
    m->n26 = rd16(d + 0x26);
    m->n28 = rd16(d + 0x28);
    /* the array layout of C3d_Fixup; 64-bit sums so that a bad count cannot wrap */
    uint64_t o = 0x30, nm = (uint64_t)m->nmesh1 + m->nmesh2;
    uint64_t at[11];
    const uint64_t len[10] = {
        (uint64_t)m->nobj * C3D_OBJECT, nm * C3D_MESH, (uint64_t)m->nmesh2 * C3D_MESH2X,
        (uint64_t)m->nsec * C3D_SECTION, (uint64_t)m->n28 * C3D_R1C, (uint64_t)m->npoly * C3D_POLY,
        (uint64_t)m->nvert * C3D_VERTEX, (uint64_t)m->nvert * C3D_VX, (uint64_t)m->n10 * C3D_R14,
        (uint64_t)m->n26 * C3D_R5C,
    };
    for (int i = 0; i < 10; i++) {
        at[i] = o;
        o += len[i];
    }
    at[10] = o;
    if (o != m->slots || o > size || (uint64_t)m->slots + (uint64_t)m->ntex * C3D_SLOT > size ||
        (uint64_t)m->paths + (uint64_t)m->ntex * C3D_PATH > size)
        return false;
    m->objects = (uint32_t)at[0];
    m->meshes = (uint32_t)at[1];
    m->mesh2x = (uint32_t)at[2];
    m->sections = (uint32_t)at[3];
    m->r1c = (uint32_t)at[4];
    m->polys = (uint32_t)at[5];
    m->verts = (uint32_t)at[6];
    m->vx = (uint32_t)at[7];
    m->r14 = (uint32_t)at[8];
    m->r5c = (uint32_t)at[9];
    uint32_t meshes2 = m->meshes + (uint32_t)m->nmesh1 * C3D_MESH;

    for (uint32_t i = 0; i < m->npoly; i++) {   /* texture indices at +0x04..+0x2b */
        uint8_t *p = d + m->polys + (size_t)i * C3D_POLY + 4;
        for (int k = 0; k < 10; k++, p += 4) {
            uint32_t t = rd32(p);
            if (t != 0xffffffffu) wr32(p, t + (uint32_t)tex_base);
        }
    }
    for (uint32_t i = 0; i < m->nobj; i++) {   /* sibling, child, parent, +0x170 into the objects; mesh */
        uint32_t b = m->objects + i * C3D_OBJECT;
        if (!field_ok(m, b, m->objects, m->meshes) || !field_ok(m, b + 4, m->objects, m->meshes) ||
            !field_ok(m, b + 8, m->objects, m->meshes) || !field_ok(m, b + 0x170, m->objects, m->meshes) ||
            !field_ok(m, b + 0x0c, m->meshes, m->mesh2x))
            return false;
    }
    for (uint32_t i = 0; i < m->n26; i++)      /* +0x58: next 0x5c record */
        if (!field_ok(m, m->r5c + i * C3D_R5C + 0x58, m->r5c, m->slots)) return false;
    for (uint32_t i = 0; i < nm; i++) {
        uint32_t b = m->meshes + i * C3D_MESH;
        /* parts of 0x108 bytes from +0x0c, their count in the byte at +0x113 (1 in every file seen): the
           original walks them at that stride even past the record */
        for (uint32_t k = 0; k < d[b + 0x113]; k++) {
            uint32_t s = b + 0x0c + k * 0x108;
            if ((uint64_t)s + 0x2c > size) return false;
            if (!range_ok(m, s, rd32(d + s + 4), C3D_VERTEX, m->verts, m->vx) ||
                !range_ok(m, s + 0x18, rd32(d + s + 0x1c), C3D_POLY, m->polys, m->verts) ||
                !range_ok(m, s + 0x28, rd32(d + s + 4), C3D_VX, m->vx, m->r14) ||
                !field_ok(m, s + 0x14, m->r14, m->r5c))
                return false;
        }
        if (!field_ok(m, b + 0x10c, m->r5c, m->slots)) return false;
    }
    for (uint32_t i = 0; i < m->nmesh2; i++) {
        uint32_t b = m->mesh2x + i * C3D_MESH2X;
        if (!field_ok(m, b + 0x0c, meshes2, m->mesh2x) || !field_ok(m, b + 0x98, m->mesh2x, m->sections))
            return false;
    }
    for (uint32_t i = 0; i < m->nsec; i++)
        if (!field_ok(m, m->sections + i * C3D_SECTION + 0x10, m->meshes, m->mesh2x)) return false;
    return true;
}

const uint8_t *C3d_Ref(const C3d *m, const uint8_t *field, uint32_t base)
{
    uint32_t v = rd32(field);
    return v == 0xffffffffu ? NULL : m->data + base + v;
}

const uint8_t *C3d_Object(const C3d *m, int i) { return m->data + m->objects + (size_t)i * C3D_OBJECT; }
const uint8_t *C3d_Mesh(const C3d *m, int i) { return m->data + m->meshes + (size_t)i * C3D_MESH; }
const uint8_t *C3d_Poly(const C3d *m, int i) { return m->data + m->polys + (size_t)i * C3D_POLY; }
const uint8_t *C3d_Vertex(const C3d *m, int i) { return m->data + m->verts + (size_t)i * C3D_VERTEX; }

const char *C3d_TexPath(const C3d *m, int i)
{
    uint8_t *p = m->data + m->paths + (size_t)i * C3D_PATH;
    p[C3D_PATH - 1] = 0;
    return (const char *)p;
}
