/* The Direct3D 7 device on OpenGL ES 3.0 (gasm:gl, WebGL 2 rules): the back end of d3d7.c. Adapted from
   OpenNFSPU's THRASH device (../opennfspu/src/render/thrash_dev.c: the flip chain, the two-stage
   combiner, fog and alpha test, the pre-transformed path), with Direct3D 7's transform and lighting
   added for the model vertices. Written from Direct3D 7's documented behaviour.

   Conventions (as OpenNFSPU's): the render target keeps Direct3D's rows (top first) and is flipped at
   present; pixel centres are on integer coordinates (+0.5); a transformed-and-lit vertex goes through
   Direct3D's viewport mapping to the same screen coordinates as a pre-transformed one, in a form linear
   in clip space so that GL's clipping stays exact. The mode is drawn at its own size, as the original's
   display, and scaled to the drawable (the mode chosen is the player's trade of detail for speed). */
#include "prof.h"
#include "render/d3d7.h"
#include "platform.h"
#include <GLES3/gl3.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { STAGES = 2 };   /* the stages the combiner evaluates (CMR2 sets up two) */

static int mw, mh, mbpp, scale;
static GLuint fbo[2], colour[2], depth;   /* [0] front, [1] back (the render target) */
static GLuint prog, present_prog, vao, vbo, ibo, white_tex, white_cube, samplers[STAGES], cube_sampler;
static float gamma_scale = 1.0f;

static struct {
    GLint mode, tl, stage_op, stage_arg, tfactor, alphatest, alpharef, alphafunc, fog, fogmode, fogcolour,
        fogparams, specular, lodbias, tci, texflags, texm0, texm1, tex0, tex1, pointsize, wv, nrm, proj, vp, vpz,
        lighting, colorvertex, has_colour, src, ambient, mat, power, localviewer, normalize, vfog, rangefog,
        nlights, ltype, ldiff, lspec, lamb, lpos, ldir, lp1, lp2, texc, cube0, bumpmat, bumplum;
} u;
static struct { GLint tex, gamma, rect; } up;

/* ---------------------------------------------------------------- the GL state sent

   The draws set the whole Direct3D state again each time; only what changed is sent (in the browser
   every GL call crosses into JavaScript and WebGL's checks: about 60 a draw, most of them the same as
   the last). The paths that change GL state outside these (the open, the present, texture uploads,
   cube faces, read-backs) forget it with shadow_reset(). */

enum { UCACHE = 128, UBYTES = 256 };
static struct {
    bool known;
    uint32_t caps;                      /* bit per cap(): its last state */
    uint32_t caps_known;
    GLenum depth_func, front, cull, blend[2];
    GLboolean depth_mask;
    GLint stencil[7];                   /* func, ref, mask, fail, zfail, pass, write mask */
    GLuint program, fb, vbuf, ibuf, vao;
    GLint viewport[4], scissor[4];
    GLenum active;
    GLuint tex2d[3], cube[3], sampler[3];
    GLint sparam[STAGES][4];
    uint8_t attr_on;                    /* bit per attribute: enabled */
    struct { GLuint buf; GLint size; GLenum type; GLboolean norm; GLsizei stride; uintptr_t off; } attr[6];
    float attr_const[6][4];
    uint8_t attr_const_known;
} sh;
static struct { GLint loc; uint16_t len; uint8_t v[UBYTES]; } ucache[UCACHE];

static void shadow_reset(void)
{
    memset(&sh, 0, sizeof sh);
}

static void uniforms_reset(void)
{
    for (int i = 0; i < UCACHE; i++) ucache[i].loc = -1, ucache[i].len = 0;
}

/* true when uniform loc of the main program is to be sent (its n bytes not the last ones sent) */
static bool ufresh(GLint loc, const void *v, size_t n)
{
    if (loc < 0) return false;
    if (n > UBYTES) return true;
    unsigned h = (unsigned)loc * 2654435761u >> 25;   /* 7 bits */
    for (int k = 0; k < UCACHE; k++) {
        unsigned i = (h + (unsigned)k) & (UCACHE - 1);
        if (ucache[i].loc == loc || ucache[i].loc == -1) {
            if (ucache[i].loc == loc && ucache[i].len == n && !memcmp(ucache[i].v, v, n)) return false;
            ucache[i].loc = loc, ucache[i].len = (uint16_t)n;
            memcpy(ucache[i].v, v, n);
            return true;
        }
    }
    return true;
}

static void u1i(GLint l, GLint a) { if (ufresh(l, &a, sizeof a)) glUniform1i(l, a); }
static void u1f(GLint l, GLfloat a) { if (ufresh(l, &a, sizeof a)) glUniform1f(l, a); }
static void u2f(GLint l, GLfloat a, GLfloat b) { GLfloat v[2] = {a, b}; if (ufresh(l, v, sizeof v)) glUniform2f(l, a, b); }
static void u3f(GLint l, GLfloat a, GLfloat b, GLfloat c) { GLfloat v[3] = {a, b, c}; if (ufresh(l, v, sizeof v)) glUniform3f(l, a, b, c); }
static void u4f(GLint l, GLfloat a, GLfloat b, GLfloat c, GLfloat d) { GLfloat v[4] = {a, b, c, d}; if (ufresh(l, v, sizeof v)) glUniform4f(l, a, b, c, d); }
static void u2i(GLint l, GLint a, GLint b) { GLint v[2] = {a, b}; if (ufresh(l, v, sizeof v)) glUniform2i(l, a, b); }
static void u4i(GLint l, GLint a, GLint b, GLint c, GLint d) { GLint v[4] = {a, b, c, d}; if (ufresh(l, v, sizeof v)) glUniform4i(l, a, b, c, d); }
static void u1iv(GLint l, GLsizei n, const GLint *v) { if (ufresh(l, v, (size_t)n * 4)) glUniform1iv(l, n, v); }
static void u2iv(GLint l, GLsizei n, const GLint *v) { if (ufresh(l, v, (size_t)n * 8)) glUniform2iv(l, n, v); }
static void u4iv(GLint l, GLsizei n, const GLint *v) { if (ufresh(l, v, (size_t)n * 16)) glUniform4iv(l, n, v); }
static void u1fv(GLint l, GLsizei n, const GLfloat *v) { if (ufresh(l, v, (size_t)n * 4)) glUniform1fv(l, n, v); }
static void u3fv(GLint l, GLsizei n, const GLfloat *v) { if (ufresh(l, v, (size_t)n * 12)) glUniform3fv(l, n, v); }
static void u4fv(GLint l, GLsizei n, const GLfloat *v) { if (ufresh(l, v, (size_t)n * 16)) glUniform4fv(l, n, v); }
static void um3(GLint l, const GLfloat *v) { if (ufresh(l, v, 36)) glUniformMatrix3fv(l, 1, GL_FALSE, v); }
static void um4(GLint l, const GLfloat *v) { if (ufresh(l, v, 64)) glUniformMatrix4fv(l, 1, GL_FALSE, v); }

static void cap(GLenum c, bool on)
{
    static const GLenum caps[] = {GL_DEPTH_TEST, GL_BLEND, GL_CULL_FACE, GL_STENCIL_TEST, GL_DITHER, GL_SCISSOR_TEST};
    int i = 0;
    while (caps[i] != c) i++;
    uint32_t b = 1u << i;
    if (sh.caps_known & b && !(sh.caps & b) == !on) return;
    sh.caps_known |= b;
    if (on) sh.caps |= b, glEnable(c);
    else sh.caps &= ~b, glDisable(c);
}

