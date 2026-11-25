/* Linux / Steam Deck frontend: SDL2 window + Vulkan, SDL audio, keyboard and gamepad, in-game menu.
 * Usage: ./pharrier [path/to/pharrier.zip]   (default: pharrier.zip next to the program or in roms/)
 * Launcher at start (can be turned off), menu: Esc / F10, or Guide, L3+R3 or Back+Start on the gamepad.
 * Settings: pharrier.ini / pharrier.nv / capturas/ next to the program if it is writable,
 * else in $XDG_CONFIG_HOME/pharrier-recomp (~/.config/pharrier-recomp). */
#include <SDL.h>
#include <SDL_vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <limits.h>
#include "emu_core.h"
#include "mie.h"
#include "gpu.h"
#include "vkrender.h"
#include "swrender.h"
#include "osd.h"
#include "ph_input.h"
#include "ui.h"
#ifndef PH_VERSION
#define PH_VERSION "0.2"
#endif

int gfx_vk_start(const VkrConfig *cfg);
void gfx_vk_redraw(const uint16_t *lay, uint32_t ena, uint32_t direct);
extern void (*g_vk_layer_hook)(uint16_t *lay, uint32_t *ena, uint32_t *direct);

/* ---------------- settings ---------------- */
enum { ACT_UP, ACT_DOWN, ACT_LEFT, ACT_RIGHT, ACT_SHOT, ACT_MISSILE, ACT_BOMB, ACT_VR_L, ACT_VR_R,
       ACT_START, ACT_COIN, ACT_TEST, ACT_SERVICE, ACT_PAUSE, ACT_N };
static const char *act_names[ACT_N] = { "up", "down", "left", "right", "shot", "missile", "bomb", "view_left", "view_right",
    "start", "coin", "test", "service", "pause" };
static struct { int scale, wide, pillarbox, msaa, fullscreen, stick_speed, pad_mode, invert_y, lang, launcher; int keys[ACT_N]; } S;
/* interface language: 0 = from the environment (LANG / LC_ALL starting with "es" = Spanish), 1 Spanish, 2 English */
static int lang_en(void) {
    if (S.lang == 1) return 0;
    if (S.lang == 2) return 1;
    const char *l = getenv("LC_ALL"); if (!l || !*l) l = getenv("LC_MESSAGES"); if (!l || !*l) l = getenv("LANG");
    return !(l && l[0] == 'e' && l[1] == 's');
}
#define T2(es, en) (lang_en() ? (en) : (es))
static char cfg_dir[PATH_MAX];

