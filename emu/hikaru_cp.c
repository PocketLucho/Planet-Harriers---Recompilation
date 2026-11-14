#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "hikaru_cp.h"

/* 16-bit bases and indices (the stages of Planet Harriers set bases well above 0x4000) */
#define NUM_MAT 0x20000
#define NUM_TEX 0x20000

static float f32(uint32_t v) { float f; memcpy(&f, &v, 4); return f; }

void cp_init(CpState *s) {
    memset(s, 0, sizeof *s);
    s->mat_table = calloc(NUM_MAT, sizeof(CpMaterial));
    s->tex_table = calloc(NUM_TEX, sizeof(CpTexhead));
    s->precision = 1.0f;
    s->lit_table = calloc(1024, sizeof(CpLight)); s->ls_table = calloc(256, sizeof(CpLightset));
    s->ls_scratch.mask = 0xf; s->cur_ls = -1;
    for (int k = 0; k < 32; k++) for (int i = 0; i < 4; i++) s->mv[k][i][i] = 1.0f;
    for (int i = 0; i < 4; i++) s->local[i][i] = 1.0f;
}

static const uint32_t *fetch(CpState *s, uint32_t pc) {
    switch (pc >> 24) {
    case 0x40: case 0x41:
        if (!s->slave_ram) return 0;
        return (const uint32_t *)(s->slave_ram + (pc & 0x01fffffc));
    case 0x48: case 0x4c:
        return (const uint32_t *)(s->cmdram + (pc & 0x003ffffc));
    }
    return 0;
}

static uint32_t insn_size(uint32_t w) { return 1u << (((w >> 4) & 3) + 2); }

static void xform_m(float (*m)[4], const float in[3], float out[3], int is_normal) {
    for (int r = 0; r < 3; r++)
        out[r] = m[0][r] * in[0] + m[1][r] * in[1] + m[2][r] * in[2] + (is_normal ? 0 : m[3][r]);
}

/* modelview of slot `slot` with the local matrix (if any) applied first */
static void eff_mv(CpState *s, int slot, float out[4][4]) {
    float (*m)[4] = s->mv[slot];
    if (!s->has_local) { memcpy(out, m, sizeof(float) * 16); return; }
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 3; j++)
            out[i][j] = m[0][j] * s->local[i][0] + m[1][j] * s->local[i][1] + m[2][j] * s->local[i][2] + (i == 3 ? m[3][j] : 0.0f);
        out[i][3] = i == 3;
    }
}
/* Level of detail (semantics as documented in Odin, antarctic_ops.cpp): width of the model's bounding sphere on
 * the screen in 1/16 pixel = diameter * projection distance / distance of its centre. */
static float lod_size(const CpState *s) {
    float d = s->lod_dist; if (d < 1e-6f) d = 1e-6f;
    return 32.0f * s->lod_radius * fabsf(s->vp_scratch.f) / d;
}
/* does the sphere touch the view volume of the current viewport? (planes through the eye and the window edges) */
static int lod_sphere_visible(const CpState *s) {
    const CpViewport *v = &s->vp_scratch;
    float f = v->f, g = v->f2, r = s->lod_radius;
    float x = s->lod_center[0], y = s->lod_center[1], z = -s->lod_center[2];
    float wl = v->l - v->off_x, wr = v->r - v->off_x, wu = v->off_y - v->b, wd = v->off_y - v->t;   /* window on the projection plane, y up */
    if (z + r < v->n) return 0;
    if (x * f - z * wl < -r * hypotf(f, wl) || z * wr - x * f < -r * hypotf(f, wr)) return 0;
    if (y * g - z * wd < -r * hypotf(g, wd) || z * wu - y * g < -r * hypotf(g, wu)) return 0;
    return 1;
}
static int jump_cond(const CpState *s, uint32_t w) {
    uint32_t id = w >> 16;
    switch ((w >> 12) & 0xf) {
    case 0x0: return 1;
    case 0x1: return s->lod_branch != id;
    case 0x5: return lod_size(s) >= (float)id;
    case 0x6: return !s->lod_cond;
    case 0x7: return s->lod_cond != 0;
    case 0x9: return lod_size(s) < (float)id;
    case 0xd: return s->lod_compared ? s->lod_branch == id : lod_size(s) >= (float)id;
    }
    return 0;
}
static int call_cond(const CpState *s, uint32_t w) {
    switch ((w >> 12) & 0xf) { case 0x0: return 1; case 0x4: return !s->lod_cond; case 0x8: return s->lod_cond != 0; }
    return 0;
}