static void use_program(GLuint p) { if (!sh.known || sh.program != p) glUseProgram(p); sh.program = p, sh.known = true; }
static void depth_mask(GLboolean m) { if (sh.depth_mask != m + 1) glDepthMask(m), sh.depth_mask = (GLboolean)(m + 1); }
static void depth_func(GLenum f) { if (sh.depth_func != f) glDepthFunc(f), sh.depth_func = f; }
static void blend_func(GLenum s, GLenum d) { if (sh.blend[0] != s + 1 || sh.blend[1] != d + 1) glBlendFunc(s, d), sh.blend[0] = s + 1, sh.blend[1] = d + 1; }
static void front_face(GLenum f) { if (sh.front != f) glFrontFace(f), sh.front = f; }
static void cull_face(GLenum f) { if (sh.cull != f) glCullFace(f), sh.cull = f; }

/* the stencil's function, reference, masks and operations (+1 so that a zero is not known) */
static void stencil(GLenum func, GLint ref, GLuint mask, GLenum fail, GLenum zfail, GLenum pass, GLuint wmask)
{
    GLint *s = sh.stencil;
    if (s[0] != (GLint)func + 1 || s[1] != ref || s[2] != (GLint)mask) glStencilFunc(func, ref, mask);
    if (s[3] != (GLint)fail + 1 || s[4] != (GLint)zfail || s[5] != (GLint)pass) glStencilOp(fail, zfail, pass);
    if (s[6] != (GLint)wmask + 1) glStencilMask(wmask);
    s[0] = (GLint)func + 1, s[1] = ref, s[2] = (GLint)mask, s[3] = (GLint)fail + 1, s[4] = (GLint)zfail, s[5] = (GLint)pass;
    s[6] = (GLint)wmask + 1;
}

static void bind_fb(GLuint f) { if (sh.fb != f + 1) glBindFramebuffer(GL_FRAMEBUFFER, f), sh.fb = f + 1; }
static void viewport(GLint x, GLint y, GLint w, GLint h)
{
    GLint *v = sh.viewport;
    if (v[0] != x || v[1] != y || v[2] != w + 1 || v[3] != h) glViewport(x, y, w, h);
    v[0] = x, v[1] = y, v[2] = w + 1, v[3] = h;
}
static void scissor_box(GLint x, GLint y, GLint w, GLint h)
{
    GLint *v = sh.scissor;
    if (v[0] != x || v[1] != y || v[2] != w + 1 || v[3] != h) glScissor(x, y, w, h);
    v[0] = x, v[1] = y, v[2] = w + 1, v[3] = h;
}
static void active(int unit) { if (sh.active != GL_TEXTURE0 + (GLenum)unit + 1) glActiveTexture(GL_TEXTURE0 + (GLenum)unit), sh.active = GL_TEXTURE0 + (GLenum)unit + 1; }
static void bind_tex(int unit, GLenum target, GLuint t)
{
    GLuint *b = target == GL_TEXTURE_2D ? sh.tex2d : sh.cube;
    if (b[unit] == t + 1) return;
    active(unit);
    glBindTexture(target, t);
    b[unit] = t + 1;
}
static void bind_sampler(int unit, GLuint s) { if (sh.sampler[unit] != s + 1) glBindSampler((GLuint)unit, s), sh.sampler[unit] = s + 1; }
static void sampler_params(int t, GLint min, GLint mag, GLint ws, GLint wt)
{
    GLint *p = sh.sparam[t];
    GLuint s = samplers[t];
    if (p[0] != min) glSamplerParameteri(s, GL_TEXTURE_MIN_FILTER, min);
    if (p[1] != mag) glSamplerParameteri(s, GL_TEXTURE_MAG_FILTER, mag);
    if (p[2] != ws) glSamplerParameteri(s, GL_TEXTURE_WRAP_S, ws);
    if (p[3] != wt) glSamplerParameteri(s, GL_TEXTURE_WRAP_T, wt);
    p[0] = min, p[1] = mag, p[2] = ws, p[3] = wt;
}
static void bind_buffer(GLenum target, GLuint b)
{
    GLuint *c = target == GL_ARRAY_BUFFER ? &sh.vbuf : &sh.ibuf;
    if (*c != b + 1) glBindBuffer(target, b), *c = b + 1;
}

/* attribute i from the bound array buffer, or (size 0) off with the constant v */
static void attr(GLuint i, GLint size, GLenum type, GLboolean norm, GLsizei stride, uintptr_t off, const float v[4])
{
    uint8_t b = (uint8_t)(1u << i);
    if (size) {
        if (!(sh.attr_on & b)) glEnableVertexAttribArray(i), sh.attr_on |= b;
        GLuint buf = sh.vbuf;
        if (sh.attr[i].buf != buf || sh.attr[i].size != size || sh.attr[i].type != type || sh.attr[i].norm != norm ||
            sh.attr[i].stride != stride || sh.attr[i].off != off) {
            glVertexAttribPointer(i, size, type, norm, stride, (const void *)off);
            sh.attr[i].buf = buf, sh.attr[i].size = size, sh.attr[i].type = type, sh.attr[i].norm = norm;
            sh.attr[i].stride = stride, sh.attr[i].off = off;
        }
        return;
    }
    if (sh.attr_on & b || !(sh.attr_const_known & b)) glDisableVertexAttribArray(i), sh.attr_on &= (uint8_t)~b;
    if (!(sh.attr_const_known & b) || memcmp(sh.attr_const[i], v, 16)) {
        glVertexAttrib4f(i, v[0], v[1], v[2], v[3]);
        memcpy(sh.attr_const[i], v, 16);
        sh.attr_const_known |= b;
    }
}

/* ---------------------------------------------------------------- shaders */

