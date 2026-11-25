#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "hikaru_gfx.h"
#include "gpu.h"

float g_gfx_xscale = 1.0f;
int g_gfx_clean_edges = 0;   /* widescreen "clean edges" option: also catch flat quads whose z differ by rounding */   /* 0.75 in widescreen: the 4:3 image covers the central 3/4, more world at the sides */

int g_gfx_backface_cull = 0;

/* Back-face culling. emit_tri() has already put every triangle in the order its winding bit asks for, so the
 * sign of v0 . (v1 x v2) in eye space says which side faces the eye (valid also for triangles that cross the
 * eye plane). Only opaque polygons (type 3) are culled: the board evidently skips their far side - the camera of
 * the character-select intro sits against the room wall and wall fixtures 0.2 units away would otherwise fill
 * the screen with their backs for a frame or two at every pan (grey flashes) - while translucent, punch-through
 * and background polygons are needed from both sides (shot trails, glass). A mirrored modelview (reflections)
 * reverses the facing. */
int gfx_tri_culled(const CpTri *t) {
    if (!g_gfx_backface_cull || t->polytype != 3 || t->mat.blend == 2) return 0;
    const float *a = t->v[0].pos, *b = t->v[1].pos, *c = t->v[2].pos;
    float d = a[0] * (b[1] * c[2] - b[2] * c[1]) - a[1] * (b[0] * c[2] - b[2] * c[0]) + a[2] * (b[0] * c[1] - b[1] * c[0]);
    return t->mirrored ? d < 0 : d > 0;
}

static void reserve(GfxFrame *f, int n) {
    if (f->nv + n > f->cap) { f->cap = (f->nv + n) * 2 + 1024; f->v = realloc(f->v, f->cap * sizeof(GfxVertex)); }
}

/* the three vertices at the same depth: screen-aligned quad. Rounding error allowed (the letterbox bars of the
 * intro cutscene have z = -0.199999988 / -0.200000018 and were missed by an exact compare). */
static int flat_z(const CpTri *t) {
    float z0 = t->v[0].pos[2], tol = g_gfx_clean_edges ? fabsf(z0) * 1e-5f + 1e-7f : 0.0f;
    return fabsf(t->v[1].pos[2] - z0) <= tol && fabsf(t->v[2].pos[2] - z0) <= tol;
}

static void make_vertex(GfxVertex *o, const CpTri *t, const CpVert *v, const GfxShade *sh, uint32_t texw) {
    const CpViewport *vp = &t->view;
    float w = -v->pos[2];
    float sx = vp->off_x + v->pos[0] * vp->f / (w != 0 ? w : 1e-6f);
    float sy = vp->off_y - v->pos[1] * vp->f / (w != 0 ? w : 1e-6f);
    float xn = (sx / 320.0f - 1.0f) * g_gfx_xscale, yn = sy / 240.0f - 1.0f;
    /* widescreen: flat untextured screen quads that overflow the 4:3 edges (letterbox bars of RETIRED / FINISHED and
       of the cutscenes) reach the edges of the 16:9 picture */
    if (g_gfx_xscale < 1.0f && !((t->mat.flags881 >> 3) & 1) && flat_z(t)) {
        if (sx <= 0.0f) xn = -1.0f; else if (sx >= 640.0f) xn = 1.0f;
    }
    o->clip[0] = xn * w; o->clip[1] = yn * w; o->clip[2] = GFX_NEAR; o->clip[3] = w;
    {   /* 2C1 bit 14: texcoords in whole texels instead of 1/16 (Tatooine ground; verified against the arcade video) */
        float sc = (t->tex.w2c1 & 0x4000) ? 16.0f : 1.0f;
        o->uv[0] = v->uv[0] * sc; o->uv[1] = v->uv[1] * sc; }
    memcpy(o->light, sh->light, sizeof o->light); memcpy(o->spec, sh->spec, sizeof o->spec);
    memcpy(o->base, sh->base, 4); memcpy(o->col1, sh->col1, 4); memcpy(o->fog, sh->fog, 4);
    const CpTexhead *th = &t->tex;
    int bx = 0, by = 0;
    if (th->slotx >= 0x80 && th->sloty >= 0xc0) { bx = (th->slotx - 0x80) * 16; by = (th->sloty - 0xc0) * 16; }
    o->tex[0] = texw;
    o->tex[1] = (uint32_t)bx | ((uint32_t)by << 16);
    o->tex[2] = (uint32_t)th->logw | ((uint32_t)th->logh << 8);
    o->tex[3] = sh->modes;
}

