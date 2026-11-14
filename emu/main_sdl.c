/* Linux / Steam Deck frontend: SDL2 window + Vulkan, SDL audio, keyboard and gamepad, in-game menu.
 * Usage: ./pharrier [path/to/pharrier.zip]   (default: pharrier.zip next to the program or in roms/)
 * Menu: F10 or L3+R3 on the gamepad (resolution, MSAA, 16:9, fullscreen, stick speed, gamepad, country).
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

int gfx_vk_start(const VkrConfig *cfg);
void gfx_vk_redraw(const uint16_t *lay, uint32_t ena, uint32_t direct);
extern void (*g_vk_layer_hook)(uint16_t *lay, uint32_t *ena, uint32_t *direct);

/* ---------------- settings ---------------- */
enum { ACT_UP, ACT_DOWN, ACT_LEFT, ACT_RIGHT, ACT_SHOT, ACT_MISSILE, ACT_BOMB, ACT_VR_L, ACT_VR_R,
       ACT_START, ACT_COIN, ACT_TEST, ACT_SERVICE, ACT_PAUSE, ACT_N };
static const char *act_names[ACT_N] = { "up", "down", "left", "right", "shot", "missile", "bomb", "view_left", "view_right",
    "start", "coin", "test", "service", "pause" };
static struct { int scale, wide, msaa, fullscreen, stick_speed, pad_mode, invert_y, lang; int keys[ACT_N]; } S;
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
    S.scale = 2; S.wide = 0; S.msaa = 1; S.fullscreen = 0; S.stick_speed = 5; S.pad_mode = 1; S.invert_y = 0;
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
        if (!strcmp(key, "scale")) S.scale = v; else if (!strcmp(key, "widescreen")) S.wide = !!v;
        else if (!strcmp(key, "msaa")) S.msaa = v; else if (!strcmp(key, "fullscreen")) S.fullscreen = !!v;
        else if (!strcmp(key, "stick_speed")) S.stick_speed = v; else if (!strcmp(key, "gamepad")) S.pad_mode = !!v; else if (!strcmp(key, "invert_y")) S.invert_y = !!v; else if (!strcmp(key, "language")) S.lang = v;
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
    fprintf(f, "; Planet Harriers Recomp (Linux) - opciones (F10 / L3+R3)\n[video]\nscale = %d\nwidescreen = %d\nmsaa = %d\nfullscreen = %d\n\n"
               "[controles]\n; stick speed (keys) 1..10; gamepad: 0 off, 1 on; invert_y: 1 = up/down swapped; language: 0 auto, 1 es, 2 en\nstick_speed = %d\ngamepad = %d\ninvert_y = %d\nlanguage = %d\n\n"
               "[keys]\n; SDL scancodes (https://wiki.libsdl.org/SDL2/SDL_Scancode)\n", S.scale, S.wide, S.msaa, S.fullscreen, S.stick_speed, S.pad_mode, S.invert_y, S.lang);
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