/* one triangle per instanced modelview (161 with P=1 pushes extra matrices) */
static void push_lightset(CpState *s) {
    if (s->nls >= s->capls) { s->capls = s->capls ? s->capls * 2 : 256; s->ls_frame = realloc(s->ls_frame, s->capls * sizeof(CpLightset)); }
    s->ls_frame[s->nls] = s->ls_scratch; s->cur_ls = s->nls++;
}

static void emit_tri(CpState *s) {
    if (s->nverts < 3) return;
    uint32_t info = s->tmp_info;
    int order[3] = { 0, 1, 2 };
    if (info & 1) { order[1] = 2; order[2] = 1; }        /* winding */
    static int swap_sel = -1; if (swap_sel < 0) swap_sel = getenv("PH_SKINSWAP") != 0;
    for (int k = 0; k < 1; k++) {
        if (s->ntris + 2 > s->cap) { s->cap = s->cap ? s->cap * 2 : 4096; s->tris = realloc(s->tris, s->cap * sizeof(CpTri)); }
        CpTri *t = &s->tris[s->ntris++];
        float em[2][4][4]; eff_mv(s, 0, em[0]); eff_mv(s, 1, em[1]);
        for (int i = 0; i < 3; i++) {
            t->v[i] = s->tmp[order[i]];
            int slot = s->tmp[order[i]].mtx ^ swap_sel; { static int f0 = -1; if (f0 < 0) f0 = getenv("PH_SLOT0") != 0; if (f0) slot = 0; }
            xform_m(em[slot], s->tmp[order[i]].pos, t->v[i].pos, 0);
            xform_m(em[slot], s->tmp[order[i]].nrm, t->v[i].nrm, 1);
        }
        t->view = s->vp_scratch; t->mat = s->mat_scratch; t->tex = s->tex_scratch;
        t->polytype = s->polytype; t->polyalpha = s->polyalpha; t->pattern = s->pattern; t->info = info; t->src_pc = s->pc; t->ls = s->cur_ls;
    }
    s->last_tri_count = 1;
}

static void push_pos(CpState *s, uint32_t info, const float p[3], const float *nrm, const float *uv) {
    if (!(info & 2)) s->tmp[0] = s->tmp[1];      /* p = pivot: keep vertex -2 */
    s->tmp[1] = s->tmp[2];
    memset(&s->tmp[2], 0, sizeof s->tmp[2]);
    memcpy(s->tmp[2].pos, p, sizeof(float) * 3);
    if (nrm) memcpy(s->tmp[2].nrm, nrm, sizeof(float) * 3);
    if (uv) { s->tmp[2].uv[0] = uv[0]; s->tmp[2].uv[1] = uv[1]; }
    s->tmp[2].alpha = info >> 24;
    s->tmp[2].mtx = (info >> 12) & 1;
    s->nverts++;
    if (((info >> 9) & 7) == 7) { s->tmp_info = info; emit_tri(s); }
}

#define LOG(...) do { if (s->log) fprintf(s->log, __VA_ARGS__); } while (0)