/* Hikaru lighting. Per vertex (Gouraud); the model is the one documented for the board (Odin's notes, X360 port):
 *   lit = (viewport ambient 011 x 691 [shading mode 1] + 691 emission [mode 2] + sum of the lights) x base + specular
 *   light i: attenuation x (max(N.L, 0) + ambient share 051/16) x colour (10 bits, 255 = 1.0)
 *   specular (not in mode 3): attenuation x (R.V)^exponent x light colour x 491 colour / 64
 * base is the texel and / or the 091 / 291 colours, as the texel mode (2C1 bits 29-31) and the alpha mode (881 bit 20)
 * say; it is combined in the fragment shader. Lit only when the material has a shading mode and the lightset is not
 * fully masked. Lights are in eye space. Fog (621) on materials with the 881 depth-blend bit: linear on the eye depth. */
static float clamp01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
#if defined(__SSE__) || defined(__x86_64__) || defined(_M_X64)
#include <xmmintrin.h>
static inline float rsqrt_fast(float x) {   /* ~23-bit accurate: rsqrtss + one Newton step */
    float y = _mm_cvtss_f32(_mm_rsqrt_ss(_mm_set_ss(x)));
    return y * (1.5f - 0.5f * x * y * y);
}
#else
static inline float rsqrt_fast(float x) { return 1.0f / sqrtf(x); }
#endif
static void norm3(float v[3]) { float l2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2]; if (l2 > 1e-24f) { float r = rsqrt_fast(l2); v[0] *= r; v[1] *= r; v[2] *= r; } }

/* per-frame lightset cache: light vectors classified once per frame instead of per vertex */
enum { LT_DIR, LT_POINT, LT_SPOT, LT_UNDIRECTED };
typedef struct { float L[3], dlen, pos[3], axis[3], col[3], amb, att0, att1, cos_outer, cone_scale; uint8_t type, att_type; } GfxLight;
typedef struct { int n; GfxLight l[4]; } GfxLs;
static GfxLs *ls_cache; static int ls_cap; static const CpState *ls_owner; static uint32_t ls_gen; static int ls_n = -1;
static void prepare_lights(const CpState *s) {
    if (ls_owner == s && ls_gen == s->gen && ls_n == s->nls) return;
    if (s->nls > ls_cap) { ls_cap = s->nls * 2 + 64; ls_cache = realloc(ls_cache, ls_cap * sizeof(GfxLs)); }
    for (int i = 0; i < s->nls; i++) {
        const CpLightset *ls = &s->ls_frame[i]; GfxLs *o = &ls_cache[i]; o->n = 0;
        if (ls->mask == 0xf) continue;
        for (int li = 0; li < 4; li++) {
            const CpLight *l = &ls->l[li];
            if (ls->mask & (1 << li)) continue;
            GfxLight *g = &o->l[o->n++];
            /* attenuation type 0 with both parameters 1: none */
            int none = l->att_type == 0 && l->att[0] == 1.0f && l->att[1] == 1.0f;
            g->att_type = none ? 4 : l->att_type; g->att0 = l->att[0]; g->att1 = l->att[1];
            if (none) g->type = (l->has_dir && !l->dir[0] && !l->dir[1] && !l->dir[2]) || (!l->has_dir && !l->has_pos) ? LT_UNDIRECTED : LT_DIR;
            else if (l->has_pos && l->has_spot && l->has_cone) g->type = LT_SPOT;
            else if (l->has_pos) g->type = LT_POINT;
            else g->type = LT_DIR;
            memcpy(g->pos, l->pos, sizeof g->pos); memcpy(g->L, l->dir, sizeof g->L);
            g->dlen = sqrtf(g->L[0] * g->L[0] + g->L[1] * g->L[1] + g->L[2] * g->L[2]); norm3(g->L);
            for (int k = 0; k < 3; k++) g->axis[k] = -l->spot[k];
            norm3(g->axis);
            g->cos_outer = l->cos_outer; g->cone_scale = l->cone_scale;
            g->amb = (float)l->ambient * (1.0f / 16.0f);
            for (int k = 0; k < 3; k++) g->col[k] = l->diffuse[k] / 255.0f;
        }
    }
    ls_owner = s; ls_gen = s->gen; ls_n = s->nls;
}

static const uint8_t texel_modes[8] = { 0, 1, 2, 0, 3, 4, 0, 0 };

