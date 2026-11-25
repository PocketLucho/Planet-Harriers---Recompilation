/* Debug software rasterizer for Hikaru CP output (flat/textured, z-buffered). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifdef _WIN32
#include "miniz.h"       /* zlib-compatible compress/crc32 */
#else
#include <zlib.h>
#endif
#include "hikaru_cp.h"
#include "swrender.h"
#include "hikaru_gfx.h"

#define W 640
#define H 480
static uint8_t fb[H][W][3]; static float zb[H][W];
int sr_proj_mode = 2; float sr_scale_k = 1.0f;
uint8_t *sr_texram;       /* 2 banks x 4 MB */
int sr_no_tex;

/* texel fetch: TEXRAM bank as 2048x1024 16-bit sheet, linear 16-bit texels */
static uint16_t tex16(int bank, int x, int y) {
    x &= 2047; y &= 1023;
    uint32_t o = (uint32_t)y * 4096 + x * 2;
    const uint8_t *b = sr_texram + bank * 0x400000;
    return b[o] | (b[o + 1] << 8);
}
static int wrapc(int c, int n, int wrap, int mirror) {
    if (!wrap) return c < 0 ? 0 : c >= n ? n - 1 : c;
    if (mirror) { int p = c / n; c %= n; if (c < 0) { c += n; p--; } return (p & 1) ? n - 1 - c : c; }
    c %= n; return c < 0 ? c + n : c;
}
/* returns 0xAARRGGBB */
static uint32_t sample(const CpTexhead *t, float u, float v) {
    if (t->w2c1 & 0x4000) { u *= 16; v *= 16; }   /* texcoords in whole texels (see hikaru_gfx.c) */
    int w = 16 << t->logw, h = 16 << t->logh;
    int x = wrapc((int)floorf(u), w, (t->wrap >> 1) & 1, (t->repeat >> 1) & 1);
    int y = wrapc((int)floorf(v), h, t->wrap & 1, t->repeat & 1);
    int bx = (t->slotx - 0x80) * 16, by = (t->sloty - 0xc0) * 16;
    if (t->slotx < 0x80 || t->sloty < 0xc0) { bx = by = 0; }
    uint32_t r, g, b, a;
    switch (t->fmt) {
    case 0: { uint16_t p = tex16(t->bank, bx + x, by + y);
        r = (p & 31) << 3; g = ((p >> 5) & 31) << 3; b = ((p >> 10) & 31) << 3; a = (p >> 15) ? 255 : 0; break; }
    case 1: { uint16_t p = tex16(t->bank, bx + x, by + y);
        r = (p & 15) * 17; g = ((p >> 4) & 15) * 17; b = ((p >> 8) & 15) * 17; a = (p >> 12) * 17; break; }
    case 4: { uint16_t p = tex16(t->bank, bx + x, by + y);
        r = g = b = p & 255; a = p >> 8; break; }
    case 2: default: { /* I4: one 16-bit word per 2x2 block of 4-bit texels, the value being the intensity of the four
                          channels; logical size 2w x 2h, words start at column bx/2 (same as the Vulkan shader) */
        int qx = wrapc((int)floorf(u), 2 * w, (t->wrap >> 1) & 1, (t->repeat >> 1) & 1), qy = wrapc((int)floorf(v), 2 * h, t->wrap & 1, t->repeat & 1);
        uint16_t p = tex16(t->bank, bx / 2 + qx / 2, by + qy / 2);
        uint32_t n = (p >> (12 - 4 * ((qx & 1) + 2 * (qy & 1)))) & 15;
        r = g = b = a = n * 17; break; }
    }
    return (a << 24) | (r << 16) | (g << 8) | b;
}

