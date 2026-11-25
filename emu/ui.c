/* Menus: launcher and pause menu on a premultiplied RGBA canvas (see ui.h).
 * Look: deep-blue gradient panels, gold / amber accents and holo-blue details, large type, amber-to-gold bars.
 * Text: stb_truetype with the fonts embedded in third_party/fonts_embed.h (SIL OFL). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "ui.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "third_party/stb_truetype.h"
#include "third_party/fonts_embed.h"

/* ---------------- colours ---------------- */
typedef struct { float r, g, b; } Col;
static const Col kGold = { 255, 214, 70 }, kAmber = { 255, 150, 40 }, kHolo = { 110, 205, 255 },
                 kText = { 226, 233, 245 }, kDim = { 128, 146, 176 }, kPanelTop = { 10, 20, 40 }, kPanelBot = { 2, 5, 13 },
                 kFrame = { 13, 28, 54 }, kBlack = { 0, 0, 0 };
static Col mix(Col a, Col b, float t) { Col c = { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t }; return c; }

/* ---------------- canvas ---------------- */
static uint32_t *C; static int CW, CH;
static int clip_x0, clip_y0, clip_x1, clip_y1;
static void clip_reset(void) { clip_x0 = 0; clip_y0 = 0; clip_x1 = CW; clip_y1 = CH; }

static inline void blend(int x, int y, Col c, float a) {   /* a = coverage * opacity, 0..1 */
    if (x < clip_x0 || y < clip_y0 || x >= clip_x1 || y >= clip_y1 || a <= 0.002f) return;
    if (a > 1) a = 1;
    uint32_t d = C[y * CW + x];
    float k = 1 - a;
    int r = (int)(c.r * a + (d & 255) * k + 0.5f), g = (int)(c.g * a + ((d >> 8) & 255) * k + 0.5f),
        b = (int)(c.b * a + ((d >> 16) & 255) * k + 0.5f), al = (int)(255 * a + (d >> 24) * k + 0.5f);
    if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255; if (al > 255) al = 255;
    C[y * CW + x] = (uint32_t)r | (uint32_t)g << 8 | (uint32_t)b << 16 | (uint32_t)al << 24;
}
/* coverage of a rounded rectangle at pixel centre (px, py) */
static float rr_cov(float px, float py, float x, float y, float w, float h, float rad) {
    float cx = px < x + rad ? x + rad : px > x + w - rad ? x + w - rad : px;
    float cy = py < y + rad ? y + rad : py > y + h - rad ? y + h - rad : py;
    float dx = px - cx, dy = py - cy;
    if (dx == 0 && dy == 0) {   /* inside the straight part: edge distance */
        float e = fminf(fminf(px - x, x + w - px), fminf(py - y, y + h - py));
        return e >= 0.5f ? 1 : e + 0.5f > 0 ? e + 0.5f : 0;
    }
    float dist = rad - sqrtf(dx * dx + dy * dy);
    return dist >= 0.5f ? 1 : dist + 0.5f > 0 ? dist + 0.5f : 0;
}
/* vertical gradient rounded rectangle; a0/a1 = opacity at the top / bottom */
static void rrect_v(float x, float y, float w, float h, float rad, Col c0, float a0, Col c1, float a1) {
    int ix0 = (int)floorf(x), iy0 = (int)floorf(y), ix1 = (int)ceilf(x + w), iy1 = (int)ceilf(y + h);
    for (int py = iy0; py < iy1; py++) {
        float t = h > 1 ? (py + 0.5f - y) / h : 0; if (t < 0) t = 0; if (t > 1) t = 1;
        Col c = mix(c0, c1, t); float a = a0 + (a1 - a0) * t;
        for (int px = ix0; px < ix1; px++) {
            float cov = rad > 0 ? rr_cov(px + 0.5f, py + 0.5f, x, y, w, h, rad) : 1;
            if (!rad) { float e = fminf(fminf(px + 1 - x, x + w - px), fminf(py + 1 - y, y + h - py)); cov = e < 1 ? (e > 0 ? e : 0) : 1; }
            blend(px, py, c, cov * a);
        }
    }
}
/* horizontal gradient rounded rectangle */
static void rrect_h(float x, float y, float w, float h, float rad, Col c0, float a0, Col c1, float a1) {
    int ix0 = (int)floorf(x), iy0 = (int)floorf(y), ix1 = (int)ceilf(x + w), iy1 = (int)ceilf(y + h);
    for (int px = ix0; px < ix1; px++) {
        float t = w > 1 ? (px + 0.5f - x) / w : 0; if (t < 0) t = 0; if (t > 1) t = 1;
        Col c = mix(c0, c1, t); float a = a0 + (a1 - a0) * t;
        for (int py = iy0; py < iy1; py++) blend(px, py, c, rr_cov(px + 0.5f, py + 0.5f, x, y, w, h, rad) * a);
    }
}
static void rrect(float x, float y, float w, float h, float rad, Col c, float a) { rrect_v(x, y, w, h, rad, c, a, c, a); }
/* outline: ring between two rounded rectangles */
static void rrect_line(float x, float y, float w, float h, float rad, float th, Col c, float a) {
    int ix0 = (int)floorf(x), iy0 = (int)floorf(y), ix1 = (int)ceilf(x + w), iy1 = (int)ceilf(y + h);
    for (int py = iy0; py < iy1; py++) for (int px = ix0; px < ix1; px++) {
        float o = rr_cov(px + 0.5f, py + 0.5f, x, y, w, h, rad);
        float i = rr_cov(px + 0.5f, py + 0.5f, x + th, y + th, w - 2 * th, h - 2 * th, rad > th ? rad - th : 0);
        blend(px, py, c, (o - i) * a);
    }
}
static void disc(float cx, float cy, float r, Col c, float a) { rrect(cx - r, cy - r, 2 * r, 2 * r, r, c, a); }
/* filled triangle pointing left (dir -1) or right (+1), centred at (cx, cy), half-height r */
static void tri(float cx, float cy, float r, int dir, Col c, float a) {
    for (int y = (int)(cy - r) - 1; y <= (int)(cy + r) + 1; y++) for (int x = (int)(cx - r) - 1; x <= (int)(cx + r) + 1; x++) {
        float py = y + 0.5f - cy, px = (x + 0.5f - cx) * dir;   /* tip at +r */
        float w = (r - px) * 0.5f;                                   /* half-height of the triangle at this x */
        if (px < -r * 0.8f - 0.5f || px > r + 0.5f) continue;
        float cov = fminf(fminf(w - fabsf(py) + 0.5f, px + r * 0.8f + 0.5f), 1);
        if (cov > 0) blend(x, y, c, cov * a);
    }
}
static void hline(float x0, float x1, float y, Col c, float a) { for (int x = (int)x0; x < (int)x1; x++) blend(x, (int)y, c, a); }