static const char *VS =
    "#version 300 es\n"
    "precision highp float; precision highp int;\n"
    "layout(location=0) in vec4 a_pos;\n"        /* x, y, z, rhw (pre-transformed) or x, y, z (model) */
    "layout(location=1) in vec3 a_normal;\n"
    "layout(location=2) in vec4 a_diffuse;\n"    /* D3DCOLOR bytes: b, g, r, a */
    "layout(location=3) in vec4 a_specular;\n"
    "layout(location=4) in vec2 a_uv0;\n"
    "layout(location=5) in vec2 a_uv1;\n"
    "uniform vec2 u_mode;\n"
    "uniform int u_tl;\n"
    "uniform mat4 u_wv, u_proj; uniform mat3 u_nrm;\n"   /* D3D row-vector matrices, as uploaded */
    "uniform vec4 u_vp; uniform vec2 u_vpz;\n"
    "uniform int u_tci[2], u_texflags[2];\n"
    "uniform mat4 u_texm0, u_texm1;\n"
    "uniform float u_pointsize;\n"
    "uniform int u_lighting, u_colorvertex, u_localviewer, u_normalize, u_specular, u_vfog, u_rangefog;\n"
    "uniform ivec2 u_has_colour;\n"              /* the vertex has diffuse, specular */
    "uniform ivec4 u_src;\n"                     /* diffuse, specular, ambient, emissive source */
    "uniform vec4 u_ambient; uniform vec4 u_mat[4]; uniform float u_power;\n"
    "uniform vec3 u_fogparams;\n"
    "uniform int u_nlights; uniform int u_ltype[8];\n"
    "uniform vec4 u_ldiff[8], u_lspec[8], u_lamb[8];\n"
    "uniform vec3 u_lpos[8], u_ldir[8];\n"       /* camera space */
    "uniform vec4 u_lp1[8], u_lp2[8];\n"         /* range, falloff, att0, att1; att2, cos(theta/2), cos(phi/2) */
    "out vec4 v_diffuse; out vec4 v_specular; out vec3 v_uv[2];\n"
    "vec4 source(int s, int which) {\n"          /* D3DMCS: 0 material, 1 colour 1, 2 colour 2 */
    "  if (u_colorvertex != 0 && s == 1 && u_has_colour.x != 0) return a_diffuse.bgra;\n"
    "  if (u_colorvertex != 0 && s == 2 && u_has_colour.y != 0) return a_specular.bgra;\n"
    "  return u_mat[which];\n"
    "}\n"
    "vec3 texcoord(int s, vec3 pc, vec3 n) {\n"
    "  int tci = u_tci[s];\n"
    "  int gen = tci >> 16; vec4 c;\n"
    "  if (gen == 1) c = vec4(n, 1.0);\n"
    "  else if (gen == 2) c = vec4(pc, 1.0);\n"
    "  else if (gen == 3) {\n"
    "    vec3 e = u_localviewer != 0 ? normalize(pc) : vec3(0.0, 0.0, 1.0);\n"
    "    c = vec4(reflect(e, n), 1.0);\n"
    "  } else c = vec4((tci & 0xffff) == 1 ? a_uv1 : a_uv0, 1.0, 0.0);\n"
    "  if ((u_texflags[s] & 255) != 0) c = (s == 0 ? u_texm0 : u_texm1) * c;\n"
    "  return c.xyz;\n"
    "}\n"
    "void main() {\n"
    "  if (u_tl == 0) {\n"
    "    float w = a_pos.w > 0.0 ? 1.0 / a_pos.w : 1.0;\n"
    "    vec2 ndc = (a_pos.xy + 0.5) / u_mode * 2.0 - 1.0;\n"
    "    gl_Position = vec4(ndc * w, (a_pos.z * 2.0 - 1.0) * w, w);\n"
    "    v_diffuse = a_diffuse.bgra; v_specular = a_specular.bgra;\n"
    "    v_uv[0] = vec3(u_tci[0] == 1 ? a_uv1 : a_uv0, 0.0);\n"
    "    v_uv[1] = vec3(u_tci[1] == 1 ? a_uv1 : a_uv0, 0.0);\n"
    "  } else {\n"
    "    vec3 pc = (u_wv * vec4(a_pos.xyz, 1.0)).xyz;\n"
    "    vec4 cl = u_proj * vec4(pc, 1.0);\n"
    /* the viewport mapping x' = vp.x + (1 + x/w) vp.w / 2, y' = vp.y + (1 - y/w) vp.h / 2,
       z' = minz + z/w (maxz - minz), then the pre-transformed path's, times w */
    "    gl_Position = vec4(((2.0 * u_vp.x + u_vp.z + 1.0) / u_mode.x - 1.0) * cl.w + u_vp.z / u_mode.x * cl.x,\n"
    "                       ((2.0 * u_vp.y + u_vp.w + 1.0) / u_mode.y - 1.0) * cl.w - u_vp.w / u_mode.y * cl.y,\n"
    "                       (2.0 * u_vpz.x - 1.0) * cl.w + 2.0 * (u_vpz.y - u_vpz.x) * cl.z, cl.w);\n"
    "    vec3 n = u_nrm * a_normal;\n"
    "    if (u_normalize != 0 && dot(n, n) > 0.0) n = normalize(n);\n"
    "    vec4 dif, spe;\n"
    "    if (u_lighting != 0) {\n"
    "      vec4 md = source(u_src.x, 0), ms = source(u_src.y, 2), ma = source(u_src.z, 1), me = source(u_src.w, 3);\n"
    "      vec3 amb = u_ambient.rgb, d = vec3(0.0), s = vec3(0.0);\n"
    "      vec3 eye = u_localviewer != 0 ? -normalize(pc) : vec3(0.0, 0.0, -1.0);\n"
    "      for (int i = 0; i < 8; i++) {\n"
    "        if (i >= u_nlights) break;\n"
    "        vec3 l; float att = 1.0;\n"
    "        if (u_ltype[i] == 3) l = -normalize(u_ldir[i]);\n"
    "        else {\n"
    "          vec3 v = u_lpos[i] - pc; float dist = length(v);\n"
    "          if (dist > u_lp1[i].x) continue;\n"
    "          l = v / max(dist, 1e-6);\n"
    "          float q = u_lp1[i].z + u_lp1[i].w * dist + u_lp2[i].x * dist * dist;\n"
    "          att = q > 0.0 ? 1.0 / q : 1.0;\n"
    "          if (u_ltype[i] == 2) {\n"
    "            float rho = dot(normalize(u_ldir[i]), -l);\n"
    "            if (rho <= u_lp2[i].z) att = 0.0;\n"
    "            else if (rho < u_lp2[i].y) att *= pow((rho - u_lp2[i].z) / (u_lp2[i].y - u_lp2[i].z), u_lp1[i].y);\n"
    "          }\n"
    "        }\n"
    "        amb += att * u_lamb[i].rgb;\n"
    "        float nl = dot(n, l);\n"
    "        if (nl > 0.0) {\n"
    "          d += att * nl * u_ldiff[i].rgb;\n"
    "          if (u_specular != 0) {\n"
    "            float nh = dot(n, normalize(eye + l));\n"
    "            if (nh > 0.0) s += att * pow(nh, u_power) * u_lspec[i].rgb;\n"
    "          }\n"
    "        }\n"
    "      }\n"
    "      dif = vec4(clamp(me.rgb + ma.rgb * amb + md.rgb * d, 0.0, 1.0), md.a);\n"
    "      spe = vec4(clamp(ms.rgb * s, 0.0, 1.0), 0.0);\n"
    "    } else {\n"
    "      dif = u_has_colour.x != 0 ? a_diffuse.bgra : vec4(1.0);\n"
    "      spe = u_has_colour.y != 0 ? a_specular.bgra : vec4(0.0);\n"
    "    }\n"
    /* vertex fog into the specular alpha (FOGVERTEXMODE with table fog off): eye depth or range */
    "    if (u_vfog != 0) {\n"
    "      float z = u_rangefog != 0 ? length(pc) : pc.z, f;\n"
    "      if (u_vfog == 1) f = exp(-z * u_fogparams.z);\n"
    "      else if (u_vfog == 2) f = exp(-(z * u_fogparams.z) * (z * u_fogparams.z));\n"
    "      else f = (u_fogparams.y - z) / (u_fogparams.y - u_fogparams.x);\n"
    "      spe.a = clamp(f, 0.0, 1.0);\n"
    "    }\n"
    "    v_diffuse = dif; v_specular = spe;\n"
    "    v_uv[0] = texcoord(0, pc, n);\n"
    "    v_uv[1] = texcoord(1, pc, n);\n"
    "  }\n"
    "  gl_PointSize = u_pointsize;\n"
    "}\n";