void sr_png(const char *path) {
    size_t rl = W * 3 + 1; uint8_t *raw = malloc(rl * H);
    for (int y = 0; y < H; y++) { raw[y * rl] = 0; memcpy(raw + y * rl + 1, fb[y], W * 3); }
    uLongf cl = compressBound(rl * H); uint8_t *cz = malloc(cl); compress(cz, &cl, raw, rl * H);
    FILE *f = fopen(path, "wb");
    static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 }; fwrite(sig, 1, 8, f);
    #define CH(t, d, n) do { uint8_t hd[8]; uint32_t L = (n); hd[0]=L>>24;hd[1]=L>>16;hd[2]=L>>8;hd[3]=L; memcpy(hd+4,t,4); fwrite(hd,1,8,f); fwrite(d,1,L,f); \
        uint32_t c = crc32(0, (const Bytef*)t, 4); c = crc32(c, (const Bytef*)(d), L); uint8_t cc[4]={c>>24,c>>16,c>>8,c}; fwrite(cc,1,4,f);} while(0)
    uint8_t ih[13] = { 0,0,W>>8,W&255, 0,0,H>>8,H&255, 8, 2, 0, 0, 0 };
    CH("IHDR", ih, 13); CH("IDAT", cz, cl); CH("IEND", "", 0);
    fclose(f); free(raw); free(cz);
}

/* eye -> screen. Hypotheses selectable with -proj */
static int project(const CpTri *t, const float *p, float *sx, float *sy, float *sz) {
    const CpViewport *v = &t->view;
    float x = p[0], y = p[1], z = p[2];
    float foc = v->f * sr_scale_k;
    if (sr_proj_mode == 0) { if (z <= 0.001f) return 0; *sx = v->off_x + x * foc / z; *sy = v->off_y - y * foc / z; *sz = z; }
    else if (sr_proj_mode == 1) { if (z <= 0.001f) return 0; *sx = v->off_x + x * foc / z; *sy = v->off_y + y * foc / z; *sz = z; }
    else { if (-z <= 0.001f) return 0; *sx = v->off_x + x * foc / -z; *sy = v->off_y - y * foc / -z; *sz = -z; }
    return 1;
}

/* ---- rasterizer: mirrors what the Vulkan path draws (hikaru_gfx.c + shaders/hikaru.frag), so that the headless
 * frame dumps can be compared with footage of the cabinet: near-plane clipping, per-vertex colour and fog, texel
 * alpha test, alpha / additive blending, viewport groups with a depth buffer each, viewport clip rectangle. ---- */
typedef struct { float x, y, w, uv[2], light[4], spec[3], base[3], col1[3], fog[4]; } SV;
static uint8_t cov[H][W];          /* pixels the 3D picture has written (the 2D layers behind it show elsewhere) */
enum { SR_OPAQUE, SR_BLEND, SR_ADD };