/* ---------------- menu ---------------- */
static const char *region_es[5] = { "Japón", "EE. UU.", "Exportación (Europa)", "Australia", "Corea" };
static const char *region_en[5] = { "Japan", "USA", "Export (Europe)", "Australia", "Korea" };
#define region_names (lang_en() ? region_en : region_es)
enum { M_CONT, M_SCALE, M_MSAA, M_WIDE, M_FULL, M_SPEED, M_PAD, M_INVY, M_LANG, M_REGION, M_SHOT, M_QUIT, M_N };
static int menu_open, menu_sel, menu_region, want_restart, want_shot;
static int msaa_next(int m, int dir) { static const int v[4] = { 1, 2, 4, 8 }; int i = m == 8 ? 3 : m == 4 ? 2 : m == 2 ? 1 : 0; i = (i + dir + 4) % 4; return v[i]; }
static void menu_change(int dir) {
    switch (menu_sel) {
    case M_SCALE: S.scale = (S.scale - 1 + dir + 8) % 8 + 1; break;
    case M_MSAA: S.msaa = msaa_next(S.msaa, dir); break;
    case M_WIDE: S.wide = !S.wide; break;
    case M_FULL: S.fullscreen = !S.fullscreen; set_fullscreen(S.fullscreen); break;
    case M_SPEED: S.stick_speed = (S.stick_speed - 1 + dir + 10) % 10 + 1; break;
    case M_PAD: S.pad_mode = !S.pad_mode; break;
    case M_INVY: S.invert_y = !S.invert_y; break;
    case M_LANG: S.lang = (S.lang + dir + 3) % 3; break;
    case M_REGION: menu_region = (menu_region + dir + 5) % 5; break;
    default: return;
    }
    if (menu_sel == M_SCALE || menu_sel == M_MSAA || menu_sel == M_WIDE) vkr_reconfigure(S.scale, S.wide, S.msaa);
    settings_save();
}
static void menu_accept(void) {
    switch (menu_sel) {
    case M_CONT: menu_open = 0; break;
    case M_REGION: if (menu_region != emu_region()) { emu_set_region(menu_region); want_restart = 1; running = 0; } break;
    case M_SHOT: menu_open = 0; want_shot = 2; break;
    case M_QUIT: running = 0; break;
    default: menu_change(1); break;
    }
}
static void menu_draw(void) {
    static uint16_t lay[640 * 960];
    uint32_t ena = (uint32_t)gpu_layers(lay), dir = (uint32_t)gpu_layers_direct();
    OsdItem it[M_N]; memset(it, 0, sizeof it);
    const char *labels_es[M_N] = { "Continuar", "Resolución interna", "Antialiasing (MSAA)", "Panorámico 16:9", "Pantalla completa",
                                   "Velocidad palanca (teclas)", "Mando", "Invertir arriba/abajo", "Idioma / Language", "País / región (reinicia)",
                                   "Captura de pantalla", "Salir" };
    const char *labels_en[M_N] = { "Continue", "Internal resolution", "Antialiasing (MSAA)", "Widescreen 16:9", "Fullscreen",
                                   "Stick speed (keys)", "Gamepad", "Invert up/down", "Idioma / Language", "Country / region (restarts)",
                                   "Screenshot", "Quit" };
    const char **labels = lang_en() ? labels_en : labels_es;
    for (int i = 0; i < M_N; i++) { it[i].label = labels[i]; it[i].kind = OSD_ACTION; }
    for (int i = M_SCALE; i <= M_REGION; i++) it[i].kind = OSD_CHOICE;
    snprintf(it[M_SCALE].value, 48, "%dx (%dx%d)", S.scale, (S.wide ? 854 : 640) * S.scale, 480 * S.scale);
    snprintf(it[M_MSAA].value, 48, "%s", S.msaa > 1 ? (S.msaa == 2 ? "2x" : S.msaa == 4 ? "4x" : "8x") : "No");
    snprintf(it[M_LANG].value, 48, "%s", S.lang == 0 ? "Auto" : S.lang == 1 ? "Español" : "English");
    if (S.msaa > 1 && vkr_msaa() != S.msaa) snprintf(it[M_MSAA].value, 48, "%dx (GPU: %dx)", S.msaa, vkr_msaa());
    snprintf(it[M_WIDE].value, 48, "%s", S.wide ? T2("Sí", "Yes") : "No");
    snprintf(it[M_FULL].value, 48, "%s", S.fullscreen ? T2("Sí", "Yes") : "No");
    snprintf(it[M_SPEED].value, 48, "%d", S.stick_speed);
    snprintf(it[M_INVY].value, 48, "%s", S.invert_y ? T2("Sí", "Yes") : "No");
    snprintf(it[M_PAD].value, 48, "%s", S.pad_mode ? T2("Activado", "On") : T2("Desactivado", "Off"));
    snprintf(it[M_REGION].value, 48, "%s", region_names[menu_region]);
    osd_menu(lay, &ena, &dir, T2("PLANET HARRIERS - OPCIONES", "PLANET HARRIERS - OPTIONS"), it, M_N, menu_sel,
             T2("Arriba/Abajo elegir  Izq/Der cambiar  A aceptar  B volver", "Up/Down select  Left/Right change  A accept  B back"));
    gfx_vk_redraw(lay, ena, dir);
}
static void menu_event(const SDL_Event *e) {
    int up = 0, down = 0, left = 0, right = 0, ok = 0, back = 0;
    if (e->type == SDL_KEYDOWN) {
        SDL_Scancode s = e->key.keysym.scancode;
        up = s == SDL_SCANCODE_UP; down = s == SDL_SCANCODE_DOWN; left = s == SDL_SCANCODE_LEFT; right = s == SDL_SCANCODE_RIGHT;
        ok = s == SDL_SCANCODE_RETURN || s == SDL_SCANCODE_SPACE; back = s == SDL_SCANCODE_ESCAPE || s == SDL_SCANCODE_F10 || s == SDL_SCANCODE_BACKSPACE;
    } else if (e->type == SDL_CONTROLLERBUTTONDOWN) {
        int b = e->cbutton.button;
        up = b == SDL_CONTROLLER_BUTTON_DPAD_UP; down = b == SDL_CONTROLLER_BUTTON_DPAD_DOWN;
        left = b == SDL_CONTROLLER_BUTTON_DPAD_LEFT; right = b == SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
        ok = b == SDL_CONTROLLER_BUTTON_A; back = b == SDL_CONTROLLER_BUTTON_B || b == SDL_CONTROLLER_BUTTON_START;
    } else return;
    if (up) menu_sel = (menu_sel + M_N - 1) % M_N;
    if (down) menu_sel = (menu_sel + 1) % M_N;
    if (left) menu_change(-1);
    if (right) menu_change(1);
    if (ok) menu_accept();
    if (back) menu_open = 0;
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
    const char *rom = find_rom(argc > 1 ? argv[1] : 0, exe);
    static char modpath[PATH_MAX + 32]; snprintf(modpath, sizeof modpath, "%s/libph_game.so", exe);
    if (getenv("PH_BUILDER_DUMP")) {       /* builder: run headless on the interpreter and dump the RAM of both CPUs */
        if (!rom) return 2;
        EmuConfig bc = { 0 }; bc.rom = rom; bc.hleboot = 1; bc.idle_skip = 1; bc.stop_cpu = -1; bc.prof_cpu = -1;
        if (emu_init(&bc)) return 2;
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
    if (emu_init(&c)) { SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Planet Harriers - Recomp", T2("No se pudo cargar la ROM.", "The ROM could not be loaded."), win); return 1; }
    nv_load();
    unsigned ne = 0; SDL_Vulkan_GetInstanceExtensions(win, &ne, 0);
    const char **ext = calloc(ne + 1, sizeof *ext); SDL_Vulkan_GetInstanceExtensions(win, &ne, ext);
    VkrConfig vc = { 0 }; vc.scale = getenv("PH_SCALE") ? atoi(getenv("PH_SCALE")) : S.scale; vc.wide = S.wide; vc.msaa = S.msaa;
    vc.inst_ext = ext; vc.n_inst_ext = ne; vc.create_surface = make_surface; vc.surface_user = win;
    update_size();
    if (gfx_vk_start(&vc)) { SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Planet Harriers - Recomp", T2("No se pudo inicializar Vulkan.", "Vulkan could not be initialised."), win); return 1; }
    update_size();
    if (S.fullscreen || getenv("SteamDeck")) { S.fullscreen = 1; set_fullscreen(1); }
    g_vk_layer_hook = layer_hook;
    kb = SDL_GetKeyboardState(0);
    open_pad(); audio_init();
    Uint64 freq = SDL_GetPerformanceFrequency(), t0 = SDL_GetPerformanceCounter();
    double next = 0, fps_t = 0; long frames = 0, fps_n = 0;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            else if (e.type == SDL_WINDOWEVENT && (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED || e.window.event == SDL_WINDOWEVENT_RESIZED)) update_size();
            else if (e.type == SDL_CONTROLLERDEVICEADDED) open_pad();
            else if (e.type == SDL_CONTROLLERDEVICEREMOVED && pad && e.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad))) { SDL_GameControllerClose(pad); pad = 0; status(T2("Mando desconectado", "Gamepad disconnected")); open_pad(); }
            if (menu_open) { menu_event(&e); continue; }
            if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                SDL_Scancode s = e.key.keysym.scancode;
                if (s == SDL_SCANCODE_ESCAPE) running = 0;
                else if (s == SDL_SCANCODE_F11) { S.fullscreen = !fullscreen; set_fullscreen(S.fullscreen); settings_save(); }
                else if (s == SDL_SCANCODE_F10) { menu_open = 1; menu_sel = 0; menu_region = emu_region(); }
                else if (s == SDL_SCANCODE_F12) want_shot = 1;
                else if ((int)s == S.keys[ACT_PAUSE]) paused = !paused;
                if ((int)s == S.keys[ACT_COIN]) g_input.coins[0]++;
            } else if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                int b = e.cbutton.button;
                if (b == SDL_CONTROLLER_BUTTON_BACK) g_input.coins[0]++;
                if ((b == SDL_CONTROLLER_BUTTON_LEFTSTICK && PB(SDL_CONTROLLER_BUTTON_RIGHTSTICK)) ||
                    (b == SDL_CONTROLLER_BUTTON_RIGHTSTICK && PB(SDL_CONTROLLER_BUTTON_LEFTSTICK)) || b == SDL_CONTROLLER_BUTTON_GUIDE) {
                    menu_open = 1; menu_sel = 0; menu_region = emu_region();
                }
            }
        }
        {   /* test hooks: PH_AUTOMENU=<frame> opens the menu (PH_MENUSEL=<item>), PH_AUTOQUIT=<frame> quits */
            static long am = -2, aq = -2; if (am == -2) { am = getenv("PH_AUTOMENU") ? atol(getenv("PH_AUTOMENU")) : -1; aq = getenv("PH_AUTOQUIT") ? atol(getenv("PH_AUTOQUIT")) : -1; }
            if (frames == am && !menu_open) { menu_open = 1; menu_sel = getenv("PH_MENUSEL") ? atoi(getenv("PH_MENUSEL")) : 0; menu_region = emu_region(); am = -1; }
            static long mf; if (menu_open) mf++;
            if (aq >= 0 && (frames >= aq || mf > 60)) running = 0;
        }
        if (menu_open || paused) {
            if (adev) SDL_PauseAudioDevice(adev, 1);
            if (menu_open) menu_draw();
            else { static uint16_t lay[640 * 960]; uint32_t ena = (uint32_t)gpu_layers(lay), dir = (uint32_t)gpu_layers_direct();
                   osd_banner(lay, &ena, &dir, T2("PAUSA  (P para continuar)", "PAUSED  (P to resume)")); gfx_vk_redraw(lay, ena, dir); }
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
    if (want_restart) execv("/proc/self/exe", argv);   /* country changed: the BIOS reads it at boot */
    return 0;
}