static const char *FS =
    "#version 300 es\n"
    "precision highp float; precision highp int;\n"
    "in vec4 v_diffuse; in vec4 v_specular; in vec3 v_uv[2];\n"
    "uniform sampler2D u_tex0, u_tex1;\n"
    "uniform samplerCube u_texc; uniform int u_cube0;\n"
    "uniform vec4 u_bumpmat; uniform vec2 u_bumplum;\n"   /* stage 0's BUMPENVMAT00, 01, 10, 11; LSCALE, LOFFSET */   /* stage 0's texture a cube: its direction */
    "uniform ivec2 u_stage_op[2];\n"             /* colour op, alpha op (D3DTOP) */
    "uniform ivec4 u_stage_arg[2];\n"            /* colour arg 1, 2, alpha arg 1, 2 (D3DTA) */
    "uniform vec4 u_tfactor;\n"
    "uniform int u_alphatest, u_alpharef, u_alphafunc;\n"
    "uniform int u_fog, u_fogmode, u_specular;\n"
    "uniform vec3 u_fogcolour; uniform vec3 u_fogparams;\n"   /* start, end, density */
    "uniform float u_lodbias[2];\n"
    "out vec4 o;\n"
    "vec4 arg(int a, vec4 tex, vec4 cur) {\n"
    "  int s = a & 15; vec4 r;\n"
    "  if (s == 0) r = v_diffuse; else if (s == 1) r = cur; else if (s == 2) r = tex;\n"
    "  else if (s == 3) r = u_tfactor; else r = v_specular;\n"
    "  if ((a & 16) != 0) r = vec4(1.0) - r;\n"
    "  if ((a & 32) != 0) r = vec4(r.a);\n"
    "  return r;\n"
    "}\n"
    "vec4 op(int o, vec4 a1, vec4 a2, vec4 tex, vec4 cur) {\n"
    "  if (o == 2) return a1; if (o == 3) return a2; if (o == 4) return a1 * a2;\n"
    "  if (o == 5) return 2.0 * a1 * a2; if (o == 6) return 4.0 * a1 * a2; if (o == 7) return a1 + a2;\n"
    "  if (o == 8) return a1 + a2 - 0.5; if (o == 9) return 2.0 * (a1 + a2 - 0.5); if (o == 10) return a1 - a2;\n"
    "  if (o == 11) return a1 + a2 - a1 * a2;\n"
    "  if (o == 12) return mix(a2, a1, v_diffuse.a); if (o == 13) return mix(a2, a1, tex.a);\n"
    "  if (o == 14) return mix(a2, a1, u_tfactor.a); if (o == 15) return a1 + a2 * (1.0 - tex.a);\n"
    "  if (o == 16) return mix(a2, a1, cur.a);\n"
    "  if (o == 18) return vec4(a1.rgb + a1.a * a2.rgb, a1.a);\n"          /* MODULATEALPHA_ADDCOLOR */
    "  if (o == 19) return vec4(a1.rgb * a2.rgb + a1.a, a1.a);\n"          /* MODULATECOLOR_ADDALPHA */
    "  if (o == 20) return vec4(a1.rgb + (1.0 - a1.a) * a2.rgb, a1.a);\n"  /* MODULATEINVALPHA_ADDCOLOR */
    "  if (o == 21) return vec4((1.0 - a1.rgb) * a2.rgb + a1.a, a1.a);\n"  /* MODULATEINVCOLOR_ADDALPHA */
    "  if (o == 24) return vec4(clamp(4.0 * dot(a1.rgb - 0.5, a2.rgb - 0.5), 0.0, 1.0));\n"   /* DOTPRODUCT3 */
    "  return cur;\n"
    "}\n"
    "bool cmp(int f, int a, int r) {\n"           /* D3DCMPFUNC */
    "  if (f == 1) return false; if (f == 2) return a < r; if (f == 3) return a == r; if (f == 4) return a <= r;\n"
    "  if (f == 5) return a > r; if (f == 6) return a != r; if (f == 7) return a >= r; return true;\n"
    "}\n"
    "void main() {\n"
    "  vec4 cur = v_diffuse;\n"
    "  vec2 bump = vec2(0.0); float lum = 1.0;\n"
    "  for (int s = 0; s < 2; s++) {\n"
    "    int cop = u_stage_op[s].x, aop = u_stage_op[s].y;\n"
    "    if (cop <= 1) break;\n"                  /* a disabled stage ends the cascade */
    "    vec4 tex = s == 1 ? texture(u_tex1, v_uv[1].xy + bump, u_lodbias[1])\n"
    "             : u_cube0 != 0 ? texture(u_texc, v_uv[0]) : texture(u_tex0, v_uv[0].xy, u_lodbias[0]);\n"
    /* BUMPENVMAP (22), BUMPENVMAPLUMINANCE (23): the bump map's signed du, dv through the matrix move
       the next stage's coordinates, its luminance (scaled and offset) scales that stage's colour; the
       stage passes the current colour on */
    "    if (cop == 22 || cop == 23) {\n"
    "      vec2 d = floor(tex.rg * 255.0 + 0.5); d = max(mix(d, d - 256.0, step(128.0, d)) / 127.0, -1.0);\n"
    "      bump = vec2(u_bumpmat.x * d.x + u_bumpmat.z * d.y, u_bumpmat.y * d.x + u_bumpmat.w * d.y);\n"
    "      if (cop == 23) lum = clamp(tex.b * u_bumplum.x + u_bumplum.y, 0.0, 1.0);\n"
    "      continue;\n"
    "    }\n"
    "    if (s == 1) tex.rgb *= lum;\n"
    "    vec4 c = op(cop, arg(u_stage_arg[s].x, tex, cur), arg(u_stage_arg[s].y, tex, cur), tex, cur);\n"
    "    vec4 a = aop <= 1 ? cur : op(aop, arg(u_stage_arg[s].z, tex, cur), arg(u_stage_arg[s].w, tex, cur), tex, cur);\n"
    "    cur = clamp(vec4(c.rgb, a.a), 0.0, 1.0);\n"
    "  }\n"
    "  if (u_specular != 0) cur.rgb = min(cur.rgb + v_specular.rgb, 1.0);\n"
    "  if (u_fog != 0) {\n"
    "    float f, z = 1.0 / gl_FragCoord.w;\n"
    "    if (u_fogmode == 1) f = exp(-z * u_fogparams.z);\n"
    "    else if (u_fogmode == 2) f = exp(-(z * u_fogparams.z) * (z * u_fogparams.z));\n"
    "    else if (u_fogmode == 3) f = (u_fogparams.y - z) / (u_fogparams.y - u_fogparams.x);\n"
    "    else f = v_specular.a;\n"
    "    cur.rgb = mix(u_fogcolour, cur.rgb, clamp(f, 0.0, 1.0));\n"
    "  }\n"
    "  if (u_alphatest != 0 && !cmp(u_alphafunc, int(floor(cur.a * 255.0 + 0.5)), u_alpharef)) discard;\n"
    "  o = cur;\n"
    "}\n";

static const char *PVS =
    "#version 300 es\n"
    "uniform vec4 u_rect;\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "  vec2 c = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));\n"
    "  v_uv = vec2(c.x, 1.0 - c.y);\n"
    "  gl_Position = vec4(mix(u_rect.xy, u_rect.zw, c), 0.0, 1.0);\n"
    "}\n";

static const char *PFS =
    "#version 300 es\n"
    "precision mediump float;\n"
    "uniform sampler2D u_tex; uniform float u_gamma;\n"
    "in vec2 v_uv; out vec4 o;\n"
    "void main() { o = vec4(min(texture(u_tex, v_uv).rgb * u_gamma, 1.0), 1.0); }\n";

static GLuint shader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = "";
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        plat_log("D3D: shader:");
        plat_log(log);
    }
    return s;
}

static GLuint program(const char *vs, const char *fs)
{
    GLuint p = glCreateProgram();
    GLuint a = shader(GL_VERTEX_SHADER, vs), b = shader(GL_FRAGMENT_SHADER, fs);
    glAttachShader(p, a);
    glAttachShader(p, b);
    glLinkProgram(p);
    glDeleteShader(a);
    glDeleteShader(b);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024] = "";
        glGetProgramInfoLog(p, sizeof log, NULL, log);
        plat_log("D3D: program:");
        plat_log(log);
    }
    return p;
}