void cp_frame(CpState *s, uint32_t start_pc, long max_steps) {
    s->pc = start_pc; s->sp = 0; s->pattern = 0; s->running = 1; s->ntris = 0; s->steps = 0;
    s->gen++; s->nls = 0; push_lightset(s);
    s->has_clear = 0; s->zero_run = 0; s->vp_depth = 0; s->mv_depth = 0; s->mv_total = 1; s->last_tri_count = 0; s->mat_base = 0; s->tex_base = 0; s->nverts = 0;
    while (s->running && s->steps < max_steps) {
        const uint32_t *in = fetch(s, s->pc);
        if (!in) { LOG("CP %08x: bad pc\n", s->pc); break; }
        uint32_t w = in[0], op = w & 0x1ff, sub = (w >> 8) & 0xf, sz = insn_size(w);
        uint32_t next = s->pc + sz;
        s->opcount[op]++; s->steps++;
        if (w) s->zero_run = 0;
        if (s->log) {
            LOG("%08x:", s->pc);
            for (uint32_t i = 0; i < sz / 4; i++) LOG(" %08x", in[i]);
            LOG("\n");
        }
        switch (op) {
        case 0x000:                                                /* nop; a long run of zeros = end of program */
            if (w == 0 && ++s->zero_run > 32) { LOG("CP %08x: end (zero run)\n", s->pc); s->running = 0; }
            break;
        case 0x012: case 0x052: {                                  /* jump / call, conditional on the level of detail */
            uint32_t a = in[1] * 4; if (w & 0x800) a += s->pc;
            if (op == 0x012 ? jump_cond(s, w) : call_cond(s, w)) {
                if (op == 0x052) { if (s->sp < 64) { s->pat_stack[s->sp] = s->pattern; s->stack[s->sp++] = s->pc; } }
                else if (a == s->pc) { s->running = 0; break; }    /* a jump to itself parks the processor */
                next = a;
            }
            break; }
        case 0x082:                                                /* ret */
            if (call_cond(s, w)) { if (s->sp > 0) { --s->sp; next = s->stack[s->sp] + 8; s->pattern = s->pat_stack[s->sp]; } else s->running = 0; }
            break;
        case 0x1c2: s->running = 0; break;                         /* kill */
        case 0x005: { uint32_t t = w & 0xfffff000; s->lod_radius = f32(t); s->lod_cond = lod_sphere_visible(s); break; }   /* test the bounding sphere */
        case 0x055: s->lod_radius = f32(in[1]); s->lod_compared = 0; break;                                               /* radius of the bounding sphere */
        case 0x095: {                                              /* compare the size on the screen with two thresholds */
            float size = lod_size(s); uint32_t hi = in[1] >> 16, lo = in[1] & 0xffff; int form = (w >> 12) & 0xf;
            s->lod_compared = 1;
            if (form == 4) { s->lod_cond = size >= (float)lo; s->lod_branch = size >= (float)hi ? lo : hi; }
            else if (form == 8) { s->lod_cond = size >= (float)hi; s->lod_branch = size >= (float)lo ? hi : lo; }
            break; }
        case 0x021: {
            CpViewport *v = &s->vp_scratch;
            switch ((w >> 8) & 7) {
            case 0: v->f = f32(in[1]); v->f2 = f32(in[2]); v->n = f32(in[3]); break;
            case 2: v->off_x = (float)(in[1] & 0xffff); v->off_y = (float)(in[1] >> 16);
                    v->l = (float)(((int32_t)(int16_t)in[2] << 3) >> 3); v->r = (float)(((int32_t)(int16_t)in[3] << 3) >> 3);
                    v->b = (float)(((int32_t)(int16_t)(in[2] >> 16) << 3) >> 3); v->t = (float)(((int32_t)(int16_t)(in[3] >> 16) << 3) >> 3); break;
            case 4: v->dmin = f32(in[1]); v->dmax = f32(in[2]); v->dfunc = in[3] >> 29; break;
            case 6: v->fog_on = ((w >> 17) & 1) ^ 1; v->fog_color[0] = in[1]; v->fog_color[1] = in[1] >> 8; v->fog_color[2] = in[1] >> 16;
                    v->fog_density = f32(in[2]); v->fog_bias = f32(in[3]); break;
            default: break;
            }
            break; }
        case 0x011: s->vp_scratch.ambient[0] = w >> 16; s->vp_scratch.ambient[1] = in[1] & 0xffff; s->vp_scratch.ambient[2] = in[1] >> 16; break;
        case 0x191: s->vp_scratch.clear = in[1]; s->clear_color = in[1]; s->has_clear = 1; break;
        case 0x004: s->vp_table[(w >> 16) & 7] = s->vp_scratch; break;
        case 0x003:
            switch ((w >> 12) & 0xf) {
            case 0: s->vp_scratch = s->vp_table[(w >> 16) & 7]; break;
            case 2: if (s->vp_depth < 32) s->vp_stack[s->vp_depth++] = s->vp_scratch; s->vp_scratch = s->vp_table[(w >> 16) & 7]; break;
            case 4: if (s->vp_depth > 0) s->vp_scratch = s->vp_stack[--s->vp_depth]; break;
            }
            break;
        case 0x161:
            if (sub == 1 && (w & 0x0000b000) == 0x00002000) {     /* a row of the local matrix; bit 14 marks its last row */
                int row = (w >> 16) & 3;
                s->local[row][0] = f32(in[1]); s->local[row][1] = f32(in[2]); s->local[row][2] = f32(in[3]); s->local[row][3] = row == 3;
                if (w & 0x00004000) s->has_local = 1;
            } else if (sub == 1 && !(w & 0x0000f000)) {
                /* A row of the modelview (bit 18: of the second modelview, which the vertices with header bit 12
                 * follow: skinned joints). A row alone leaves the others as they are: Planet Harriers sends rows
                 * 0-3, then only row 3 for the next characters of a text. A model without a second modelview uses
                 * the first for all its vertices. */
                int second = (w >> 18) & 1, row = (w >> 16) & 3;
                float (*m)[4] = s->mv[second];
                m[row][0] = f32(in[1]); m[row][1] = f32(in[2]); m[row][2] = f32(in[3]); m[row][3] = row == 3;
                if (!second) { memcpy(s->mv[1], s->mv[0], sizeof s->mv[0]); s->has_local = 0; }
            } else if (sub == 5) {                                 /* 561: centre of the bounding sphere, in model space */
                float em[4][4], p[3] = { f32(in[1]), f32(in[2]), f32(in[3]) }, e[3];
                eff_mv(s, 0, em); xform_m(em, p, e, 0);
                memcpy(s->lod_center, e, sizeof e); s->lod_dist = sqrtf(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
            } else if (sub == 9 || sub == 0xb) {                   /* light vectors (eye space) */
                CpLight *l = &s->lit_scratch; float v[3] = { f32(in[1]), f32(in[2]), f32(in[3]) };
                switch (w & 0x000ff000) {
                case 0x00008000: memcpy(l->dir, v, sizeof v); l->has_dir = 1; break;
                case 0x00010000: memcpy(l->pos, v, sizeof v); l->has_pos = 1; break;
                case 0x00016000: memset(l->pos, 0, sizeof l->pos); l->has_pos = 1; break;
                }
            }
            break;
        case 0x051: {
            CpLight *l = &s->lit_scratch;
            if (sub == 0) { l->diffuse[0] = in[1] & 0x3ff; l->diffuse[1] = (in[1] >> 10) & 0x3ff; l->diffuse[2] = (in[1] >> 20) & 0x3ff; }
            else if (sub == 4) { l->has_spec = ((w >> 24) & 1) ^ 1; l->specular[0] = in[1]; l->specular[1] = in[1] >> 8; l->specular[2] = in[1] >> 16; }
            break; }
        case 0x061: { CpLight *l = &s->lit_scratch;
            l->att_type = (w >> 16) & 3; l->att[0] = f32(in[1]); l->att[1] = f32(in[2]); l->has_pos = l->has_dir = 0; break; }
        case 0x104: s->lit_table[(w >> 16) & 0x3ff] = s->lit_scratch; break;
        case 0x064: { uint32_t i = s->ls_base + ((w >> 16) & 0xff); if (i < 256) { CpLightset *ls = &s->ls_table[i];
            ls->l[0] = s->lit_table[in[1] & 0x3ff]; ls->l[1] = s->lit_table[(in[1] >> 16) & 0x3ff];
            ls->l[2] = s->lit_table[in[2] & 0x3ff]; ls->l[3] = s->lit_table[(in[2] >> 16) & 0x3ff]; }
            break; }
        case 0x043: { uint32_t i = (w >> 16) & 0xff;
            if (!(w & 0x1000)) s->ls_base = i;
            else { i += s->ls_base; if (i < 256) { s->ls_scratch = s->ls_table[i]; s->ls_scratch.mask = (w >> 24) & 0xf; push_lightset(s); } }
            break; }
        case 0x091:
            switch (sub) {
            case 0: s->mat_scratch.diffuse[0] = in[1]; s->mat_scratch.diffuse[1] = in[1] >> 8; s->mat_scratch.diffuse[2] = in[1] >> 16; break;
            case 2: s->mat_scratch.ambient[0] = in[1]; s->mat_scratch.ambient[1] = in[1] >> 8; s->mat_scratch.ambient[2] = in[1] >> 16; break;
            case 4: for (int i = 0; i < 4; i++) s->mat_scratch.specular[i] = in[1] >> (i * 8); break;
            }
            break;
        case 0x081:
            if (sub == 8) s->mat_scratch.flags881 = w >> 16;
            else if (sub == 0xa) s->mat_scratch.blend = (w >> 16) & 3;
            else if (sub == 0xc) s->mat_scratch.alpha_test = (w >> 16) & 0x3f;
            break;
        case 0x084: { uint32_t i = s->mat_base + (w >> 16); if (i < NUM_MAT) s->mat_table[i] = s->mat_scratch; break; }
        case 0x083: { uint32_t i = w >> 16;
            if (!(w & 0x1000)) s->mat_base = i; else if (i + s->mat_base < NUM_MAT) s->mat_scratch = s->mat_table[i + s->mat_base];
            break; }
        case 0x0c1:
            if (sub == 0) s->tex_scratch.w0c1 = w;
            else if (sub == 2) { CpTexhead *t = &s->tex_scratch; t->w2c1 = w;
                t->logw = (w >> 16) & 7; t->logh = (w >> 19) & 7; t->wrap = (w >> 22) & 3; t->repeat = (w >> 24) & 3; t->fmt = (w >> 26) & 7; }
            else if (sub == 4) { CpTexhead *t = &s->tex_scratch; t->w4c1 = w;
                t->bank = (w >> 12) & 1; t->slotx = (w >> 16) & 0xff; t->sloty = w >> 24; }
            break;
        case 0x0c4: { uint32_t i = s->tex_base + (w >> 16);
 if (i < NUM_TEX) s->tex_table[i] = s->tex_scratch; break; }
        case 0x0c3: { uint32_t i = w >> 16;
            if (!(w & 0x1000)) s->tex_base = i; else if (i + s->tex_base < NUM_TEX) s->tex_scratch = s->tex_table[i + s->tex_base];
            break; }
        case 0x101:
            /* 501 Set Pattern (per Odin's notes): low 4 bits = 0 every pixel .. 8 none, 8+n = the pixels n leaves
               out; bit 4 mirrors left-right. SW Racer sends -7..-1 (16 bits) to fade far scenery in. A subroutine
               returns with the pattern it was called with. */
            if (sub == 5) s->pattern = (w >> 16) & 0x1f;
            if (sub == 9) { int lg = (w >> 16) & 0xff; s->precision = ldexpf(1.0f, lg + 2 - 0x8f); }   /* 2^(lg-0x8d); lg can exceed 0x8d (sky dome: x2) */
            break;
        case 0x103: case 0x113: s->polytype = (w >> 8) & 0xf; s->polyalpha = (w >> 24) / 255.0f; break;
        case 0x12c: case 0x12d: case 0x12e: case 0x12f: {         /* push position, static (fixed point) + normal */
            float p[3], n[3];
            for (int i = 0; i < 3; i++) {
                p[i] = (int16_t)(in[1 + i] >> 16) * s->precision;
                n[i] = (int16_t)((in[1 + i] & 0x3ff) << 6) / 16384.0f;
            }
            push_pos(s, w, p, n, 0);
            break; }
        case 0x1ac: case 0x1ad: case 0x1ae: case 0x1af: {         /* push position, dynamic (float) */
            float p[3] = { f32(in[1]), f32(in[2]), f32(in[3]) };
            push_pos(s, w, p, 0, 0);
            break; }
        case 0x1b8: case 0x1b9: case 0x1ba: case 0x1bb: case 0x1bc: case 0x1bd: case 0x1be: case 0x1bf: {
            float p[3] = { f32(in[1]), f32(in[2]), f32(in[3]) }, n[3] = { f32(in[5]), f32(in[6]), f32(in[7]) };
            float uv[2] = { (int16_t)in[4] / 16.0f, (int16_t)(in[4] >> 16) / 16.0f };
            push_pos(s, w, p, n, uv);
            break; }
        case 0x0e8: case 0x0e9:                                    /* texcoords for the last three vertices */
            if (s->nverts >= 3) for (int i = 0; i < 3; i++) {
                s->tmp[2 - i].uv[0] = (int16_t)in[1 + i] / 16.0f; s->tmp[2 - i].uv[1] = (int16_t)(in[1 + i] >> 16) / 16.0f; }
            if (((w >> 9) & 7) == 7) { s->tmp_info = w; emit_tri(s); }
            break;
        case 0x158: case 0x159: case 0x15a: case 0x15b:            /* texcoords for the last vertex; T=7 closes the triangle */
            s->tmp[2].uv[0] = (int16_t)in[1] / 16.0f; s->tmp[2].uv[1] = (int16_t)(in[1] >> 16) / 16.0f;
            if (((w >> 9) & 7) == 7) { s->tmp_info = w; emit_tri(s); }
            break;
        case 0x006: case 0x046:                                    /* lights: unknown */
        case 0x181:                                                /* 181 / 781: framebuffer configuration */
            if ((w & 0xfff) == 0x181) s->fb181 = w; else if ((w & 0xfff) == 0x781) s->fb781 = w;
            break;
        case 0x0d1: case 0x154: case 0x194: case 0x088: case 0x1a1:
            break;
        default:
            s->unknown_ops++;
            if (s->log) LOG("   ^ unknown opcode %03x\n", op);
            break;
        }
        s->pc = next;
    }
}