/* ---------------- text ---------------- */
enum { F_TITLE, F_BODY, F_BOLD, F_N };
static stbtt_fontinfo font[F_N]; static int fonts_ok;
typedef struct { uint8_t *bm; int w, h, xo, yo; float adv; int done; } Glyph;
typedef struct { int f, px; float scale; int asc; Glyph g[256 + 16]; } GSet;
static GSet *gsets[32]; static int ngsets;
static const int extra_cp[16] = { 0x2013, 0x2014, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2026, 0x2190, 0x2191, 0x2192, 0x2193, 0x25b2, 0x25b6, 0x25bc, 0x25c0 };
static int gidx(int cp) { if (cp < 256) return cp; for (int i = 0; i < 16; i++) if (extra_cp[i] == cp) return 256 + i; return '?'; }
static void fonts_init(void) {
    if (fonts_ok) return;
    const unsigned char *d[F_N] = { font_title, font_body, font_bold };
    for (int i = 0; i < F_N; i++) stbtt_InitFont(&font[i], d[i], stbtt_GetFontOffsetForIndex(d[i], 0));
    fonts_ok = 1;
}
static GSet *gset(int f, int px) {
    for (int i = 0; i < ngsets; i++) if (gsets[i]->f == f && gsets[i]->px == px) return gsets[i];
    if (ngsets == 32) { free(gsets[0]); memmove(gsets, gsets + 1, 31 * sizeof *gsets); ngsets--; }   /* never in practice */
    GSet *s = calloc(1, sizeof *s); s->f = f; s->px = px;
    s->scale = stbtt_ScaleForPixelHeight(&font[f], (float)px);
    int asc, desc, gap; stbtt_GetFontVMetrics(&font[f], &asc, &desc, &gap); s->asc = (int)(asc * s->scale + 0.5f);
    gsets[ngsets++] = s; return s;
}
static Glyph *glyph(GSet *s, int cp) {
    int i = gidx(cp); Glyph *g = &s->g[i];
    if (!g->done) {
        int code = i < 256 ? i : extra_cp[i - 256], adv, lsb;
        stbtt_GetCodepointHMetrics(&font[s->f], code, &adv, &lsb); g->adv = adv * s->scale;
        g->bm = stbtt_GetCodepointBitmap(&font[s->f], s->scale, s->scale, code, &g->w, &g->h, &g->xo, &g->yo);
        g->done = 1;
    }
    return g;
}
static int utf8(const char **p) {
    const unsigned char *s = (const unsigned char *)*p; int c = *s;
    if (c < 0x80) { *p += 1; return c; }
    if ((c & 0xe0) == 0xc0 && s[1]) { *p += 2; return ((c & 31) << 6) | (s[1] & 63); }
    if ((c & 0xf0) == 0xe0 && s[1] && s[2]) { *p += 3; return ((c & 15) << 12) | ((s[1] & 63) << 6) | (s[2] & 63); }
    *p += 1; return '?';
}
static float text_w(int f, int px, const char *s, float track) {
    GSet *g = gset(f, px); float w = 0; int n = 0;
    while (*s) { w += glyph(g, utf8(&s))->adv + track; n++; }
    return n ? w - track : 0;
}
/* y = top of the line box; align 0 left, 1 centre, 2 right; returns the width */
static float text(int f, int px, float x, float y, const char *s, Col c, float a, int align, float track) {
    GSet *g = gset(f, px); float w = text_w(f, px, s, track);
    if (align == 1) x -= w / 2; else if (align == 2) x -= w;
    float pen = x; int base = (int)(y + g->asc);
    while (*s) {
        Glyph *gl = glyph(g, utf8(&s));
        int ox = (int)floorf(pen + 0.5f) + gl->xo, oy = base + gl->yo;
        for (int j = 0; j < gl->h; j++) for (int i = 0; i < gl->w; i++) {
            uint8_t v = gl->bm[j * gl->w + i]; if (v) blend(ox + i, oy + j, c, a * v / 255.0f);
        }
        pen += gl->adv + track;
    }
    return w;
}
/* text with a soft glow around it */
static void text_glow(int f, int px, float x, float y, const char *s, Col c, Col glow, float ga, int align, float track, int rad) {
    for (int dy = -rad; dy <= rad; dy += 2) for (int dx = -rad; dx <= rad; dx += 2) {
        float d = sqrtf((float)(dx * dx + dy * dy)); if (d > rad || (!dx && !dy)) continue;
        text(f, px, x + dx, y + dy, s, glow, ga * (1 - d / (rad + 1)), align, track);
    }
    text(f, px, x, y, s, c, 1, align, track);
    if (f == F_TITLE) text(f, px, x + px / 36.0f, y, s, c, 1, align, track);   /* a little heavier than the regular weight */
}