#define LOC(n) u.n = glGetUniformLocation(prog, "u_" #n)

static bool gl_open(int width, int height, int bpp)
{
    mw = width;
    mh = height;
    mbpp = bpp;
    scale = 1;   /* (the back buffer at a multiple of the mode: kept as a factor, the mode itself) */
    if (!prog) {
        prog = program(VS, FS);
        present_prog = program(PVS, PFS);
        LOC(mode); LOC(tl); LOC(stage_op); LOC(stage_arg); LOC(tfactor); LOC(alphatest); LOC(alpharef);
        LOC(alphafunc); LOC(fog); LOC(fogmode); LOC(fogcolour); LOC(fogparams); LOC(specular); LOC(lodbias);
        LOC(tci); LOC(texflags); LOC(texm0); LOC(texm1); LOC(tex0); LOC(tex1); LOC(pointsize); LOC(wv);
        LOC(nrm); LOC(proj); LOC(vp); LOC(vpz); LOC(lighting); LOC(colorvertex); LOC(has_colour); LOC(src);
        LOC(ambient); LOC(mat); LOC(power); LOC(localviewer); LOC(normalize); LOC(vfog); LOC(rangefog);
        LOC(nlights); LOC(ltype); LOC(ldiff); LOC(lspec); LOC(lamb); LOC(lpos); LOC(ldir); LOC(lp1); LOC(lp2);
        LOC(texc); LOC(cube0); LOC(bumpmat); LOC(bumplum);
        up.tex = glGetUniformLocation(present_prog, "u_tex");
        up.gamma = glGetUniformLocation(present_prog, "u_gamma");
        up.rect = glGetUniformLocation(present_prog, "u_rect");
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ibo);
        glGenSamplers(STAGES, samplers);
        glGenSamplers(1, &cube_sampler);
        glSamplerParameteri(cube_sampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glSamplerParameteri(cube_sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glSamplerParameteri(cube_sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(cube_sampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        static const uint8_t white[4] = {255, 255, 255, 255};
        glGenTextures(1, &white_tex);
        glBindTexture(GL_TEXTURE_2D, white_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        /* unit 2's cube when stage 0 samples none (a sampler may not see the target being drawn) */
        glGenTextures(1, &white_cube);
        glBindTexture(GL_TEXTURE_CUBE_MAP, white_cube);
        for (int f = 0; f < 6; f++)
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)f, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    }
    if (fbo[0]) {
        glDeleteFramebuffers(2, fbo);
        glDeleteTextures(2, colour);
        glDeleteRenderbuffers(1, &depth);
    }
    /* 16-bit modes draw into RGB565 (the original's banding), 32-bit into RGB8. One depth buffer. */
    GLenum fmt = bpp <= 16 ? GL_RGB565 : GL_RGB8;
    glGenFramebuffers(2, fbo);
    glGenTextures(2, colour);
    glGenRenderbuffers(1, &depth);
    glBindRenderbuffer(GL_RENDERBUFFER, depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, mw * scale, mh * scale);
    for (int i = 0; i < 2; i++) {
        glBindTexture(GL_TEXTURE_2D, colour[i]);
        glTexStorage2D(GL_TEXTURE_2D, 1, fmt, mw * scale, mh * scale);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colour[i], 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0, 0, 0, 1);
        glClearDepthf(1.0f);
        glClearStencil(0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    }
    shadow_reset();
    uniforms_reset();
    return true;
}

static void gl_texture_upload(D3dTexture *t);

/* ---------------------------------------------------------------- state */

static GLenum cmpfunc(uint32_t f)
{
    static const GLenum m[9] = {GL_ALWAYS, GL_NEVER, GL_LESS, GL_EQUAL, GL_LEQUAL, GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS};
    return f <= 8 ? m[f] : GL_ALWAYS;
}

static GLenum blendfactor(uint32_t b, bool src)
{
    switch (b) {
    case 1: return GL_ZERO;
    case 2: return GL_ONE;
    case 3: return GL_SRC_COLOR;
    case 4: return GL_ONE_MINUS_SRC_COLOR;
    case 5: return GL_SRC_ALPHA;
    case 6: return GL_ONE_MINUS_SRC_ALPHA;
    case 7: return GL_DST_ALPHA;
    case 8: return GL_ONE_MINUS_DST_ALPHA;
    case 9: return GL_DST_COLOR;
    case 10: return GL_ONE_MINUS_DST_COLOR;
    case 11: return GL_SRC_ALPHA_SATURATE;
    case 12: return src ? GL_SRC_ALPHA : GL_ONE_MINUS_SRC_ALPHA;   /* BOTHSRCALPHA */
    case 13: return src ? GL_ONE_MINUS_SRC_ALPHA : GL_SRC_ALPHA;   /* BOTHINVSRCALPHA */
    default: return src ? GL_ONE : GL_ZERO;
    }
}

static GLenum stencilop(uint32_t o)
{
    static const GLenum m[9] = {GL_KEEP, GL_KEEP, GL_ZERO, GL_REPLACE, GL_INCR, GL_DECR, GL_INVERT, GL_INCR_WRAP, GL_DECR_WRAP};
    return o <= 8 ? m[o] : GL_KEEP;
}

/* D3DTADDRESS: 1 wrap, 2 mirror, 3 clamp, 4 border (as clamp) */
static GLenum wrap(uint32_t a) { return a == 1 ? GL_REPEAT : a == 2 ? GL_MIRRORED_REPEAT : GL_CLAMP_TO_EDGE; }

static float bits_float(uint32_t v)
{
    float f;
    memcpy(&f, &v, 4);
    return f;
}

static void colour4(uint32_t c, float *f)
{
    f[0] = (float)(c >> 16 & 255) / 255.0f;
    f[1] = (float)(c >> 8 & 255) / 255.0f;
    f[2] = (float)(c & 255) / 255.0f;
    f[3] = (float)(c >> 24) / 255.0f;
}

/* the target being drawn: the back buffer (the mode at its scale) or a cube's face (its size, 1:1) */
static int tw, th, tscale;

static void cube_alloc(D3dTexture *t);

static void bind_target(const D3dState *s)
{
    D3dTexture *c = s->target;
    if (c && (c->gl || (cube_alloc(c), c->gl))) {
        bind_fb(c->fbo[s->target_face]);
        tw = th = c->width, tscale = 1;
    } else {
        bind_fb(fbo[1]);
        tw = mw, th = mh, tscale = scale;
    }
    viewport(0, 0, tw * tscale, th * tscale);
}

static void scissor(int x0, int y0, int x1, int y1)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > tw) x1 = tw;
    if (y1 > th) y1 = th;
    if (x1 < x0) x1 = x0;
    if (y1 < y0) y1 = y0;
    cap(GL_SCISSOR_TEST, true);
    scissor_box(x0 * tscale, y0 * tscale, (x1 - x0) * tscale, (y1 - y0) * tscale);
}

static void mul(const D3DMATRIX *a, const D3DMATRIX *b, D3DMATRIX *o)
{
    D3DMATRIX r;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            r.m[i][j] = a->m[i][0] * b->m[0][j] + a->m[i][1] * b->m[1][j] + a->m[i][2] * b->m[2][j] + a->m[i][3] * b->m[3][j];
    *o = r;
}

/* The normal matrix: the inverse transpose of the world-view's 3x3 (for row vectors n' = n N). */
static void normal_matrix(const D3DMATRIX *wv, float out[9])
{
    const float(*m)[4] = wv->m;
    float c00 = m[1][1] * m[2][2] - m[1][2] * m[2][1], c01 = m[1][2] * m[2][0] - m[1][0] * m[2][2],
          c02 = m[1][0] * m[2][1] - m[1][1] * m[2][0];
    float det = m[0][0] * c00 + m[0][1] * c01 + m[0][2] * c02;
    float k = fabsf(det) > 1e-20f ? 1.0f / det : 1.0f;
    float cof[3][3] = {
        {c00, c01, c02},
        {m[0][2] * m[2][1] - m[0][1] * m[2][2], m[0][0] * m[2][2] - m[0][2] * m[2][0], m[0][1] * m[2][0] - m[0][0] * m[2][1]},
        {m[0][1] * m[1][2] - m[0][2] * m[1][1], m[0][2] * m[1][0] - m[0][0] * m[1][2], m[0][0] * m[1][1] - m[0][1] * m[1][0]},
    };
    /* N = (M^-1)^T is the cofactor matrix over det. The shader computes (A * n) with A read column-major
       from out, and n N needs A = N^T: out[col * 3 + row] = N[col][row]. */
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) out[c * 3 + r] = cof[c][r] * k;
}