void gfx_vertex_shade(const CpState *s, const CpTri *t, int k, const float face_n[3], GfxShade *o) {
    const CpMaterial *m = &t->mat; const CpViewport *vp = &t->view; const CpVert *v = &t->v[k];
    int textured = (m->flags881 >> 3) & 1, mode = m->flags881 & 3, alpha_mode = (m->flags881 >> 4) & 1;
    prepare_lights(s);
    const GfxLs *ls = (t->ls >= 0 && t->ls < s->nls) ? &ls_cache[t->ls] : 0;
    int lit = mode && ls && ls->n;
    memcpy(o->base, m->diffuse, 3); o->base[3] = 255;
    memcpy(o->col1, m->ambient, 3); o->col1[3] = 255;
    o->modes = (textured ? texel_modes[(t->tex.w2c1 >> 29) & 7] : 0) | ((uint32_t)alpha_mode << 3) | ((uint32_t)lit << 4);
    o->light[3] = clamp01(t->polyalpha * (v->alpha / 255.0f));
    o->spec[0] = o->spec[1] = o->spec[2] = o->spec[3] = 0;
    if (!lit) { o->light[0] = o->light[1] = o->light[2] = 1.0f; }
    else {
        float n[3], c[3] = { 0, 0, 0 }, sp[3] = { 0, 0, 0 };
        if (mode == 2 && !v->nrm[0] && !v->nrm[1] && !v->nrm[2]) memcpy(n, face_n, sizeof n);
        else if (!v->nrm[0] && !v->nrm[1] && !v->nrm[2]) memcpy(n, face_n, sizeof n);
        else memcpy(n, v->nrm, sizeof n);
        norm3(n);
        for (int i = 0; i < 3; i++) {
            float a16 = m->amb16[i] / 255.0f;
            if (mode == 1) c[i] += vp->ambient[i] / 255.0f * a16;
            else if (mode == 2) c[i] += a16;
        }
        float V[3] = { -v->pos[0], -v->pos[1], -v->pos[2] }; norm3(V);
        float sexp = (float)m->specular[3];
        int want_spec = mode != 3 && (m->specular[0] | m->specular[1] | m->specular[2]);
        for (int li = 0; li < ls->n; li++) {
            const GfxLight *l = &ls->l[li];
            if (l->type == LT_UNDIRECTED) { for (int i = 0; i < 3; i++) c[i] += l->col[i]; continue; }
            float L[3], d;
            if (l->type == LT_DIR) { memcpy(L, l->L, sizeof L); d = l->dlen; }
            else { for (int i = 0; i < 3; i++) L[i] = l->pos[i] - v->pos[i];
                   float l2 = L[0] * L[0] + L[1] * L[1] + L[2] * L[2]; d = sqrtf(l2); if (d > 1e-12f) { L[0] /= d; L[1] /= d; L[2] /= d; } }
            float att = 1.0f;
            switch (l->att_type) {
            case 0: att = clamp01(l->att0 * (d + l->att1)); break;
            case 1: att = clamp01(l->att0 * (d * d + l->att1)); break;
            case 2: att = clamp01(l->att0 * (1.0f / (d > 1e-12f ? d : 1e-12f) + l->att1)); break;
            case 3: att = clamp01(l->att0 * (1.0f / (d * d > 1e-24f ? d * d : 1e-24f) + l->att1)); break;
            }
            if (l->type == LT_SPOT) att *= clamp01((L[0] * l->axis[0] + L[1] * l->axis[1] + L[2] * l->axis[2] - l->cos_outer) * l->cone_scale);
            if (att <= 0) continue;
            float ndl = n[0] * L[0] + n[1] * L[1] + n[2] * L[2];
            float kd = att * ((ndl > 0 ? ndl : 0) + l->amb);
            for (int i = 0; i < 3; i++) c[i] += kd * l->col[i];
            if (want_spec && ndl > 0) {
                float dn = 2 * ndl, R[3] = { dn * n[0] - L[0], dn * n[1] - L[1], dn * n[2] - L[2] };
                float rdv = R[0] * V[0] + R[1] * V[1] + R[2] * V[2];
                if (rdv > 0) { float ks = att * powf(rdv, sexp); for (int i = 0; i < 3; i++) sp[i] += ks * l->col[i]; }
            }
        }
        for (int i = 0; i < 3; i++) { o->light[i] = c[i]; o->spec[i] = sp[i] * m->specular[i] / 64.0f; }
    }
    o->fog[0] = o->fog[1] = o->fog[2] = o->fog[3] = 0;
    static int nofog = -1; if (nofog < 0) nofog = getenv("PH_NOFOG") != 0;
    if (!nofog && vp->fog_on && ((m->flags881 >> 2) & 1)) {
        /* 621 params as built by the game (@0C014A96): P = 1/(end-start), Q = near/end  ->  f = 1 - P*(end - z) */
        float end = vp->fog_bias > 0 ? vp->n / vp->fog_bias : 1e30f;
        float f = clamp01(1.0f - vp->fog_density * (end - (-v->pos[2])));
        o->fog[0] = vp->fog_color[0]; o->fog[1] = vp->fog_color[1]; o->fog[2] = vp->fog_color[2]; o->fog[3] = (uint8_t)(f * 255.0f + 0.5f);
    }
}