static void raster(const CpTri *t, const SV *v0, const SV *v1, const SV *v2, int textured, int atest, int translucent, int mode,
                   int cx0, int cy0, int cx1, int cy1, uint32_t modes) {
    float x0 = v0->x, y0 = v0->y, x1 = v1->x, y1 = v1->y, x2 = v2->x, y2 = v2->y;
    float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if (fabsf(area) < 1e-6f) return;
    float fx0 = fminf(x0, fminf(x1, x2)), fx1 = fmaxf(x0, fmaxf(x1, x2)), fy0 = fminf(y0, fminf(y1, y2)), fy1 = fmaxf(y0, fmaxf(y1, y2));
    if (fx1 < cx0 || fx0 > cx1 || fy1 < cy0 || fy0 > cy1) return;
    int minx = (int)fmaxf((float)cx0, floorf(fx0)), maxx = (int)fminf((float)(cx1 - 1), ceilf(fx1));
    int miny = (int)fmaxf((float)cy0, floorf(fy0)), maxy = (int)fminf((float)(cy1 - 1), ceilf(fy1));
    const SV *vv[3] = { v0, v1, v2 };
    float iw[3] = { 1 / v0->w, 1 / v1->w, 1 / v2->w };
    float sc = (t->tex.w2c1 & 0x4000) ? 16.0f : 1.0f;
    for (int y = miny; y <= maxy; y++) for (int x = minx; x <= maxx; x++) {
        float px = x + 0.5f, py = y + 0.5f;
        float b[3];
        b[0] = ((x1 - px) * (y2 - py) - (x2 - px) * (y1 - py)) / area;
        b[1] = ((x2 - px) * (y0 - py) - (x0 - px) * (y2 - py)) / area;
        b[2] = 1 - b[0] - b[1];
        if (b[0] < 0 || b[1] < 0 || b[2] < 0) continue;
        float zi = b[0] * iw[0] + b[1] * iw[1] + b[2] * iw[2];
        float z = 1 / zi;
        if (z >= zb[y][x]) continue;
        if (t->pattern & 15) {     /* 501 screen-door pattern (same ordered matrix as the Vulkan shader) */
            static const int bayer[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
            int qx = x & 3, n = t->pattern & 15; if (t->pattern & 16) qx = 3 - qx;
            int bb = bayer[(y & 3) * 4 + qx];
            if (n <= 8 ? bb < 2 * n : bb >= 2 * (n - 8)) continue;
        }
        float pc[3]; for (int k = 0; k < 3; k++) pc[k] = b[k] * iw[k] * z;      /* perspective-correct weights */
        float L[4], sp[3], b0[3], b1[3], fg[4];
#define LERP3(dst, f, n) for (int i = 0; i < n; i++) dst[i] = pc[0] * vv[0]->f[i] + pc[1] * vv[1]->f[i] + pc[2] * vv[2]->f[i]
        LERP3(L, light, 4); LERP3(sp, spec, 3); LERP3(b0, base, 3); LERP3(b1, col1, 3); LERP3(fg, fog, 4);
#undef LERP3
        float c[4]; int tm = modes & 7, am = (modes >> 3) & 1, lit = (modes >> 4) & 1;
        if (textured) {
            float u = (pc[0] * vv[0]->uv[0] + pc[1] * vv[1]->uv[0] + pc[2] * vv[2]->uv[0]) * sc;
            float v = (pc[0] * vv[0]->uv[1] + pc[1] * vv[1]->uv[1] + pc[2] * vv[2]->uv[1]) * sc;
            uint32_t tx = sample(&t->tex, u, v);
            float tr = (float)((tx >> 16) & 255) / 255.0f, tg = (float)((tx >> 8) & 255) / 255.0f, tb = (float)(tx & 255) / 255.0f, ta = (float)(tx >> 24) / 255.0f;
            float T[3] = { tr, tg, tb };
            for (int i = 0; i < 3; i++) {
                if (am) c[i] = b0[i] + (T[i] - b0[i]) * L[3];
                else if (tm == 1) c[i] = b0[i] + (T[i] - b0[i]) * ta;
                else if (tm == 2) c[i] = T[i] * b1[i];
                else if (tm == 3) c[i] = b0[i];
                else if (tm == 4) c[i] = b0[i] + (b1[i] - b0[i]) * T[i];
                else c[i] = T[i];
            }
            c[3] = am ? ta : tm == 1 ? L[3] : ta * L[3];
            if (atest && ta < (translucent ? 1.0f / 512.0f : 0.5f)) continue;
        } else if (am) { for (int i = 0; i < 3; i++) c[i] = b0[i] + (b1[i] - b0[i]) * L[3]; c[3] = 1; }
        else { for (int i = 0; i < 3; i++) c[i] = b0[i]; c[3] = L[3]; }
        if (lit) for (int i = 0; i < 3; i++) c[i] = c[i] * L[i] + sp[i];
        for (int i = 0; i < 3; i++) { if (c[i] > 1) c[i] = 1; c[i] += (fg[i] - c[i]) * fg[3]; }
        if (!translucent) c[3] = 1;
        float r = c[0] * 255, g = c[1] * 255, bl = c[2] * 255, al = c[3];
        if (mode == SR_BLEND) { r = fb[y][x][0] + (r - fb[y][x][0]) * al; g = fb[y][x][1] + (g - fb[y][x][1]) * al; bl = fb[y][x][2] + (bl - fb[y][x][2]) * al; }
        else if (mode == SR_ADD) { r = fb[y][x][0] + r * al; g = fb[y][x][1] + g * al; bl = fb[y][x][2] + bl * al; }
        else zb[y][x] = z;
        fb[y][x][0] = (uint8_t)(r < 0 ? 0 : r > 255 ? 255 : r); fb[y][x][1] = (uint8_t)(g < 0 ? 0 : g > 255 ? 255 : g); fb[y][x][2] = (uint8_t)(bl < 0 ? 0 : bl > 255 ? 255 : bl);
        cov[y][x] = 1;
    }
}

static void sv_lerp(SV *o, const SV *a, const SV *b, float t) {
    o->x = a->x + (b->x - a->x) * t; o->y = a->y + (b->y - a->y) * t; o->w = a->w + (b->w - a->w) * t;
    for (int i = 0; i < 2; i++) o->uv[i] = a->uv[i] + (b->uv[i] - a->uv[i]) * t;
    for (int i = 0; i < 4; i++) { o->light[i] = a->light[i] + (b->light[i] - a->light[i]) * t; o->fog[i] = a->fog[i] + (b->fog[i] - a->fog[i]) * t; }
    for (int i = 0; i < 3; i++) { o->spec[i] = a->spec[i] + (b->spec[i] - a->spec[i]) * t; o->base[i] = a->base[i] + (b->base[i] - a->base[i]) * t;
                                  o->col1[i] = a->col1[i] + (b->col1[i] - a->col1[i]) * t; }
}

/* draw all triangles produced by the CP into the framebuffer */
void sr_render(const CpState *s) {
    uint8_t cr = 0, cg = 0, cb = 0;
    if (s->has_clear) { cr = s->clear_color & 255; cg = (s->clear_color >> 8) & 255; cb = (s->clear_color >> 16) & 255; }
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) { zb[y][x] = 1e30f; cov[y][x] = 0; fb[y][x][0] = cr; fb[y][x][1] = cg; fb[y][x][2] = cb; }
    const float nearw = 0.01f;
    unsigned groups = 0; for (int i = 0; i < s->ntris; i++) groups |= 1u << (s->tris[i].view.dfunc & 7);
    int first = 1;
    for (int grp = 0; grp < 8; grp++) {
      if (!(groups & (1u << grp))) continue;
      if (!first) for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) zb[y][x] = 1e30f;    /* a depth buffer of its own */
      first = 0;
      for (int pass = 0; pass < 2; pass++) for (int i = 0; i < s->ntris; i++) {
        const CpTri *t = &s->tris[i];
        int add = t->mat.blend == 2, blended = t->polytype == 0xd || t->polytype == 0xb, shadow = t->polytype == 5 || t->polytype == 7;
        int translucent = blended || add;
        if ((t->view.dfunc & 7) != grp || translucent != pass) continue;
        if (gfx_tri_culled(t)) continue;
        const CpViewport *vp = &t->view;
        float a[3], b[3], n[3];
        for (int k = 0; k < 3; k++) { a[k] = t->v[1].pos[k] - t->v[0].pos[k]; b[k] = t->v[2].pos[k] - t->v[0].pos[k]; }
        n[0] = a[1] * b[2] - a[2] * b[1]; n[1] = a[2] * b[0] - a[0] * b[2]; n[2] = a[0] * b[1] - a[1] * b[0];
        if (n[2] < 0) { n[0] = -n[0]; n[1] = -n[1]; n[2] = -n[2]; }
        int textured = sr_texram && !sr_no_tex && ((t->mat.flags881 >> 3) & 1);
        SV in[3], poly[8]; int np = 0; uint32_t modes = 0;
        for (int k = 0; k < 3; k++) {
            GfxShade sh; gfx_vertex_shade(s, t, k, n, &sh); modes = sh.modes;
            in[k].x = t->v[k].pos[0]; in[k].y = t->v[k].pos[1]; in[k].w = -t->v[k].pos[2];      /* view space until projected */
            in[k].uv[0] = t->v[k].uv[0]; in[k].uv[1] = t->v[k].uv[1];
            for (int c = 0; c < 4; c++) { in[k].light[c] = sh.light[c]; in[k].fog[c] = sh.fog[c] / 255.0f; }
            for (int c = 0; c < 3; c++) { in[k].spec[c] = sh.spec[c]; in[k].base[c] = sh.base[c] / 255.0f; in[k].col1[c] = sh.col1[c] / 255.0f; }
        }
        for (int k = 0; k < 3; k++) {              /* clip against the near plane */
            const SV *p = &in[k], *q = &in[(k + 1) % 3];
            int pin = p->w >= nearw, qin = q->w >= nearw;
            if (pin) poly[np++] = *p;
            if (pin != qin) sv_lerp(&poly[np++], p, q, (nearw - p->w) / (q->w - p->w));
        }
        if (np < 3) continue;
        for (int k = 0; k < np; k++) { float w = poly[k].w; poly[k].x = vp->off_x + poly[k].x * vp->f / w; poly[k].y = vp->off_y - poly[k].y * vp->f / w; }
        int cx0 = (int)vp->l, cx1 = (int)vp->r, cy0 = (int)vp->b, cy1 = (int)vp->t;
        if (cx1 <= cx0 || cy1 <= cy0) { cx0 = 0; cy0 = 0; cx1 = W; cy1 = H; }
        if (cx0 < 0) cx0 = 0; if (cy0 < 0) cy0 = 0; if (cx1 > W) cx1 = W; if (cy1 > H) cy1 = H;
        for (int k = 1; k + 1 < np; k++)
            raster(t, &poly[0], &poly[k], &poly[k + 1], textured, t->polytype != 3 && !shadow, translucent,
                   pass == 0 ? SR_OPAQUE : (add ? SR_ADD : SR_BLEND), cx0, cy0, cx1, cy1, modes);
      }
    }
}