static void xform3(const D3DMATRIX *m, const D3DVECTOR *v, float w, float o[3])
{
    for (int j = 0; j < 3; j++) o[j] = v->x * m->m[0][j] + v->y * m->m[1][j] + v->z * m->m[2][j] + w * m->m[3][j];
}

static void lights(const D3dState *s)
{
    GLint type[8];
    float diff[32], spec[32], amb[32], pos[24], dir[24], p1[32], p2[32];
    int n = 0;
    for (int i = 0; i < D3D_LIGHTS; i++) {
        if (!s->light_on[i]) continue;
        const D3DLIGHT7 *l = &s->light[i];
        type[n] = (GLint)l->type;
        memcpy(diff + 4 * n, &l->diffuse, 16);
        memcpy(spec + 4 * n, &l->specular, 16);
        memcpy(amb + 4 * n, &l->ambient, 16);
        xform3(&s->view, &l->position, 1.0f, pos + 3 * n);
        xform3(&s->view, &l->direction, 0.0f, dir + 3 * n);
        p1[4 * n] = l->range > 0 ? l->range : 1e30f;
        p1[4 * n + 1] = l->falloff;
        p1[4 * n + 2] = l->attenuation0;
        p1[4 * n + 3] = l->attenuation1;
        p2[4 * n] = l->attenuation2;
        p2[4 * n + 1] = cosf(l->theta * 0.5f);
        p2[4 * n + 2] = cosf(l->phi * 0.5f);
        p2[4 * n + 3] = 0;
        n++;
    }
    u1i(u.nlights, n);
    if (!n) return;
    u1iv(u.ltype, n, type);
    u4fv(u.ldiff, n, diff);
    u4fv(u.lspec, n, spec);
    u4fv(u.lamb, n, amb);
    u3fv(u.lpos, n, pos);
    u3fv(u.ldir, n, dir);
    u4fv(u.lp1, n, p1);
    u4fv(u.lp2, n, p2);
}