typedef struct { float pos[3], nrm[3], fn[3], fogp[3], alpha; uint32_t vamb[3]; int ls; uint32_t flags; uint8_t mat[6], fog_on, flat; } VCacheKey;
static VCacheKey vc_key[4]; static uint8_t vc_col[4][4], vc_fog[4][4]; static int vc_valid[4], vc_next;

static int pending_clear;
static void add_range(GfxFrame *f, int kind, int first, int count, const CpViewport *v) {
    if (!count) return;
    int16_t c[4] = { (int16_t)(v->l < 0 ? 0 : v->l > 640 ? 640 : v->l), (int16_t)(v->b < 0 ? 0 : v->b > 480 ? 480 : v->b),
                     (int16_t)(v->r < 0 ? 0 : v->r > 640 ? 640 : v->r), (int16_t)(v->t < 0 ? 0 : v->t > 480 ? 480 : v->t) };
    if (c[2] <= c[0] || c[3] <= c[1]) { c[0] = 0; c[1] = 0; c[2] = 640; c[3] = 480; }   /* no 221 clip: whole screen */
    GfxRange *p = f->nr ? &f->r[f->nr - 1] : 0;
    if (p && !pending_clear && p->kind == kind && p->first + p->count == first && !memcmp(p->clip, c, sizeof c)) { p->count += count; return; }
    if (f->nr >= f->rcap) { f->rcap = f->rcap ? f->rcap * 2 : 64; f->r = realloc(f->r, f->rcap * sizeof(GfxRange)); }
    GfxRange *g = &f->r[f->nr++];
    g->first = first; g->count = count; g->kind = kind; memcpy(g->clip, c, sizeof c); g->clear_depth = (uint8_t)pending_clear;
    pending_clear = 0;
}