/* write an RGBA8 buffer as PNG (RGB) */
void sr_png_rgba_fp(FILE *f, const uint8_t *rgba, int w, int h);
void sr_png_rgba(const char *path, const uint8_t *rgba, int w, int h) {
    FILE *f = fopen(path, "wb"); if (!f) return;
    sr_png_rgba_fp(f, rgba, w, h);
}
void sr_png_rgba_fp(FILE *f, const uint8_t *rgba, int w, int h) {
    size_t rl = (size_t)w * 3 + 1; uint8_t *raw = malloc(rl * h);
    for (int y = 0; y < h; y++) { raw[y * rl] = 0; for (int x = 0; x < w; x++) memcpy(raw + y * rl + 1 + x * 3, rgba + ((size_t)y * w + x) * 4, 3); }
    uLongf cl = compressBound(rl * h); uint8_t *cz = malloc(cl); compress(cz, &cl, raw, rl * h);
    static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 }; fwrite(sig, 1, 8, f);
    #define CH2(t, d, n) do { uint8_t hd[8]; uint32_t L = (n); hd[0]=L>>24;hd[1]=L>>16;hd[2]=L>>8;hd[3]=L; memcpy(hd+4,t,4); fwrite(hd,1,8,f); fwrite(d,1,L,f); \
        uint32_t c = crc32(0, (const Bytef*)t, 4); c = crc32(c, (const Bytef*)(d), L); uint8_t cc[4]={c>>24,c>>16,c>>8,c}; fwrite(cc,1,4,f);} while(0)
    uint8_t ih[13] = { w >> 24, w >> 16, w >> 8, w, h >> 24, h >> 16, h >> 8, h, 8, 2, 0, 0, 0 };
    CH2("IHDR", ih, 13); CH2("IDAT", cz, cl); CH2("IEND", "", 0);
    fclose(f); free(raw); free(cz);
}

/* 2D layers over the 3D image: layer 2 over layer 1, ABGR1555 with bit 15 = opaque */
void sr_overlay(const uint16_t *lay, int ena, int direct) {
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        for (int k = 0; k < 2; k++) {
            int l = (ena & 4) ? k : 1 - k;          /* bit 2: layer 1 on top (Eurasia 0x1bc bit 2) */
            if (!(ena & (1 << l))) continue;
            if (!(direct & (1 << l)) && cov[y][x]) continue;   /* behind the 3D: only where nothing was drawn */
            uint16_t p = lay[l * 640 * 480 + y * 640 + x];
            if (!(p & 0x8000)) continue;
            fb[y][x][0] = (uint8_t)((p & 31) << 3); fb[y][x][1] = (uint8_t)(((p >> 5) & 31) << 3); fb[y][x][2] = (uint8_t)(((p >> 10) & 31) << 3);
            break;
        }
    }
}