static void apply_state(const D3dState *s, int prim, uint32_t fvf)
{
    const uint32_t *rs = s->rs;
    for (int t = 0; t < STAGES; t++)
        if (s->tex[t] && s->tex[t]->cube && !s->tex[t]->gl) cube_alloc(s->tex[t]);   /* (it binds its faces) */
    bind_target(s);
    const D3DVIEWPORT7 *v = &s->viewport;
    scissor((int)v->x, (int)v->y, (int)(v->x + v->width), (int)(v->y + v->height));
    use_program(prog);

    if (rs[D3DRS_ZENABLE]) {
        cap(GL_DEPTH_TEST, true);
        depth_func(cmpfunc(rs[D3DRS_ZFUNC]));
    } else {
        cap(GL_DEPTH_TEST, false);
    }
    depth_mask(rs[D3DRS_ZWRITEENABLE] ? GL_TRUE : GL_FALSE);
    if (rs[D3DRS_ALPHABLENDENABLE]) {
        cap(GL_BLEND, true);
        blend_func(blendfactor(rs[D3DRS_SRCBLEND], true), blendfactor(rs[D3DRS_DESTBLEND], false));
    } else {
        cap(GL_BLEND, false);
    }
    /* Facing: the target keeps Direct3D's rows, so its clockwise is GL's counter-clockwise. */
    if (prim >= D3DPT_TRIANGLELIST && rs[D3DRS_CULLMODE] != 1) {
        cap(GL_CULL_FACE, true);
        front_face(GL_CCW);
        cull_face(rs[D3DRS_CULLMODE] == 2 ? GL_FRONT : GL_BACK);
    } else {
        cap(GL_CULL_FACE, false);
    }
    if (rs[D3DRS_STENCILENABLE]) {
        cap(GL_STENCIL_TEST, true);
        stencil(cmpfunc(rs[D3DRS_STENCILFUNC]), (GLint)rs[D3DRS_STENCILREF], rs[D3DRS_STENCILMASK], stencilop(rs[D3DRS_STENCILFAIL]),
                stencilop(rs[D3DRS_STENCILZFAIL]), stencilop(rs[D3DRS_STENCILPASS]), rs[D3DRS_STENCILWRITEMASK]);
    } else {
        cap(GL_STENCIL_TEST, false);
    }
    cap(GL_DITHER, rs[D3DRS_DITHERENABLE] != 0);

    GLint ops[4], args[8], tci[2], tflags[2];
    D3dTexture *c0 = s->tex[0] && s->tex[0]->cube && s->tex[0] != s->target ? s->tex[0] : NULL;
    bind_tex(2, GL_TEXTURE_CUBE_MAP, c0 ? c0->gl : white_cube);
    bind_sampler(2, cube_sampler);
    GLfloat bias[2];
    for (int t = 0; t < STAGES; t++) {
        const uint32_t *ts = s->tss[t];
        /* a stage without a texture ends the cascade (stage 0: the diffuse colour), as in OpenNFSPU */
        ops[t * 2] = s->tex[t] ? (GLint)ts[D3DTSS_COLOROP] : 1;
        ops[t * 2 + 1] = (GLint)ts[D3DTSS_ALPHAOP];
        args[t * 4] = (GLint)ts[D3DTSS_COLORARG1];
        args[t * 4 + 1] = (GLint)ts[D3DTSS_COLORARG2];
        args[t * 4 + 2] = (GLint)ts[D3DTSS_ALPHAARG1];
        args[t * 4 + 3] = (GLint)ts[D3DTSS_ALPHAARG2];
        tci[t] = (GLint)ts[D3DTSS_TEXCOORDINDEX];
        tflags[t] = (GLint)ts[D3DTSS_TEXTURETRANSFORMFLAGS];
        bias[t] = bits_float(ts[D3DTSS_MIPMAPLODBIAS]);
        if (!isfinite(bias[t])) bias[t] = 0;
        D3dTexture *tx = s->tex[t];
        if (tx && tx->cube) tx = NULL;   /* a cube on stage 0 is sampled from unit 2 by the direction */
        else if (tx && (tx->dirty || !tx->gl)) gl_texture_upload(tx);
        bind_tex(t, GL_TEXTURE_2D, tx && tx->gl ? tx->gl : white_tex);
        /* every texture has one level (the files carry no mip chain), so the mip filter has no effect */
        sampler_params(t, ts[D3DTSS_MINFILTER] >= 2 ? GL_LINEAR : GL_NEAREST, ts[D3DTSS_MAGFILTER] >= 2 ? GL_LINEAR : GL_NEAREST,
                       (GLint)wrap(ts[D3DTSS_ADDRESSU]), (GLint)wrap(ts[D3DTSS_ADDRESSV]));
        bind_sampler(t, samplers[t]);
    }
    u1i(u.tex0, 0);
    u1i(u.tex1, 1);
    u1i(u.texc, 2);
    u1i(u.cube0, c0 != NULL);
    const uint32_t *t0 = s->tss[0];
    u4f(u.bumpmat, bits_float(t0[D3DTSS_BUMPENVMAT00]), bits_float(t0[D3DTSS_BUMPENVMAT01]),
                bits_float(t0[D3DTSS_BUMPENVMAT10]), bits_float(t0[D3DTSS_BUMPENVMAT11]));
    u2f(u.bumplum, bits_float(t0[D3DTSS_BUMPENVLSCALE]), bits_float(t0[D3DTSS_BUMPENVLOFFSET]));
    u2iv(u.stage_op, 2, ops);
    u4iv(u.stage_arg, 2, args);
    u1fv(u.lodbias, 2, bias);
    u2f(u.mode, (float)tw, (float)th);
    u1f(u.pointsize, (float)tscale);
    float tf[4], fc[4];
    colour4(rs[D3DRS_TEXTUREFACTOR], tf);
    u4fv(u.tfactor, 1, tf);
    u1i(u.alphatest, rs[D3DRS_ALPHATESTENABLE] != 0);
    u1i(u.alpharef, (GLint)(rs[D3DRS_ALPHAREF] & 0xff));
    u1i(u.alphafunc, (GLint)rs[D3DRS_ALPHAFUNC]);
    u1i(u.fog, rs[D3DRS_FOGENABLE] != 0);
    u1i(u.fogmode, (GLint)rs[D3DRS_FOGTABLEMODE]);
    colour4(rs[D3DRS_FOGCOLOR], fc);
    u3fv(u.fogcolour, 1, fc);
    u3f(u.fogparams, bits_float(rs[D3DRS_FOGSTART]), bits_float(rs[D3DRS_FOGEND]), bits_float(rs[D3DRS_FOGDENSITY]));
    u1i(u.specular, rs[D3DRS_SPECULARENABLE] != 0);

    bool tl = !(fvf & D3DFVF_XYZRHW);
    u1i(u.tl, tl);
    if (!tl) {
        GLint plain[2] = {(GLint)(tci[0] & 0xffff), (GLint)(tci[1] & 0xffff)};
        u1iv(u.tci, 2, plain);
        return;
    }
    u1iv(u.tci, 2, tci);
    u1iv(u.texflags, 2, tflags);
    um4(u.texm0, &s->texm[0].m[0][0]);
    um4(u.texm1, &s->texm[1].m[0][0]);
    /* a D3D matrix uploaded as it is (row-major) is its transpose to GLSL: (M * v) there is v M */
    D3DMATRIX wv;
    mul(&s->world, &s->view, &wv);
    um4(u.wv, &wv.m[0][0]);
    um4(u.proj, &s->proj.m[0][0]);
    float nm[9];
    normal_matrix(&wv, nm);
    um3(u.nrm, nm);
    u4f(u.vp, (float)v->x, (float)v->y, (float)v->width, (float)v->height);
    u2f(u.vpz, v->minz, v->maxz);
    /* without normals the lighting still runs (on a zero normal: ambient and emissive only) */
    u1i(u.lighting, rs[D3DRS_LIGHTING] != 0);
    u1i(u.colorvertex, rs[D3DRS_COLORVERTEX] != 0);
    u2i(u.has_colour, (fvf & D3DFVF_DIFFUSE) != 0, (fvf & D3DFVF_SPECULAR) != 0);
    u4i(u.src, (GLint)rs[D3DRS_DIFFUSEMATERIALSOURCE], (GLint)rs[D3DRS_SPECULARMATERIALSOURCE],
                (GLint)rs[D3DRS_AMBIENTMATERIALSOURCE], (GLint)rs[D3DRS_EMISSIVEMATERIALSOURCE]);
    float amb[4];
    colour4(rs[D3DRS_AMBIENT], amb);
    u4fv(u.ambient, 1, amb);
    u4fv(u.mat, 4, &s->material.diffuse.r);   /* diffuse, ambient, specular, emissive */
    u1f(u.power, s->material.power);
    u1i(u.localviewer, rs[D3DRS_LOCALVIEWER] != 0);
    u1i(u.normalize, rs[D3DRS_NORMALIZENORMALS] != 0);
    u1i(u.vfog, rs[D3DRS_FOGENABLE] && !rs[D3DRS_FOGTABLEMODE] ? (GLint)rs[D3DRS_FOGVERTEXMODE] : 0);
    u1i(u.rangefog, rs[D3DRS_RANGEFOGENABLE] != 0);
    lights(s);
}

/* ---------------------------------------------------------------- draw */

static void attribs(uint32_t fvf, uint32_t stride, uintptr_t base)
{
    static const float zero3[4] = {0, 0, 0, 1}, one[4] = {1, 1, 1, 1}, zero[4] = {0, 0, 0, 0}, zero2[4] = {0, 0, 0, 1};
    GLsizei st = (GLsizei)stride;
    uintptr_t o = base;
    int npos = fvf & D3DFVF_XYZRHW ? 4 : 3;
    attr(0, npos, GL_FLOAT, GL_FALSE, st, o, NULL);
    o += (uintptr_t)npos * 4;
    if (fvf & D3DFVF_NORMAL) attr(1, 3, GL_FLOAT, GL_FALSE, st, o, NULL), o += 12;
    else attr(1, 0, 0, 0, 0, 0, zero3);
    if (fvf & D3DFVF_DIFFUSE) attr(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, st, o, NULL), o += 4;
    else attr(2, 0, 0, 0, 0, 0, one);
    if (fvf & D3DFVF_SPECULAR) attr(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, st, o, NULL), o += 4;
    else attr(3, 0, 0, 0, 0, 0, zero);
    int ntex = (int)(fvf >> 8 & 15);
    if (ntex >= 1) attr(4, 2, GL_FLOAT, GL_FALSE, st, o, NULL);
    else attr(4, 0, 0, 0, 0, 0, zero2);
    attr(5, 2, GL_FLOAT, GL_FALSE, st, o + (ntex >= 2 ? 8 : 0), NULL);
}