static void settings_defaults(void) {
    S.scale = 2; S.wide = getenv("SteamDeck") != 0; S.pillarbox = 1; S.msaa = getenv("SteamDeck") ? 2 : 1; S.fullscreen = 0; S.stick_speed = 5; S.pad_mode = 1; S.invert_y = 0; S.launcher = 1;
    int k[ACT_N] = { SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT, SDL_SCANCODE_Z, SDL_SCANCODE_X,
                     SDL_SCANCODE_C, SDL_SCANCODE_A, SDL_SCANCODE_S, SDL_SCANCODE_1, SDL_SCANCODE_5, SDL_SCANCODE_F2, SDL_SCANCODE_F1, SDL_SCANCODE_P };
    memcpy(S.keys, k, sizeof k);
}
static int writable_dir(const char *d) {
    char p[PATH_MAX]; snprintf(p, sizeof p, "%s/.pharrier_write_test", d);
    FILE *f = fopen(p, "wb"); if (!f) return 0; fclose(f); remove(p); return 1;
}
static void find_cfg_dir(const char *exe_dir) {
    if (writable_dir(exe_dir)) { snprintf(cfg_dir, sizeof cfg_dir, "%s", exe_dir); return; }
    const char *x = getenv("XDG_CONFIG_HOME"), *h = getenv("HOME");
    if (x && *x) snprintf(cfg_dir, sizeof cfg_dir, "%s/pharrier-recomp", x);
    else snprintf(cfg_dir, sizeof cfg_dir, "%s/.config/pharrier-recomp", h ? h : ".");
    char parent[PATH_MAX]; snprintf(parent, sizeof parent, "%s", cfg_dir);
    char *sl = strrchr(parent, '/'); if (sl) { *sl = 0; mkdir(parent, 0755); }
    mkdir(cfg_dir, 0755);
}
static void settings_load(void) {
    settings_defaults();
    char p[PATH_MAX + 32]; snprintf(p, sizeof p, "%s/pharrier.ini", cfg_dir);
    FILE *f = fopen(p, "r"); if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char key[64]; int v; if (sscanf(line, " %63[a-z_] = %d", key, &v) != 2) continue;
        if (!strcmp(key, "scale")) S.scale = v; else if (!strcmp(key, "widescreen")) S.wide = !!v; else if (!strcmp(key, "clean_edges")) S.pillarbox = !!v;
        else if (!strcmp(key, "msaa")) S.msaa = v; else if (!strcmp(key, "fullscreen")) S.fullscreen = !!v;
        else if (!strcmp(key, "stick_speed")) S.stick_speed = v; else if (!strcmp(key, "gamepad")) S.pad_mode = !!v; else if (!strcmp(key, "invert_y")) S.invert_y = !!v; else if (!strcmp(key, "language")) S.lang = v; else if (!strcmp(key, "launcher")) S.launcher = !!v;
        else for (int i = 0; i < ACT_N; i++) if (!strcmp(key, act_names[i]) && v > 0 && v < SDL_NUM_SCANCODES) S.keys[i] = v;
    }
    fclose(f);
    if (S.scale < 1 || S.scale > 8) S.scale = 2;
    if (S.msaa != 2 && S.msaa != 4 && S.msaa != 8) S.msaa = 1;
    if (S.stick_speed < 1 || S.stick_speed > 10) S.stick_speed = 5;
    if (S.lang < 0 || S.lang > 2) S.lang = 0;
}
static void settings_save(void) {
    char p[PATH_MAX + 32]; snprintf(p, sizeof p, "%s/pharrier.ini", cfg_dir);
    FILE *f = fopen(p, "w"); if (!f) return;
    fprintf(f, "; Planet Harriers Recomp (Linux) - opciones (F10 / L3+R3)\n[video]\nscale = %d\nwidescreen = %d\n; clean_edges (widescreen): 1 = black bars beside 4:3-only screens and full-width cutscene bars, 0 = stretched edges\nclean_edges = %d\nmsaa = %d\nfullscreen = %d\n\n"
               "launcher = %d\n\n[controles]\n; stick speed (keys) 1..10; gamepad: 0 off, 1 on; invert_y: 1 = up/down swapped; language: 0 auto, 1 es, 2 en\nstick_speed = %d\ngamepad = %d\ninvert_y = %d\nlanguage = %d\n\n"
               "[keys]\n; SDL scancodes (https://wiki.libsdl.org/SDL2/SDL_Scancode)\n", S.scale, S.wide, S.pillarbox, S.msaa, S.fullscreen, S.launcher, S.stick_speed, S.pad_mode, S.invert_y, S.lang);
    for (int i = 0; i < ACT_N; i++) fprintf(f, "%s = %d\n", act_names[i], S.keys[i]);
    fclose(f);
}

/* ---------------- NV (service settings, EEPROM, bookkeeping) ---------------- */
static void nv_load(void) { char p[PATH_MAX + 32]; snprintf(p, sizeof p, "%s/pharrier.nv", cfg_dir); emu_nv_load(p); }
static void nv_save(void) { char p[PATH_MAX + 32]; snprintf(p, sizeof p, "%s/pharrier.nv", cfg_dir); emu_nv_save(p); }

/* ---------------- window / Vulkan ---------------- */
static SDL_Window *win; static int running = 1, paused, fullscreen;
static int make_surface(void *inst, void *user, void *out) {
    return SDL_Vulkan_CreateSurface((SDL_Window *)user, (VkInstance)inst, (VkSurfaceKHR *)out) ? 0 : -1;
}
static void update_size(void) { int w, h; SDL_Vulkan_GetDrawableSize(win, &w, &h); vkr_resize(w, h); }
static void set_fullscreen(int on) { SDL_SetWindowFullscreen(win, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0); fullscreen = on; update_size(); }