void gfx_build(GfxFrame *f, const CpState *s) {
    f->nr = 0;
    memset(vc_valid, 0, sizeof vc_valid);
    f->nv = 0; f->n_opaque = 0;
    f->blend_rate = (s->fb181 >> 24) & 1 ? (int)((s->fb181 >> 16) & 0xff) : 0;
    f->clear[0] = f->clear[1] = f->clear[2] = 0; f->clear[3] = 1;
    if (s->has_clear) {
        f->clear[0] = (s->clear_color & 255) / 255.0f;
        f->clear[1] = ((s->clear_color >> 8) & 255) / 255.0f;
        f->clear[2] = ((s->clear_color >> 16) & 255) / 255.0f;
    }
    /* pass 0: opaque, 1: translucent (both depth-tested), 2: viewports whose 421 depth function is 2 or 6
     * (menu titles at w~5, race gauges at the far plane): drawn last, in order, without depth test */
    { static int dbg = -1; if (dbg < 0) dbg = getenv("BLENDSTATS") != 0;
      if (dbg) { int cnt[4][16] = {{0}}; for (int i = 0; i < s->ntris; i++) cnt[s->tris[i].mat.blend & 3][s->tris[i].polytype & 15]++;
        if (g_gpu.frame % 20 == 0) { fprintf(stderr, "F%llu", (unsigned long long)g_gpu.frame);
          for (int b = 0; b < 4; b++) for (int p = 0; p < 16; p++) if (cnt[b][p]) fprintf(stderr, " b%d/t%x:%d", b, p, cnt[b][p]); fprintf(stderr, "\n"); } } }
    { static long vf = -2; if (vf == -2) vf = getenv("VPSTATS") ? atol(getenv("VPSTATS")) : -1;
      if (vf >= 0 && ((long)g_gpu.frame == vf || (getenv("VPSTATS2") && (long)g_gpu.frame % 500 == 0))) {
        int last = -99, run = 0; float ll = 0, lr = 0, lb = 0, lt = 0;
        for (int i = 0; i <= s->ntris; i++) { const CpTri *t = i < s->ntris ? &s->tris[i] : 0;
          int same = t && t->view.dfunc == last && t->view.l == ll && t->view.r == lr && t->view.b == lb && t->view.t == lt;
          if (!same) { if (run) fprintf(stderr, "VP f%lld df=%d clip l=%g r=%g b=%g t=%g n=%d\n", (long long)g_gpu.frame, last, ll, lr, lb, lt, run);
            if (t) { last = t->view.dfunc; ll = t->view.l; lr = t->view.r; lb = t->view.b; lt = t->view.t; run = 0; } }
          run++; } } }
    /* 421 F = viewport number: lower numbers first, each over what is there with a depth buffer of its own (Odin);
     * inside one: opaque, then translucent in order. Each run is clipped to its viewport (221): the HOW TO CONTROL
     * screen draws its frame as F=3, the lever picture as F=4 and the race inside the frame box as F=7. */
    unsigned groups = 0; for (int i = 0; i < s->ntris; i++) groups |= 1u << (s->tris[i].view.dfunc & 7);
    int first_group = 1; pending_clear = 0;
    for (int grp = 0; grp < 8; grp++) {
      if (!(groups & (1u << grp))) continue;
      if (!first_group) pending_clear = 1;
      first_group = 0;
      for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < s->ntris; i++) {
            const CpTri *t = &s->tris[i];
            int add = t->mat.blend == 2;                         /* A81 mode 2: additive (glows, flares) */
            /* Polygon types (word >> 8): 3 opaque, 5 / 7 shadows (drawn as opaque), 9 punch-through, B background and
               D translucent (both blend with the mode of their material and leave the depth alone). */
            int blended = t->polytype == 0xd || t->polytype == 0xb, shadow = t->polytype == 5 || t->polytype == 7;
            int translucent = blended || add;
            if ((t->view.dfunc & 7) != grp || translucent != pass) continue;
            if (gfx_tri_culled(t)) continue;
            { static int sk = -2; if (sk == -2) { const char *e = getenv("PH_SKIPPASS"); sk = e ? atoi(e) : -1; } if (sk == pass) continue; }
            { static float nc = -2; if (nc == -2) { const char *e = getenv("PH_NEARCULL"); nc = e ? (float)atof(e) : -1; }
              if (nc >= 0 && (-t->v[0].pos[2] < nc || -t->v[1].pos[2] < nc || -t->v[2].pos[2] < nc)) continue; }
            add_range(f, pass == 0 ? GFX_OPAQUE : (add ? GFX_ADD : GFX_BLEND), f->nv, 3, &t->view);
            float a[3], b[3], n[3];
            for (int k = 0; k < 3; k++) { a[k] = t->v[1].pos[k] - t->v[0].pos[k]; b[k] = t->v[2].pos[k] - t->v[0].pos[k]; }
            n[0] = a[1] * b[2] - a[2] * b[1]; n[1] = a[2] * b[0] - a[0] * b[2]; n[2] = a[0] * b[1] - a[1] * b[0];
            if (n[2] < 0) { n[0] = -n[0]; n[1] = -n[1]; n[2] = -n[2]; }    /* face normal towards the viewer (fallback only) */
            int textured = (t->mat.flags881 >> 3) & 1;
            GfxShade sh[3];
            for (int k = 0; k < 3; k++) gfx_vertex_shade(s, t, k, n, &sh[k]);
            const CpTexhead *th = &t->tex;
            uint32_t texw = (uint32_t)th->fmt | ((uint32_t)th->bank << 4) |
                            ((uint32_t)((th->wrap >> 1) & 1) << 8) | ((uint32_t)(th->wrap & 1) << 9) |
                            ((uint32_t)((th->repeat >> 1) & 1) << 10) | ((uint32_t)(th->repeat & 1) << 11) |
                            ((uint32_t)textured << 16) | ((uint32_t)(t->polytype != 3 && !shadow) << 17) | ((uint32_t)translucent << 18) |
                            ((uint32_t)(t->pattern & 0x1f) << 20);
            reserve(f, 3);
            for (int k = 0; k < 3; k++) make_vertex(&f->v[f->nv++], t, &t->v[k], &sh[k], texw);
        }
      }
    }
    f->n_opaque = f->n_depth = f->nv;
}