/* ---------------- state ---------------- */
static int open_, screen, page, sel, sub_settings;   /* sub_settings: launcher -> settings pages */
static int launch_sel;
static UiOptions *O; static UiInfo *I;
static int en(void) { return I && I->lang_en ? I->lang_en() : 0; }
#define T(es, eng) (en() ? (eng) : (es))

enum { P_GAME, P_VIDEO, P_CONTROLS, P_SYSTEM, P_N };
enum { K_ACTION, K_CHOICE, K_TOGGLE, K_SLIDER };
typedef struct { int id, kind; } Row;
enum { R_CONTINUE, R_RESET, R_SHOT, R_LAUNCHER, R_QUIT, R_SCALE, R_MSAA, R_WIDE, R_PILLAR, R_CULL, R_FULL, R_PAD, R_SPEED, R_INVY, R_REGION, R_LANG, R_SHOWL };

static int first_page(void) { return screen == UI_SCREEN_PAUSE ? P_GAME : P_VIDEO; }
static int rows(int p, Row *r) {
    int n = 0;
    switch (p) {
    case P_GAME:
        r[n++] = (Row){ R_CONTINUE, K_ACTION }; r[n++] = (Row){ R_RESET, K_ACTION }; r[n++] = (Row){ R_SHOT, K_ACTION };
        r[n++] = (Row){ R_LAUNCHER, K_ACTION }; r[n++] = (Row){ R_QUIT, K_ACTION }; break;
    case P_VIDEO:
        r[n++] = (Row){ R_SCALE, K_CHOICE }; r[n++] = (Row){ R_MSAA, K_CHOICE }; r[n++] = (Row){ R_WIDE, K_TOGGLE }; r[n++] = (Row){ R_PILLAR, K_TOGGLE }; r[n++] = (Row){ R_CULL, K_TOGGLE }; r[n++] = (Row){ R_FULL, K_TOGGLE }; break;
    case P_CONTROLS:
        r[n++] = (Row){ R_PAD, K_TOGGLE }; r[n++] = (Row){ R_SPEED, K_SLIDER }; r[n++] = (Row){ R_INVY, K_TOGGLE }; break;
    case P_SYSTEM:
        r[n++] = (Row){ R_REGION, K_CHOICE }; r[n++] = (Row){ R_LANG, K_CHOICE }; r[n++] = (Row){ R_SHOWL, K_TOGGLE }; break;
    }
    return n;
}
static const char *page_name(int p) {
    switch (p) { case P_GAME: return T("PARTIDA", "GAME"); case P_VIDEO: return T("VÍDEO", "VIDEO");
                 case P_CONTROLS: return T("CONTROLES", "CONTROLS"); default: return T("SISTEMA", "SYSTEM"); }
}
static const char *row_label(int id) {
    switch (id) {
    case R_CONTINUE: return T("Continuar", "Resume");
    case R_RESET: return T("Reiniciar la máquina", "Reset the machine");
    case R_SHOT: return T("Captura de pantalla", "Screenshot");
    case R_LAUNCHER: return T("Volver al lanzador", "Back to the launcher");
    case R_QUIT: return T("Salir al escritorio", "Quit to desktop");
    case R_SCALE: return T("Resolución interna", "Internal resolution");
    case R_MSAA: return T("Antialiasing (MSAA)", "Antialiasing (MSAA)");
    case R_WIDE: return T("Panorámico 16:9", "Widescreen 16:9");
    case R_PILLAR: return T("Bordes limpios en panorámico", "Clean widescreen edges");
    case R_CULL: return T("Ocultar caras traseras", "Hide back faces");
    case R_FULL: return T("Pantalla completa", "Fullscreen");
    case R_PAD: return T("Mando", "Gamepad");
    case R_SPEED: return T("Velocidad de la palanca (teclas)", "Stick speed (keys)");
    case R_INVY: return T("Invertir arriba / abajo", "Invert up / down");
    case R_REGION: return T("País / región", "Country / region");
    case R_LANG: return "Idioma / Language";
    case R_SHOWL: return T("Mostrar el lanzador al iniciar", "Show the launcher at start");
    }
    return "";
}
static const char *row_help(int id) {
    switch (id) {
    case R_CONTINUE: return T("Vuelve a la partida.", "Back to the game.");
    case R_RESET: return T("Reinicia la placa como el botón de reset del mueble.", "Resets the board like the cabinet's reset button.");
    case R_SHOT: return T("Guarda la imagen en la carpeta 'capturas'.", "Saves the picture in the 'capturas' folder.");
    case R_LAUNCHER: return T("Detiene la partida y vuelve a la pantalla de inicio.", "Stops the game and goes back to the start screen.");
    case R_QUIT: return T("Guarda los ajustes y cierra el programa.", "Saves the settings and closes the program.");
    case R_SCALE: return T("Multiplica los 640x480 de la placa. Más alto = más nítido y más trabajo para la GPU.", "Multiplies the board's 640x480. Higher = sharper and more GPU work.");
    case R_MSAA: return T("Suaviza los bordes de los polígonos.", "Smooths polygon edges.");
    case R_WIDE: return T("Ensancha la vista 3D; los gráficos 2D quedan centrados.", "Widens the 3D view; the 2D graphics stay centred.");
    case R_PILLAR: return T("Bandas negras junto a pantallas solo 4:3 (título); franjas de cine a todo lo ancho.",
                            "Black bars beside 4:3-only screens (title); cutscene bars reach the edges.");
    case R_CULL: return T("No dibuja el reverso de los polígonos opacos, como la placa. Evita destellos grises.",
                          "Skips the far side of solid polygons, like the arcade board. Stops grey flashes.");
    case R_FULL: return T("También con F11 (o Alt+Intro en Windows).", "Also with F11 (or Alt+Enter on Windows).");
    case R_PAD: return T("Palanca izquierda o cruceta, A/RT disparo, X/B/LT misil, Y bomba, LB/RB vista.", "Left stick or d-pad, A/RT shot, X/B/LT missile, Y bomb, LB/RB view.");
    case R_SPEED: return T("Con teclado la palanca analógica se mueve poco a poco.", "With the keyboard the analog stick moves gradually.");
    case R_INVY: return T("Arriba baja el morro, como un avión.", "Up dives, like a plane.");
    case R_REGION: return screen == UI_SCREEN_PAUSE ? T("Cambiarlo reinicia la máquina (la BIOS lo lee al arrancar).", "Changing it resets the machine (the BIOS reads it at boot).")
                                                    : T("La BIOS lo lee al arrancar: textos y avisos del país.", "The BIOS reads it at boot: the country's texts and notices.");
    case R_LANG: return T("Idioma de los menús de este programa.", "Language of this program's menus.");
    case R_SHOWL: return T("Si no, el juego arranca directamente (F1 / Select+Start abren este menú).", "Otherwise the game starts directly (F1 / Back+Start open this menu).");
    }
    return "";
}
static const char *region_name(int r) {
    static const char *es[5] = { "Japón", "EE. UU.", "Exportación", "Australia", "Corea" }, *e[5] = { "Japan", "USA", "Export", "Australia", "Korea" };
    return (en() ? e : es)[r < 0 || r > 4 ? 2 : r];
}
static void row_value(int id, char *v, int n) {
    v[0] = 0;
    switch (id) {
    case R_SCALE: snprintf(v, n, "%dx  ·  %dx%d", O->scale, (O->wide ? 854 : 640) * O->scale, 480 * O->scale); break;
    case R_MSAA: if (O->msaa <= 1) snprintf(v, n, "%s", T("No", "Off"));
                 else if (I && I->msaa_gpu > 0 && I->msaa_gpu != O->msaa) snprintf(v, n, "%dx (GPU %dx)", O->msaa, I->msaa_gpu);
                 else snprintf(v, n, "%dx", O->msaa); break;
    case R_REGION: snprintf(v, n, "%s", region_name(O->region)); break;
    case R_LANG: snprintf(v, n, "%s", O->lang == 0 ? "Auto" : O->lang == 1 ? "Español" : "English"); break;
    case R_SPEED: snprintf(v, n, "%d", O->stick_speed); break;
    }
}
static int toggle_on(int id) {
    switch (id) { case R_WIDE: return O->wide; case R_PILLAR: return O->pillarbox; case R_CULL: return O->backface_cull; case R_FULL: return O->fullscreen; case R_PAD: return O->pad_mode;
                  case R_INVY: return O->invert_y; case R_SHOWL: return O->show_launcher; }
    return 0;
}