static void gl_draw(const D3dState *s, int prim, uint32_t fvf, const void *verts, uint32_t nverts,
                    const uint16_t *indices, uint32_t nindices, D3dVB *vb, uint32_t first)
{
    if (!fbo[1]) return;
    static const GLenum mode[7] = {GL_POINTS, GL_POINTS, GL_LINES, GL_LINE_STRIP, GL_TRIANGLES, GL_TRIANGLE_STRIP, GL_TRIANGLE_FAN};
    uint32_t stride = D3d_FvfStride(fvf);
    apply_state(s, prim, fvf);
    if (sh.vao != vao + 1) glBindVertexArray(vao), sh.vao = vao + 1, sh.ibuf = 0;
    if (vb) {
        /* a vertex buffer lives in its GL buffer, re-uploaded after an Unlock */
        if (!vb->gl) glGenBuffers(1, &vb->gl), vb->sized = false, vb->dirty = true;
        bind_buffer(GL_ARRAY_BUFFER, vb->gl);
        if (vb->dirty) {
            PROF_BEGIN(PROF_VBUP);
            /* the whole buffer once, then only the vertices changed since (a mesh's colours: the
               ghost's fade rewrote whole pool buffers each frame) */
            if (vb->stream) {
                /* new storage, so no wait for the frames still drawing from the old */
                if (vb->hi) glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)vb->hi * stride, vb->data, GL_STREAM_DRAW);
            } else if (!vb->sized) {
                glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)vb->count * stride, vb->data, GL_STATIC_DRAW);
                vb->sized = true;
            } else if (vb->hi > vb->lo)
                glBufferSubData(GL_ARRAY_BUFFER, (GLintptr)vb->lo * stride, (GLsizeiptr)(vb->hi - vb->lo) * stride,
                                vb->data + (size_t)vb->lo * stride);
            vb->dirty = false;
            PROF_END(PROF_VBUP);
        }
        attribs(fvf, stride, (uintptr_t)first * stride);
    } else {
        bind_buffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)nverts * stride, verts, GL_STREAM_DRAW);
        attribs(fvf, stride, 0);
    }
    if (indices && nindices) {
        bind_buffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)nindices * 2, indices, GL_STREAM_DRAW);
        glDrawElements(mode[prim], (GLsizei)nindices, GL_UNSIGNED_SHORT, (const void *)0);
    } else {
        glDrawArrays(mode[prim], 0, (GLsizei)nverts);
    }
}

static void gl_clear(const D3dState *s, int x0, int y0, int x1, int y1, uint32_t flags, uint32_t c, float z,
                     uint32_t stencil)
{
    if (!fbo[1]) return;
    bind_target(s);
    const D3DVIEWPORT7 *v = &s->viewport;
    int vx1 = (int)(v->x + v->width), vy1 = (int)(v->y + v->height);
    scissor(x0 > (int)v->x ? x0 : (int)v->x, y0 > (int)v->y ? y0 : (int)v->y, x1 < vx1 ? x1 : vx1, y1 < vy1 ? y1 : vy1);
    GLbitfield m = 0;
    if (flags & D3DCLEAR_TARGET) {
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClearColor((float)(c >> 16 & 255) / 255.0f, (float)(c >> 8 & 255) / 255.0f, (float)(c & 255) / 255.0f, 1.0f);
        m |= GL_COLOR_BUFFER_BIT;
    }
    if (flags & D3DCLEAR_ZBUFFER) {
        depth_mask(GL_TRUE);
        glClearDepthf(z < 0 ? 0 : z > 1 ? 1 : z);
        m |= GL_DEPTH_BUFFER_BIT;
    }
    if (flags & D3DCLEAR_STENCIL) {
        glStencilMask(0xffffffffu);
        sh.stencil[6] = 0;   /* (the write mask: not known) */
        glClearStencil((GLint)stencil);
        m |= GL_STENCIL_BUFFER_BIT;
    }
    if (m) glClear(m);
}

/* The flip: back to front, then the front letterboxed onto the drawable (rows flipped there). */
static void gl_present(void)
{
    PROF_BEGIN(PROF_PRESENT);
    if (!fbo[1]) return;
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo[1]);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo[0]);
    glBlitFramebuffer(0, 0, mw * scale, mh * scale, 0, 0, mw * scale, mh * scale, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    int dw, dh;
    plat_display_size(&dw, &dh);
    if (dw <= 0 || dh <= 0) return;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, dw, dh);
    cap(GL_DEPTH_TEST, false);
    cap(GL_BLEND, false);
    cap(GL_CULL_FACE, false);
    cap(GL_STENCIL_TEST, false);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    float sx = (float)dw / (float)mw, sy = (float)dh / (float)mh, k = sx < sy ? sx : sy;
    float w = (float)mw * k / (float)dw, h = (float)mh * k / (float)dh;
    glUseProgram(present_prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, colour[0]);
    glBindSampler(0, 0);
    u1i(up.tex, 0);
    u1f(up.gamma, gamma_scale);
    u4f(up.rect, -w, -h, w, h);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    shadow_reset();
    PROF_END(PROF_PRESENT);
}

static void gl_texture_upload(D3dTexture *t)
{
    PROF_BEGIN(PROF_TEXUP);
    if (!t->gl) glGenTextures(1, &t->gl);
    glBindTexture(GL_TEXTURE_2D, t->gl);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    /* a texture changed each frame (the mud on the body, the movies) is replaced in place: made
       again, the driver may wait for the frames still drawing with it */
    if (t->glw == t->width && t->glh == t->height)
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, t->width, t->height, GL_RGBA, GL_UNSIGNED_BYTE, t->rgba);
    else {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, t->width, t->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, t->rgba);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        t->glw = t->width, t->glh = t->height;
    }
    t->dirty = false;
    shadow_reset();
    PROF_END(PROF_TEXUP);
}

/* a cube's six faces (the mode's colour format, as the original's surfaces take the primary's) each
   a target with the cube's depth buffer, cleared to black */
static void cube_alloc(D3dTexture *t)
{
    int n = t->width;
    if (n <= 0) return;
    glGenTextures(1, &t->gl);
    glBindTexture(GL_TEXTURE_CUBE_MAP, t->gl);
    glTexStorage2D(GL_TEXTURE_CUBE_MAP, 1, mbpp <= 16 ? GL_RGB565 : GL_RGB8, n, n);
    glGenRenderbuffers(1, &t->depth);
    glBindRenderbuffer(GL_RENDERBUFFER, t->depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, n, n);
    glGenFramebuffers(6, t->fbo);
    glDisable(GL_SCISSOR_TEST);
    for (int f = 0; f < 6; f++) {
        glBindFramebuffer(GL_FRAMEBUFFER, t->fbo[f]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)f, t->gl, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, t->depth);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);
        glClearColor(0, 0, 0, 1);
        glClearDepthf(1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
    t->dirty = false;
    shadow_reset();
}

static void gl_texture_free(D3dTexture *t)
{
    if (t->cube && t->fbo[0]) {
        glDeleteFramebuffers(6, t->fbo);
        glDeleteRenderbuffers(1, &t->depth);
        memset(t->fbo, 0, sizeof t->fbo);
        t->depth = 0;
    }
    if (t->gl) glDeleteTextures(1, &t->gl);
    t->gl = 0;
    t->glw = t->glh = 0;
}

static void gl_vb_free(D3dVB *vb)
{
    if (vb->gl) glDeleteBuffers(1, &vb->gl);
    vb->gl = 0;
    vb->sized = false;
}

/* each mode pixel the back buffer's pixel at its corner (the buffer is the mode times scale) */
static void gl_read(int x, int y, int w, int h, uint8_t *rgba)
{
    static uint8_t *buf;
    static size_t cap;
    size_t n = (size_t)w * scale * (size_t)h * scale * 4;
    if (n > cap) {
        uint8_t *b = realloc(buf, n);
        if (!b) return;
        buf = b, cap = n;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo[1]);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(x * scale, y * scale, w * scale, h * scale, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            memcpy(rgba + ((size_t)j * w + i) * 4, buf + ((size_t)j * scale * w * scale + (size_t)i * scale) * 4, 4);
    shadow_reset();
}

static const D3dBackend gl_backend = {gl_open, gl_draw, gl_clear, gl_present, gl_texture_upload,
                                      gl_texture_free, gl_vb_free, gl_read};

void gl_dev_register(void) { D3d_SetBackend(&gl_backend); }