/* ---------------- audio ---------------- */
static SDL_AudioDeviceID adev; static uint32_t aud_r;
static void audio_init(void) {
    if (getenv("PH_NOSOUND")) return;
    SDL_AudioSpec want = { 0 }, have; want.freq = 44100; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024;
    adev = SDL_OpenAudioDevice(0, 0, &want, &have, 0);
    if (adev) { SDL_PauseAudioDevice(adev, 0); aud_r = g_aud_w; }
}
static void audio_pump(void) {
    if (!adev) return;
    if (g_aud_w - aud_r > AUD_N / 2) aud_r = g_aud_w - 4096;
    uint32_t n = g_aud_w - aud_r;
    if (SDL_GetQueuedAudioSize(adev) / 4 > 6144) { aud_r += n; return; }   /* running ahead: drop */
    static int16_t buf[AUD_N * 2];
    for (uint32_t i = 0; i < n; i++, aud_r++) { buf[i * 2] = g_aud[(aud_r % AUD_N) * 2]; buf[i * 2 + 1] = g_aud[(aud_r % AUD_N) * 2 + 1]; }
    if (n) SDL_QueueAudio(adev, buf, n * 4);
}

/* ---------------- input ---------------- */
static const Uint8 *kb; static SDL_GameController *pad; static int status_frames; static char status_msg[96];
static PhStick stick;
static void status(const char *m) { snprintf(status_msg, sizeof status_msg, "%s", m); status_frames = 180; }
static void open_pad(void) {
    if (pad) return;
    for (int i = 0; i < SDL_NumJoysticks(); i++) if (SDL_IsGameController(i)) { pad = SDL_GameControllerOpen(i); if (pad) { status(T2("Mando conectado", "Gamepad connected")); return; } }
}
static float axis(Sint16 v) { float f = v / 32767.0f; if (f > 1) f = 1; if (f < -1) f = -1; float a = f < 0 ? -f : f; if (a < 0.15f) return 0; a = (a - 0.15f) / 0.85f; return f < 0 ? -a : a; }
#define K(a) kb[S.keys[a]]
#define PB(b) (pad && SDL_GameControllerGetButton(pad, b))
static void update_input(void) {
    int kx = (K(ACT_RIGHT) ? 1 : 0) - (K(ACT_LEFT) ? 1 : 0), ky = (K(ACT_UP) ? 1 : 0) - (K(ACT_DOWN) ? 1 : 0);
    float px = 0, py = 0; uint16_t p1 = 0;
    if (pad && S.pad_mode) {
        px = axis(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX)); py = -axis(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY));
        if (PB(SDL_CONTROLLER_BUTTON_DPAD_LEFT)) px = -1; if (PB(SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) px = 1;
        if (PB(SDL_CONTROLLER_BUTTON_DPAD_UP)) py = 1; if (PB(SDL_CONTROLLER_BUTTON_DPAD_DOWN)) py = -1;
        if (PB(SDL_CONTROLLER_BUTTON_A) || SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 12000) p1 |= PH_BTN_SHOT;
        if (PB(SDL_CONTROLLER_BUTTON_X) || PB(SDL_CONTROLLER_BUTTON_B) || SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 12000) p1 |= PH_BTN_MISSILE;
        if (PB(SDL_CONTROLLER_BUTTON_Y)) p1 |= PH_BTN_BOMB;
        if (PB(SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) p1 |= PH_BTN_VR_L;
        if (PB(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) p1 |= PH_BTN_VR_R;
        if (PB(SDL_CONTROLLER_BUTTON_START)) p1 |= PH_BTN_START;
    }
    ph_stick_update(&stick, kx, ky, px, py, S.stick_speed, S.invert_y);
    if (K(ACT_START)) p1 |= PH_BTN_START;
    if (K(ACT_SHOT)) p1 |= PH_BTN_SHOT;
    if (K(ACT_MISSILE)) p1 |= PH_BTN_MISSILE;
    if (K(ACT_BOMB)) p1 |= PH_BTN_BOMB;
    if (K(ACT_VR_L)) p1 |= PH_BTN_VR_L;
    if (K(ACT_VR_R)) p1 |= PH_BTN_VR_R;
    if (K(ACT_SERVICE)) p1 |= PH_BTN_SERVICE;
    g_input.p[0] = p1;
    g_input.test = K(ACT_TEST); g_input.tilt = 0; g_input.service = K(ACT_SERVICE);
}

/* ---------------- screenshots ---------------- */
static void save_screenshot(void) {
    int w, h; vkr_size(&w, &h);
    uint8_t *rgba = malloc((size_t)w * h * 4); if (!rgba) return;
    if (vkr_take_screenshot(rgba) == 0) {
        char dir[PATH_MAX + 16], p[PATH_MAX + 64]; snprintf(dir, sizeof dir, "%s/capturas", cfg_dir); mkdir(dir, 0755);
        time_t t = time(0); struct tm tm; localtime_r(&t, &tm);
        snprintf(p, sizeof p, "%s/pharrier_%04d%02d%02d_%02d%02d%02d.png", dir, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
        sr_png_rgba(p, rgba, w, h); status(T2("Captura guardada en 'capturas'", "Screenshot saved in 'capturas'"));
    }
    free(rgba);
}

/* ---------------- in-game status line ---------------- */
static void layer_hook(uint16_t *lay, uint32_t *ena, uint32_t *direct) {
    if (status_frames > 0) { status_frames--; osd_banner(lay, ena, direct, status_msg); }
}

/* ---------------- menus (ui.c): launcher and pause menu ---------------- */
static int want_restart, want_shot, restart_launcher;
static UiOptions uo; static UiInfo ui_info; static char rom_name[128];
static const char *bind_es[10] = { "Disparo", "Misil", "Bomba", "Vista izquierda", "Vista derecha", "Moneda", "Start", "Test", "Servicio", "Pausa" };
static const char *bind_en[10] = { "Shot", "Missile", "Bomb", "View left", "View right", "Coin", "Start", "Test", "Service", "Pause" };
static const int bind_act_id[10] = { ACT_SHOT, ACT_MISSILE, ACT_BOMB, ACT_VR_L, ACT_VR_R, ACT_COIN, ACT_START, ACT_TEST, ACT_SERVICE, ACT_PAUSE };
static void ui_sync_in(void) {
    uo.scale = S.scale; uo.wide = S.wide; uo.pillarbox = S.pillarbox; uo.msaa = S.msaa; uo.fullscreen = fullscreen; uo.stick_speed = S.stick_speed;
    uo.pad_mode = S.pad_mode; uo.invert_y = S.invert_y; uo.lang = S.lang; uo.region = emu_region(); uo.show_launcher = S.launcher;
    ui_info.lang_en = lang_en; ui_info.gpu = vkr_device_name(); ui_info.blocks = emu_block_count(); ui_info.msaa_gpu = vkr_msaa();
    ui_info.pad_name = pad ? SDL_GameControllerName(pad) : 0;
    if (ui_info.pad_name && !*ui_info.pad_name) ui_info.pad_name = "Gamepad";
    ui_info.nbind = 10;
    static char keyn[10][32];
    for (int i = 0; i < 10; i++) {
        ui_info.bind_act[i] = (lang_en() ? bind_en : bind_es)[i];
        snprintf(keyn[i], sizeof keyn[i], "%s", SDL_GetScancodeName((SDL_Scancode)S.keys[bind_act_id[i]])); ui_info.bind_key[i] = keyn[i];
    }
}
static void ui_show(int screen) { ui_sync_in(); ui_open(screen, &uo, &ui_info); }
static void ui_apply(int r) {   /* options changed in the menu -> settings, renderer, window */
    S.stick_speed = uo.stick_speed; S.pad_mode = uo.pad_mode; S.invert_y = uo.invert_y; S.lang = uo.lang; S.launcher = uo.show_launcher;
    if (r == UI_VIDEO) { S.scale = uo.scale; S.wide = uo.wide; S.pillarbox = uo.pillarbox; vkr_set_pillarbox(S.pillarbox); S.msaa = uo.msaa; vkr_reconfigure(S.scale, S.wide, S.msaa); ui_info.msaa_gpu = vkr_msaa(); }
    if (r == UI_FULLSCREEN) { S.fullscreen = uo.fullscreen; set_fullscreen(S.fullscreen); }
    if (uo.region != emu_region()) emu_set_region(uo.region);   /* before the game starts: the BIOS reads it at boot */
    settings_save();
}
/* one menu key from an SDL event; -1 = none */
static int ui_key_of(const SDL_Event *e) {
    if (e->type == SDL_KEYDOWN) {
        switch (e->key.keysym.scancode) {
        case SDL_SCANCODE_UP: return UI_KEY_UP; case SDL_SCANCODE_DOWN: return UI_KEY_DOWN;
        case SDL_SCANCODE_LEFT: return UI_KEY_LEFT; case SDL_SCANCODE_RIGHT: return UI_KEY_RIGHT;
        case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: case SDL_SCANCODE_SPACE: return UI_KEY_OK;
        case SDL_SCANCODE_ESCAPE: case SDL_SCANCODE_BACKSPACE: case SDL_SCANCODE_F10: return UI_KEY_BACK;
        case SDL_SCANCODE_TAB: return (e->key.keysym.mod & KMOD_SHIFT) ? UI_KEY_PREV : UI_KEY_NEXT;
        case SDL_SCANCODE_PAGEUP: case SDL_SCANCODE_Q: return UI_KEY_PREV;
        case SDL_SCANCODE_PAGEDOWN: case SDL_SCANCODE_E: return UI_KEY_NEXT;
        default: return -1;
        }
    }
    if (e->type == SDL_CONTROLLERBUTTONDOWN) {
        switch (e->cbutton.button) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP: return UI_KEY_UP; case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return UI_KEY_DOWN;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return UI_KEY_LEFT; case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return UI_KEY_RIGHT;
        case SDL_CONTROLLER_BUTTON_A: return UI_KEY_OK; case SDL_CONTROLLER_BUTTON_B: case SDL_CONTROLLER_BUTTON_START: return UI_KEY_BACK;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return UI_KEY_PREV; case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return UI_KEY_NEXT;
        default: return -1;
        }
    }
    if (e->type == SDL_CONTROLLERAXISMOTION && (e->caxis.axis == SDL_CONTROLLER_AXIS_LEFTY || e->caxis.axis == SDL_CONTROLLER_AXIS_LEFTX)) {
        static int held[2]; int ax = e->caxis.axis == SDL_CONTROLLER_AXIS_LEFTY, v = e->caxis.value, dir = v > 20000 ? 1 : v < -20000 ? -1 : 0;
        if (dir == held[ax]) return -1;
        held[ax] = dir; if (!dir) return -1;
        return ax ? (dir < 0 ? UI_KEY_UP : UI_KEY_DOWN) : (dir < 0 ? UI_KEY_LEFT : UI_KEY_RIGHT);
    }
    return -1;
}
static void ui_draw(double t) {
    static uint32_t *canvas; int w, h; vkr_ui_canvas(&w, &h);
    if (!canvas) canvas = malloc(1280 * 720 * 4);
    ui_render(canvas, w, h, t);
    vkr_ui_upload(canvas, w, h);
    static uint16_t lay[640 * 960];
    if (ui_screen() == UI_SCREEN_LAUNCHER) gfx_vk_redraw(lay, 0, 0);
    else { uint32_t ena = (uint32_t)gpu_layers(lay), dir = (uint32_t)gpu_layers_direct(); gfx_vk_redraw(lay, ena, dir); }
}

/* ---------------- main ---------------- */
static const char *find_rom(const char *arg, const char *exe_dir) {
    static char p[PATH_MAX + 64];
    if (arg) return arg;
    const char *names[2] = { "pharrier.zip", "roms/pharrier.zip" };
    for (int i = 0; i < 2; i++) { snprintf(p, sizeof p, "%s/%s", exe_dir, names[i]); if (access(p, R_OK) == 0) return p; }
    return 0;
}

int main(int argc, char **argv) {
    char exe[PATH_MAX] = "."; ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n > 0) { exe[n] = 0; char *sl = strrchr(exe, '/'); if (sl) *sl = 0; }
    const char *rom_arg = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--launcher")) restart_launcher = 1; else if (!strcmp(argv[i], "--no-launcher")) restart_launcher = -1;
        else if (argv[i][0] != '-') rom_arg = argv[i];
    }
    const char *rom = find_rom(rom_arg, exe);
    static char modpath[PATH_MAX + 32]; snprintf(modpath, sizeof modpath, "%s/libph_game.so", exe);
    if (getenv("PH_BUILDER_DUMP")) {       /* builder: run headless on the interpreter and dump the RAM of both CPUs */
        if (!rom) return 2;
        EmuConfig bc = { 0 }; bc.rom = rom; bc.hleboot = 1; bc.idle_skip = 1; bc.stop_cpu = -1; bc.prof_cpu = -1;
        { int er = emu_init(&bc); if (er) return er == HIKARU_ERR_BIOS ? 4 : er == HIKARU_ERR_GAME ? 5 : 2; }   /* 4: no BIOS, 5: no game program */
        for (int f = 0; f < 3000; f++) if (emu_run_frame() < 0) break;
        return emu_builder_dump(getenv("PH_BUILDER_DUMP")) ? 3 : 0;
    }
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    if (!rom) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Planet Harriers - Recomp",
            T2("No se encuentra pharrier.zip.\nCopia tu romset de MAME en la carpeta 'roms' o pásalo como argumento.",
               "pharrier.zip not found.\nCopy your MAME romset to the 'roms' folder or pass it as an argument."), 0);
        return 1;
    }
    find_cfg_dir(exe); settings_load();
    win = SDL_CreateWindow("Planet Harriers - Recomp", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 960,
                           SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win) { fprintf(stderr, "SDL window: %s\n", SDL_GetError()); return 1; }
    EmuConfig c = { 0 };
    c.rom = rom; c.hleboot = 1; c.aot = 1; c.idle_skip = 1; c.stop_cpu = -1; c.prof_cpu = -1; c.logpath = getenv("PH_LOG") ? "hikaru.log" : 0;
    if (access(modpath, R_OK) == 0) c.module = modpath;
    { int er = emu_init(&c); if (er) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Planet Harriers - Recomp",
            er == HIKARU_ERR_BIOS ? T2("Falta la BIOS de Hikaru.\nTu pharrier.zip no la incluye (romset partido de MAME): copia también hikaru.zip en la carpeta 'roms'.",
                                       "The Hikaru BIOS is missing.\nYour pharrier.zip does not include it (MAME split set): copy hikaru.zip to the 'roms' folder too.")
          : er == HIKARU_ERR_GAME ? T2("A pharrier.zip le faltan archivos del juego (epr-23565a / epr-23566a).", "pharrier.zip is missing game files (epr-23565a / epr-23566a).")
          : T2("No se pudo cargar la ROM.", "The ROM could not be loaded."), win);
        return 1; } }
    nv_load();
    unsigned ne = 0; SDL_Vulkan_GetInstanceExtensions(win, &ne, 0);
    const char **ext = calloc(ne + 1, sizeof *ext); SDL_Vulkan_GetInstanceExtensions(win, &ne, ext);
    VkrConfig vc = { 0 }; vc.scale = getenv("PH_SCALE") ? atoi(getenv("PH_SCALE")) : S.scale; vc.wide = S.wide; vc.msaa = S.msaa;
    vc.inst_ext = ext; vc.n_inst_ext = ne; vc.create_surface = make_surface; vc.surface_user = win;
    update_size();
    if (gfx_vk_start(&vc)) { SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Planet Harriers - Recomp", T2("No se pudo inicializar Vulkan.", "Vulkan could not be initialised."), win); return 1; }
    vkr_set_pillarbox(S.pillarbox);
    update_size();
    if (S.fullscreen || getenv("SteamDeck")) { S.fullscreen = 1; set_fullscreen(1); }
    g_vk_layer_hook = layer_hook;
    kb = SDL_GetKeyboardState(0);
    open_pad(); audio_init();
    {   const char *b = strrchr(rom, '/'); snprintf(rom_name, sizeof rom_name, "%s", b ? b + 1 : rom); }
    ui_info.version = PH_VERSION; ui_info.rom = rom_name; ui_info.platform = getenv("SteamDeck") ? "Steam Deck" : "Linux";
    /* the launcher, unless turned off (or skipped by a restart from the menu) */
    if (restart_launcher > 0 || (S.launcher && restart_launcher == 0 && !getenv("PH_NOLAUNCHER"))) ui_show(UI_SCREEN_LAUNCHER);
    Uint64 freq = SDL_GetPerformanceFrequency(), t0 = SDL_GetPerformanceCounter();
    double next = 0, fps_t = 0; long frames = 0, fps_n = 0;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            else if (e.type == SDL_WINDOWEVENT && (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED || e.window.event == SDL_WINDOWEVENT_RESIZED)) update_size();
            else if (e.type == SDL_CONTROLLERDEVICEADDED) { open_pad(); ui_info.pad_name = pad ? SDL_GameControllerName(pad) : 0; }
            else if (e.type == SDL_CONTROLLERDEVICEREMOVED && pad && e.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad))) { SDL_GameControllerClose(pad); pad = 0; status(T2("Mando desconectado", "Gamepad disconnected")); open_pad(); ui_info.pad_name = pad ? SDL_GameControllerName(pad) : 0; }
            if (ui_is_open()) {
                int k = ui_key_of(&e); if (k < 0) continue;
                int r = ui_key(k);
                switch (r) {
                case UI_START: case UI_RESUME: break;
                case UI_QUIT: running = 0; break;
                case UI_SCREENSHOT: want_shot = 2; break;
                case UI_RESET: want_restart = 1; restart_launcher = -1; running = 0; break;      /* a fresh boot, straight into the game */
                case UI_LAUNCHER: want_restart = 1; restart_launcher = 1; running = 0; break;    /* a fresh boot behind the launcher */
                case UI_REGION: ui_apply(r); want_restart = 1; restart_launcher = -1; running = 0; break;
                case UI_NONE: break;
                default: ui_apply(r); break;
                }
                if (!ui_is_open()) { vkr_ui_upload(0, 0, 0); kb = SDL_GetKeyboardState(0); }
                continue;
            }
            if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                SDL_Scancode s = e.key.keysym.scancode;
                if (s == SDL_SCANCODE_ESCAPE || s == SDL_SCANCODE_F10) ui_show(UI_SCREEN_PAUSE);
                else if (s == SDL_SCANCODE_F11) { S.fullscreen = !fullscreen; set_fullscreen(S.fullscreen); settings_save(); }
                else if (s == SDL_SCANCODE_F12) want_shot = 1;
                else if ((int)s == S.keys[ACT_PAUSE]) paused = !paused;
                if ((int)s == S.keys[ACT_COIN]) g_input.coins[0]++;
            } else if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                int b = e.cbutton.button;
                if (b == SDL_CONTROLLER_BUTTON_BACK && !PB(SDL_CONTROLLER_BUTTON_START)) g_input.coins[0]++;
                if ((b == SDL_CONTROLLER_BUTTON_LEFTSTICK && PB(SDL_CONTROLLER_BUTTON_RIGHTSTICK)) ||
                    (b == SDL_CONTROLLER_BUTTON_RIGHTSTICK && PB(SDL_CONTROLLER_BUTTON_LEFTSTICK)) || b == SDL_CONTROLLER_BUTTON_GUIDE ||
                    (b == SDL_CONTROLLER_BUTTON_START && PB(SDL_CONTROLLER_BUTTON_BACK)) || (b == SDL_CONTROLLER_BUTTON_BACK && PB(SDL_CONTROLLER_BUTTON_START)))
                    ui_show(UI_SCREEN_PAUSE);
            }
        }
        {   /* test hooks: PH_AUTOMENU=<frame> opens the menu (PH_MENUKEYS=<keys, UI_KEY_* digits>), PH_AUTOQUIT=<frame> quits */
            static long am = -2, aq = -2; if (am == -2) { am = getenv("PH_AUTOMENU") ? atol(getenv("PH_AUTOMENU")) : -1; aq = getenv("PH_AUTOQUIT") ? atol(getenv("PH_AUTOQUIT")) : -1; }
            if (frames == am && !ui_is_open()) { ui_show(UI_SCREEN_PAUSE); const char *k = getenv("PH_MENUKEYS"); for (; k && *k; k++) ui_key(*k - '0'); am = -1; }
            static long mf; if (ui_is_open()) mf++;
            if (aq >= 0 && (frames >= aq || mf > 90)) running = 0;
        }
        if (ui_is_open() || paused) {
            if (adev) SDL_PauseAudioDevice(adev, 1);
            if (ui_is_open()) ui_draw((double)(SDL_GetPerformanceCounter() - t0) / freq);
            else { static uint16_t lay[640 * 960]; uint32_t ena = (uint32_t)gpu_layers(lay), dir = (uint32_t)gpu_layers_direct();
                   osd_banner(lay, &ena, &dir, T2("PAUSA  (P para continuar)", "PAUSED  (P to resume)")); gfx_vk_redraw(lay, ena, dir); }
            {   static int shot_n; const char *sp = getenv("PH_UISHOT");   /* test hook: save the menu picture */
                if (sp && ui_is_open() && ++shot_n == 20) { vkr_request_screenshot(); gfx_vk_redraw((uint16_t[640*960]){0}, 0, 0); }
                if (sp && vkr_screenshot_ready()) { int w, h; vkr_size(&w, &h); uint8_t *px = malloc((size_t)w * h * 4); vkr_take_screenshot(px); sr_png_rgba(sp, px, w, h); free(px); } }
            SDL_Delay(16);
            next = (double)(SDL_GetPerformanceCounter() - t0) / freq; aud_r = g_aud_w;
            continue;
        }
        if (adev) SDL_PauseAudioDevice(adev, 0);
        update_input();
        if (want_shot == 1) { want_shot = 0; vkr_request_screenshot(); }
        if (want_shot == 2) want_shot = 1;     /* from the menu: next frame, without the menu on it */
        if (emu_run_frame() < 0) break;
        if (vkr_screenshot_ready()) save_screenshot();
        audio_pump();
        if (frames % 300 == 299 && emu_nv_dirty()) nv_save();
        frames++; fps_n++;
        double t = (double)(SDL_GetPerformanceCounter() - t0) / freq;
        next += 1.0 / 60.0;
        if (next > t) SDL_Delay((Uint32)((next - t) * 1000.0)); else if (t - next > 0.25) next = t;
        if (t - fps_t >= 1.0) {
            char title[160]; if (emu_block_count() > 0) snprintf(title, sizeof title, T2("Planet Harriers - Recomp   [%ld fps, %s, %d bloques recompilados]   F10/L3+R3: opciones", "Planet Harriers - Recomp   [%ld fps, %s, %d recompiled blocks]   F10/L3+R3: options"), fps_n, vkr_device_name(), emu_block_count());
            else snprintf(title, sizeof title, T2("Planet Harriers - Recomp   [%ld fps, %s, intérprete: ejecuta ./build_game.sh]", "Planet Harriers - Recomp   [%ld fps, %s, interpreter: run ./build_game.sh]"), fps_n, vkr_device_name());
            SDL_SetWindowTitle(win, title); fps_t = t; fps_n = 0;
        }
    }
    nv_save(); settings_save();
    vkr_shutdown();
    if (adev) SDL_CloseAudioDevice(adev);
    SDL_Quit();
    if (want_restart) {   /* reset, back to the launcher or country changed (the BIOS reads it at boot): boot again */
        char *nargv[4]; int n = 0; nargv[n++] = argv[0]; if (argc > 1 && argv[1][0] != '-') nargv[n++] = argv[1];
        nargv[n++] = restart_launcher > 0 ? "--launcher" : "--no-launcher"; nargv[n] = 0;
        execv("/proc/self/exe", nargv);
    }
    return 0;
}