void ui_open(int scr, UiOptions *o, UiInfo *info) {
    fonts_init();
    O = o; I = info; screen = scr; open_ = 1; sub_settings = 0; launch_sel = 0;
    page = first_page(); sel = 0;
}
void ui_close(void) { open_ = 0; }
int ui_is_open(void) { return open_; }
int ui_screen(void) { return screen; }

static int msaa_step(int m, int d) { static const int v[4] = { 1, 2, 4, 8 }; int i = m >= 8 ? 3 : m >= 4 ? 2 : m >= 2 ? 1 : 0; i += d; if (i < 0) i = 0; if (i > 3) i = 3; return v[i]; }
static int change(int id, int d) {   /* d = -1 / +1 (0 = accept: toggles and choices go forward) */
    int fwd = d ? d : 1;
    switch (id) {
    case R_SCALE: O->scale += fwd; if (O->scale < 1) O->scale = d ? 1 : 8; if (O->scale > 8) O->scale = d ? 8 : 1; return UI_VIDEO;
    case R_MSAA: { int m = msaa_step(O->msaa, fwd); if (!d && m == O->msaa) m = 1; O->msaa = m; return UI_VIDEO; }
    case R_WIDE: O->wide = d ? d > 0 : !O->wide; return UI_VIDEO;
    case R_PILLAR: O->pillarbox = d ? d > 0 : !O->pillarbox; return UI_VIDEO;
    case R_CULL: O->backface_cull = d ? d > 0 : !O->backface_cull; return UI_VIDEO;
    case R_FULL: O->fullscreen = d ? d > 0 : !O->fullscreen; return UI_FULLSCREEN;
    case R_PAD: O->pad_mode = d ? d > 0 : !O->pad_mode; return UI_SAVE;
    case R_INVY: O->invert_y = d ? d > 0 : !O->invert_y; return UI_SAVE;
    case R_SHOWL: O->show_launcher = d ? d > 0 : !O->show_launcher; return UI_SAVE;
    case R_SPEED: O->stick_speed += fwd; if (O->stick_speed < 1) O->stick_speed = 1; if (O->stick_speed > 10) O->stick_speed = 10; return UI_SAVE;
    case R_LANG: O->lang = (O->lang + fwd + 3) % 3; return UI_SAVE;
    case R_REGION: O->region = (O->region + fwd + 5) % 5; return screen == UI_SCREEN_PAUSE ? UI_REGION : UI_SAVE;
    }
    return UI_NONE;
}
int ui_key(int key) {
    if (!open_) return UI_NONE;
    if (screen == UI_SCREEN_LAUNCHER && !sub_settings) {   /* launcher: Start / Settings / Quit */
        if (key == UI_KEY_UP) launch_sel = (launch_sel + 2) % 3;
        else if (key == UI_KEY_DOWN) launch_sel = (launch_sel + 1) % 3;
        else if (key == UI_KEY_OK) {
            if (launch_sel == 0) { open_ = 0; return UI_START; }
            if (launch_sel == 1) { sub_settings = 1; page = P_VIDEO; sel = 0; return UI_NONE; }
            return UI_QUIT;
        } else if (key == UI_KEY_BACK && launch_sel != 2) launch_sel = 2;
        return UI_NONE;
    }
    Row r[8]; int n = rows(page, r);
    switch (key) {
    case UI_KEY_UP: sel = (sel + n - 1) % n; break;
    case UI_KEY_DOWN: sel = (sel + 1) % n; break;
    case UI_KEY_PREV: case UI_KEY_NEXT: {
        int p0 = first_page(); page = p0 + (page - p0 + (key == UI_KEY_NEXT ? 1 : -1) + (P_N - p0)) % (P_N - p0); sel = 0; break; }
    case UI_KEY_LEFT: case UI_KEY_RIGHT:
        if (r[sel].kind == K_ACTION) return ui_key(key == UI_KEY_LEFT ? UI_KEY_PREV : UI_KEY_NEXT);
        return change(r[sel].id, key == UI_KEY_LEFT ? -1 : 1);
    case UI_KEY_OK:
        if (r[sel].kind != K_ACTION) return change(r[sel].id, 0);
        switch (r[sel].id) {
        case R_CONTINUE: open_ = 0; return UI_RESUME;
        case R_RESET: open_ = 0; return UI_RESET;
        case R_SHOT: open_ = 0; return UI_SCREENSHOT;
        case R_LAUNCHER: screen = UI_SCREEN_LAUNCHER; sub_settings = 0; launch_sel = 0; return UI_LAUNCHER;
        case R_QUIT: return UI_QUIT;
        }
        break;
    case UI_KEY_BACK:
        if (screen == UI_SCREEN_LAUNCHER) { sub_settings = 0; launch_sel = 1; return UI_NONE; }
        open_ = 0; return UI_RESUME;
    }
    return UI_NONE;
}

/* ---------------- drawing ---------------- */
static float S;   /* canvas units: the layout is made for 720 lines */
static void backdrop(double t, int dim_only) {
    if (dim_only) {   /* pause: the game stays visible, darkened, with a blue tint towards the bottom */
        for (int y = 0; y < CH; y++) { float k = (float)y / CH; Col c = mix(kBlack, kPanelBot, k); float a = 0.62f + 0.18f * k;
            for (int x = 0; x < CW; x++) blend(x, y, c, a); }
        return;
    }
    for (int y = 0; y < CH; y++) {   /* deep space gradient */
        float k = (float)y / CH; Col c = mix((Col){ 7, 15, 34 }, (Col){ 1, 3, 9 }, k);
        uint32_t px = (uint32_t)c.r | (uint32_t)c.g << 8 | (uint32_t)c.b << 16 | 0xff000000u;
        for (int x = 0; x < CW; x++) C[y * CW + x] = px;
    }
    /* a soft amber sun low on the horizon */
    float hx = CW * 0.72f, hy = CH * 0.60f, R = CH * 0.55f;
    for (int y = (int)(hy - R); y < (int)(hy + R) && y < CH; y++) for (int x = (int)(hx - R); x < (int)(hx + R); x++) {
        if (y < 0 || x < 0 || x >= CW) continue;
        float d = sqrtf((x - hx) * (x - hx) + (y - hy) * (y - hy)) / R; if (d >= 1) continue;
        blend(x, y, kAmber, 0.10f * (1 - d) * (1 - d));
    }
    /* stars drifting slowly */
    uint32_t seed = 12345;
    for (int i = 0; i < 140; i++) {
        seed = seed * 1103515245u + 12345u; float sx = (seed >> 8) % 10000 / 10000.0f;
        seed = seed * 1103515245u + 12345u; float sy = (seed >> 8) % 10000 / 10000.0f;
        seed = seed * 1103515245u + 12345u; float sp = 0.2f + (seed >> 8) % 100 / 100.0f;
        float x = fmodf(sx * CW - (float)t * 6 * sp + CW * 4, (float)CW), y = sy * CH * 0.62f;
        float tw = 0.35f + 0.35f * sinf((float)t * 1.7f * sp + i);
        disc(x, y, 0.6f + sp, kText, tw * sp);
    }
    /* perspective grid towards the horizon */
    float hz = CH * 0.64f;
    for (int y = (int)hz; y < CH; y++) blend(0, y, kBlack, 0);
    for (int i = 0; i < 14; i++) {   /* horizontal lines, moving towards the viewer */
        float u = fmodf(i / 14.0f + (float)t * 0.04f, 1.0f); float y = hz + (CH - hz) * u * u;
        hline(0, (float)CW, y, kHolo, 0.08f + 0.22f * u);
    }
    for (int i = -12; i <= 12; i++) {   /* lines converging on the vanishing point */
        float xb = CW * 0.5f + i * CW * 0.11f;
        for (int y = (int)hz; y < CH; y++) { float u = (y - hz) / (CH - hz); float x = CW * 0.5f + (xb - CW * 0.5f) * u; blend((int)x, y, kHolo, 0.05f + 0.2f * u); }
    }
    for (int x = 0; x < CW; x++) for (int k = 0; k < 3; k++) blend(x, (int)hz - k, kHolo, 0.25f - k * 0.08f);
}
static void panel(float x, float y, float w, float h) {
    rrect(x + 6 * S, y + 10 * S, w, h, 18 * S, kBlack, 0.35f);                          /* shadow */
    rrect_v(x, y, w, h, 16 * S, kPanelTop, 238 / 255.0f, kPanelBot, 246 / 255.0f);
    rrect_line(x, y, w, h, 16 * S, 1.5f * S, kHolo, 0.28f);
    rrect_h(x + 24 * S, y, w - 48 * S, 2 * S, 1 * S, kAmber, 0.0f, kGold, 0.9f);         /* lit top edge */
}
static void chip(float x, float y, const char *key, const char *label, float *adv) {   /* footer hint: [key] label */
    int px = (int)(24 * S), arrows = !strcmp(key, "<>"); float kw = arrows ? 56 * S : text_w(F_BOLD, px, key, 0) + 18 * S;
    rrect(x, y, kw, 32 * S, 8 * S, kFrame, 1); rrect_line(x, y, kw, 32 * S, 8 * S, 1.2f * S, kHolo, 0.7f);
    if (arrows) { tri(x + 18 * S, y + 16 * S, 7 * S, -1, kHolo, 1); tri(x + kw - 18 * S, y + 16 * S, 7 * S, 1, kHolo, 1); }
    else text(F_BOLD, px, x + kw / 2, y + 3 * S, key, kHolo, 1, 1, 0);
    float lw = text(F_BODY, px, x + kw + 10 * S, y + 3 * S, label, kText, 0.9f, 0, 0);
    *adv = kw + 10 * S + lw + 30 * S;
}
static void footer(int launcher) {
    float y = CH - 54 * S;
    rrect_v(0, y - 8 * S, (float)CW, 62 * S, 0, kBlack, 0.0f, kBlack, 0.55f);
    int pad = I && I->pad_name; float x = 40 * S, a;
    chip(x, y, pad ? "A" : T("Intro", "Enter"), T("Aceptar", "Accept"), &a); x += a;
    chip(x, y, pad ? "B" : "Esc", T("Volver", "Back"), &a); x += a;
    if (!launcher) { chip(x, y, pad ? "LB / RB" : "Tab", T("Pestaña", "Tab"), &a); x += a;
                     chip(x, y, "<>", T("Cambiar", "Change"), &a); x += a; }
}
static void draw_switch(float x, float y, int on) {   /* pill switch, x = right edge */
    float w = 74 * S, h = 34 * S; x -= w;
    if (on) rrect_h(x, y, w, h, h / 2, kAmber, 1, kGold, 1); else { rrect(x, y, w, h, h / 2, kFrame, 1); rrect_line(x, y, w, h, h / 2, 1.5f * S, kDim, 0.6f); }
    disc(on ? x + w - h / 2 : x + h / 2, y + h / 2, h / 2 - 4 * S, on ? (Col){ 255, 250, 235 } : kDim, 1);
}
static void draw_slider(float x, float y, int v, int vmax, int selected) {   /* x = right edge */
    float w = 300 * S, h = 12 * S; x -= w + 54 * S;
    rrect(x, y + 11 * S, w, h, h / 2, kFrame, 1);
    float f = (float)v / vmax;
    rrect_h(x, y + 11 * S, w * f, h, h / 2, kAmber, 1, kGold, 1);
    disc(x + w * f, y + 17 * S, 13 * S, selected ? kGold : kText, 1);
    char b[8]; snprintf(b, sizeof b, "%d", v);
    text(F_BOLD, (int)(32 * S), x + w + 54 * S, y - 4 * S, b, selected ? kGold : kText, 1, 2, 0);
}
static void draw_launcher(double t) {
    backdrop(t, 0);
    float x = 80 * S;
    text(F_BOLD, (int)(24 * S), x, 64 * S, T("SEGA HIKARU  ·  RECOMPILACIÓN ESTÁTICA", "SEGA HIKARU  ·  STATIC RECOMPILATION"), kHolo, 0.95f, 0, 3 * S);
    text_glow(F_TITLE, (int)(76 * S), x - 4 * S, 96 * S, "PLANET", kGold, kAmber, 0.10f, 0, 4 * S, (int)(8 * S));
    text_glow(F_TITLE, (int)(76 * S), x - 4 * S, 176 * S, "HARRIERS", kGold, kAmber, 0.10f, 0, 4 * S, (int)(8 * S));
    rrect_h(x, 272 * S, 520 * S, 4 * S, 2 * S, kAmber, 1, kGold, 0.0f);
    /* menu */
    const char *items[3] = { T("Iniciar partida", "Start game"), T("Ajustes", "Settings"), T("Salir", "Quit") };
    for (int i = 0; i < 3; i++) {
        float y = (318 + i * 74) * S;
        int s = i == launch_sel;
        if (s) {
            float pulse = 0.75f + 0.25f * sinf((float)t * 4);
            rrect_h(x - 24 * S, y - 6 * S, 470 * S, 62 * S, 12 * S, kAmber, 0.32f * pulse, kAmber, 0.0f);
            rrect(x - 24 * S, y - 6 * S, 6 * S, 62 * S, 3 * S, kGold, 1);
            tri(x + 400 * S, y + 26 * S, 12 * S, 1, kGold, pulse);
        }
        text(F_BOLD, (int)(44 * S), x, y, items[i], s ? kGold : kText, s ? 1 : 0.85f, 0, 0);
    }
    /* status card */
    float pw = 420 * S, ph = 300 * S, px = CW - pw - 60 * S, py = 300 * S;
    if (CW < 1000) { pw = 330 * S; px = CW - pw - 40 * S; }
    panel(px, py, pw, ph);
    text(F_TITLE, (int)(22 * S), px + 26 * S, py + 22 * S, T("ESTADO", "STATUS"), kHolo, 1, 0, 3 * S);
    char b[160]; float ly = py + 66 * S; int fs = (int)(26 * S);
    struct { const char *k; char v[96]; int ok; } L[5];
    snprintf(L[0].v, 96, "%s", I && I->rom ? I->rom : T("no encontrada", "not found")); L[0].k = "ROM"; L[0].ok = I && I->rom;
    if (I && I->blocks > 0) snprintf(L[1].v, 96, T("%d bloques recompilados", "%d recompiled blocks"), I->blocks);
    else snprintf(L[1].v, 96, "%s", T("intérprete (compila el juego)", "interpreter (build the game)"));
    L[1].k = T("Código", "Code"); L[1].ok = I && I->blocks > 0;
    snprintf(L[2].v, 96, "%s", I && I->gpu ? I->gpu : "-"); L[2].k = "GPU"; L[2].ok = 1;
    snprintf(L[3].v, 96, "%s", I && I->pad_name ? I->pad_name : T("no conectado", "not connected")); L[3].k = T("Mando", "Gamepad"); L[3].ok = I && I->pad_name;
    snprintf(L[4].v, 96, "%s %s", I && I->platform ? I->platform : "", I && I->version ? I->version : ""); L[4].k = T("Versión", "Version"); L[4].ok = 1;
    for (int i = 0; i < 5; i++, ly += 44 * S) {
        disc(px + 34 * S, ly + 16 * S, 5 * S, L[i].ok ? kGold : kAmber, L[i].ok ? 1 : 0.6f);
        text(F_BODY, fs, px + 50 * S, ly, L[i].k, kDim, 1, 0, 0);
        /* value, shortened to fit */
        snprintf(b, sizeof b, "%s", L[i].v);
        float maxw = pw - 175 * S; while (strlen(b) > 4 && text_w(F_BOLD, fs, b, 0) > maxw) { size_t n = strlen(b); b[n - 4] = '.'; b[n - 3] = '.'; b[n - 2] = 0; }
        text(F_BOLD, fs, px + pw - 24 * S, ly, b, kText, 1, 2, 0);
    }
    footer(1);
}
static void draw_pages(double t) {
    int pause = screen == UI_SCREEN_PAUSE;
    backdrop(t, pause);
    float pw = CW - 120 * S; if (pw > 1080 * S) pw = 1080 * S;
    float ph = 560 * S, px = (CW - pw) / 2, py = 52 * S;
    panel(px, py, pw, ph);
    text_glow(F_TITLE, (int)(40 * S), px + 36 * S, py + 26 * S, pause ? T("PAUSA", "PAUSED") : T("AJUSTES", "SETTINGS"), kGold, kAmber, 0.10f, 0, 3 * S, (int)(5 * S));
    text(F_BOLD, (int)(22 * S), px + pw - 36 * S, py + 38 * S, "PLANET HARRIERS", kHolo, 0.8f, 2, 3 * S);
    /* tabs */
    float tx = px + 36 * S, ty = py + 92 * S;
    for (int p = first_page(); p < P_N; p++) {
        const char *nm = page_name(p); float w = text_w(F_BOLD, (int)(26 * S), nm, 2 * S) + 36 * S;
        if (p == page) { rrect_v(tx, ty, w, 44 * S, 10 * S, (Col){ 40, 70, 110 }, 0.55f, kFrame, 0.55f); rrect_h(tx + 10 * S, ty + 41 * S, w - 20 * S, 3 * S, 1.5f * S, kAmber, 1, kGold, 1); }
        text(F_BOLD, (int)(26 * S), tx + 18 * S, ty + 8 * S, nm, p == page ? kGold : kDim, 1, 0, 2 * S);
        tx += w + 10 * S;
    }
    hline(px + 36 * S, px + pw - 36 * S, ty + 46 * S, kHolo, 0.25f);
    /* rows */
    Row r[8]; int n = rows(page, r); if (sel >= n) sel = n - 1;
    float ry = ty + 66 * S, rx0 = px + 36 * S, rx1 = px + pw - 36 * S, rh = 58 * S;
    for (int i = 0; i < n; i++, ry += rh) {
        int s = i == sel;
        if (s) { rrect_h(rx0, ry, rx1 - rx0, rh - 6 * S, 10 * S, kHolo, 0.16f, kHolo, 0.04f); rrect(rx0, ry + 8 * S, 5 * S, rh - 22 * S, 2.5f * S, kGold, 1); }
        Col lc = s ? kGold : kText;
        text(F_BOLD, (int)(32 * S), rx0 + 22 * S, ry + 8 * S, row_label(r[i].id), lc, 1, 0, 0);
        char v[64]; row_value(r[i].id, v, sizeof v);
        if (r[i].kind == K_CHOICE) {
            float w = text(F_BOLD, (int)(30 * S), rx1 - 52 * S, ry + 9 * S, v, s ? kGold : kText, 1, 2, 0);
            tri(rx1 - 24 * S, ry + 27 * S, 10 * S, 1, s ? kAmber : kDim, 1);
            tri(rx1 - 76 * S - w, ry + 27 * S, 10 * S, -1, s ? kAmber : kDim, 1);
        } else if (r[i].kind == K_TOGGLE) draw_switch(rx1 - 16 * S, ry + 9 * S, toggle_on(r[i].id));
        else if (r[i].kind == K_SLIDER) draw_slider(rx1 - 16 * S, ry + 9 * S, O->stick_speed, 10, s);
        else if (s) tri(rx1 - 30 * S, ry + 27 * S, 10 * S, 1, kGold, 0.75f + 0.25f * sinf((float)t * 4));
    }
    /* controls page: the bindings, in two columns */
    if (page == P_CONTROLS && I && I->nbind) {
        float by = ry + 10 * S; hline(rx0, rx1, by - 6 * S, kHolo, 0.18f);
        int half = (I->nbind + 1) / 2; float cw = (rx1 - rx0) / 2;
        for (int i = 0; i < I->nbind; i++) {
            float cx = rx0 + (i >= half) * cw, cy = by + (i % half) * 30 * S;
            text(F_BODY, (int)(24 * S), cx + 22 * S, cy, I->bind_act[i], kDim, 1, 0, 0);
            text(F_BOLD, (int)(24 * S), cx + cw - 30 * S, cy, I->bind_key[i], kText, 1, 2, 0);
        }
    }
    /* help for the selected row */
    hline(rx0, rx1, py + ph - 62 * S, kHolo, 0.18f);
    text(F_BODY, (int)(25 * S), rx0 + 4 * S, py + ph - 50 * S, row_help(r[sel].id), kDim, 1, 0, 0);
    footer(0);
}
void ui_render(uint32_t *rgba, int w, int h, double t) {
    C = rgba; CW = w; CH = h; S = h / 720.0f; clip_reset();
    memset(rgba, 0, (size_t)w * h * 4);
    if (!open_) return;
    fonts_init();
    if (screen == UI_SCREEN_LAUNCHER && !sub_settings) draw_launcher(t); else draw_pages(t);
}
