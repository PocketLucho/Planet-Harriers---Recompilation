/* Windows frontend: Win32 window + Vulkan swapchain, 60 Hz pacing, keyboard input.
 * Usage: PlanetHarriers-Recomp.exe [path\to\pharrier.zip]   (default: pharrier.zip next to the exe or in roms\)
 * Launcher at start (can be turned off); Esc / F10 or Back+Start on the gamepad open the menu; F9 = key bindings window.
 * Keys are configurable (saved in pharrier.ini). Defaults: 5 coin, 1 start,
 *       arrows = stick, Z shot, X missile, C bomb, A/S view change, F2 test, F1 service, P pause;
 *       F11 fullscreen, Esc quit. XInput gamepad: left stick, A/RT shot, X/B/LT missile, Y bomb, LB/RB view, Back coin.
 * Env: PH_SCALE (overrides the internal resolution), PH_LOG (write hikaru.log), PH_NOSOUND. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "emu_core.h"
#include "mie.h"
#include "gpu.h"
#include "vkrender.h"
#include "settings_win32.h"
#include "ph_input.h"
#include "swrender.h"
#include "ui.h"
#ifndef PH_VERSION
#define PH_VERSION "0.2"
#endif
#include <wchar.h>

int gfx_vk_start(const VkrConfig *cfg);
void gfx_vk_redraw(const uint16_t *lay, uint32_t ena, uint32_t direct);

static HWND hwnd; static int running = 1, fullscreen, want_settings, want_shot, paused, want_menu, want_restart, restart_launcher;
static int ui_q[32], ui_qn;   /* menu keys from the window procedure */
static char status_msg[256]; static double status_until;

/* ---- audio: waveOut fed from the emulator ring (44.1 kHz stereo, 1024-sample blocks) ---- */
#define AB_N 12
#define AB_LEN 1024
static HWAVEOUT wo; static WAVEHDR whdr[AB_N]; static int16_t abuf[AB_N][AB_LEN * 2]; static uint32_t aud_r; static int audio_on;
static void audio_init(void) {
    WAVEFORMATEX f = { WAVE_FORMAT_PCM, 2, 44100, 44100 * 4, 4, 16, 0 };
    if (getenv("PH_NOSOUND") || waveOutOpen(&wo, WAVE_MAPPER, &f, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) return;
    for (int i = 0; i < AB_N; i++) { whdr[i].lpData = (LPSTR)abuf[i]; whdr[i].dwBufferLength = AB_LEN * 4; waveOutPrepareHeader(wo, &whdr[i], sizeof whdr[i]); whdr[i].dwFlags |= WHDR_DONE; }
    aud_r = g_aud_w; audio_on = 1;
}
static void audio_pump(void) {
    if (!audio_on) return;
    int queued = 0; for (int i = 0; i < AB_N; i++) if (!(whdr[i].dwFlags & WHDR_DONE)) queued++;
    if (g_aud_w - aud_r > AUD_N / 2) aud_r = g_aud_w - AB_LEN * 4;        /* fell far behind: resync */
    while (g_aud_w - aud_r >= AB_LEN) {
        if (queued >= AB_N - 2) { aud_r += AB_LEN; continue; }           /* emulation running ahead: drop a block */
        int k = -1; for (int i = 0; i < AB_N; i++) if (whdr[i].dwFlags & WHDR_DONE) { k = i; break; }
        if (k < 0) break;
        for (int i = 0; i < AB_LEN; i++, aud_r++) { abuf[k][i * 2] = g_aud[(aud_r % AUD_N) * 2]; abuf[k][i * 2 + 1] = g_aud[(aud_r % AUD_N) * 2 + 1]; }
        whdr[k].dwFlags &= ~WHDR_DONE;
        waveOutWrite(wo, &whdr[k], sizeof whdr[k]); queued++;
    }
}
static WINDOWPLACEMENT prev_wp = { sizeof prev_wp };
static int keys[256];

static void toggle_fullscreen(void) {
    DWORD style = GetWindowLong(hwnd, GWL_STYLE);
    if (!fullscreen) {
        MONITORINFO mi = { sizeof mi };
        if (GetWindowPlacement(hwnd, &prev_wp) && GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &mi)) {
            SetWindowLong(hwnd, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
            SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                         mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        }
    } else {
        SetWindowLong(hwnd, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(hwnd, &prev_wp);
        SetWindowPos(hwnd, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
    fullscreen = !fullscreen;
}
static void toggle_fullscreen_saved(void) { toggle_fullscreen(); g_set.fullscreen = fullscreen; settings_save(); }

/* ---- XInput gamepad (loaded at runtime: xinput1_4 / 1_3 / 9_1_0) ---- */
typedef struct { WORD buttons; BYTE lt, rt; SHORT lx, ly, rx, ry; } XPad;
typedef struct { DWORD packet; XPad pad; } XState;
typedef DWORD (WINAPI *XGetState)(DWORD, XState *);
static XGetState xget; static int xtried, xconnected = -1;
static int pad_read(XPad *p) {
    if (!xtried) {
        xtried = 1;
        const char *dll[3] = { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll" };
        for (int i = 0; i < 3 && !xget; i++) { HMODULE m = LoadLibraryA(dll[i]); if (m) xget = (XGetState)(void *)GetProcAddress(m, "XInputGetState"); }
    }
    if (!xget || g_set.pad_mode == 0) return 0;
    for (DWORD i = 0; i < 4; i++) { XState s; if (xget(i, &s) == 0) { *p = s.pad; return 1; } }
    return 0;
}
static float axis(SHORT v) {   /* -1..1 with a 15% dead zone */
    float f = v / 32767.0f; if (f > 1) f = 1; if (f < -1) f = -1;
    float a = f < 0 ? -f : f; if (a < 0.15f) return 0;
    a = (a - 0.15f) / 0.85f; return f < 0 ? -a : a;
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CLOSE: running = 0; return 0;
    case WM_SIZE: vkr_resize(LOWORD(l), HIWORD(l)); return 0;
    case WM_KEYDOWN: case WM_SYSKEYDOWN:
        if (ui_is_open()) {
            int k = -1, shift = GetKeyState(VK_SHIFT) < 0;
            switch (w) { case VK_UP: k = UI_KEY_UP; break; case VK_DOWN: k = UI_KEY_DOWN; break; case VK_LEFT: k = UI_KEY_LEFT; break;
                         case VK_RIGHT: k = UI_KEY_RIGHT; break; case VK_RETURN: case VK_SPACE: k = UI_KEY_OK; break;
                         case VK_ESCAPE: case VK_BACK: case VK_F10: k = UI_KEY_BACK; break; case VK_TAB: k = shift ? UI_KEY_PREV : UI_KEY_NEXT; break;
                         case VK_PRIOR: case 'Q': k = UI_KEY_PREV; break; case VK_NEXT: case 'E': k = UI_KEY_NEXT; break; }
            if (k >= 0 && ui_qn < 32) ui_q[ui_qn++] = k;
            if (w == VK_F11 && !(l & 0x40000000)) toggle_fullscreen_saved();
            return 0;
        }
        if ((w == VK_ESCAPE || w == VK_F10) && !(l & 0x40000000)) want_menu = 1;
        if (w == VK_F9 && !(l & 0x40000000)) want_settings = 1;
        if (w == VK_F11 && !(l & 0x40000000)) toggle_fullscreen_saved();
        if ((int)w == g_set.keys[ACT_PAUSE] && !(l & 0x40000000)) paused = !paused;
        if (w == VK_F12 && !(l & 0x40000000)) want_shot = 1;
        if ((int)w == g_set.keys[ACT_COIN] && !(l & 0x40000000)) g_input.coins[0]++;
        if (w < 256) keys[w] = 1;
        return 0;                                   /* also swallows F10 / Alt so the system menu does not activate */
    case WM_KEYUP: case WM_SYSKEYUP:
        if (w < 256) keys[w] = 0;
        return 0;
    }
    return DefWindowProc(h, m, w, l);
}

static PhStick stick;
#define K(a) keys[g_set.keys[a] & 255]
static void update_input(void) {
    int kx = (K(ACT_RIGHT) ? 1 : 0) - (K(ACT_LEFT) ? 1 : 0), ky = (K(ACT_UP) ? 1 : 0) - (K(ACT_DOWN) ? 1 : 0);
    XPad p; int pad = pad_read(&p);
    if (pad != xconnected) { if (xconnected >= 0 || pad) snprintf(status_msg, sizeof status_msg, pad ? T2("Mando conectado", "Gamepad connected") : T2("Mando desconectado", "Gamepad disconnected")); xconnected = pad; }
    float px = 0, py = 0; uint16_t p1 = 0;
    if (pad) {
        px = axis(p.lx); py = axis(p.ly);
        if (p.buttons & 0x0004) px = -1; if (p.buttons & 0x0008) px = 1;       /* d-pad */
        if (p.buttons & 0x0001) py = 1;  if (p.buttons & 0x0002) py = -1;
        if ((p.buttons & 0x1000) || p.rt > 90) p1 |= PH_BTN_SHOT;              /* A / RT: trigger */
        if ((p.buttons & (0x4000 | 0x2000)) || p.lt > 90) p1 |= PH_BTN_MISSILE; /* X / B / LT: missile */
        if (p.buttons & 0x8000) p1 |= PH_BTN_BOMB;                             /* Y: bomb */
        if (p.buttons & 0x0100) p1 |= PH_BTN_VR_L;                             /* LB / RB: view change */
        if (p.buttons & 0x0200) p1 |= PH_BTN_VR_R;
        if (p.buttons & 0x0010) p1 |= PH_BTN_START;                            /* Start */
        static int back_prev, start_prev; int back = (p.buttons & 0x0020) != 0, start = (p.buttons & 0x0010) != 0;
        if ((back && start && !(back_prev && start_prev)) || ((p.buttons & 0x00c0) == 0x00c0)) want_menu = 1;   /* Back+Start or L3+R3: menu */
        else if (back && !back_prev && !start) g_input.coins[0]++;            /* Back: coin */
        back_prev = back; start_prev = start;
    }
    ph_stick_update(&stick, kx, ky, px, py, g_set.stick_speed, g_set.invert_y);
    if (K(ACT_START))   p1 |= PH_BTN_START;
    if (K(ACT_SHOT))    p1 |= PH_BTN_SHOT;
    if (K(ACT_MISSILE)) p1 |= PH_BTN_MISSILE;
    if (K(ACT_BOMB))    p1 |= PH_BTN_BOMB;
    if (K(ACT_VR_L))    p1 |= PH_BTN_VR_L;
    if (K(ACT_VR_R))    p1 |= PH_BTN_VR_R;
    if (K(ACT_SERVICE)) p1 |= PH_BTN_SERVICE;       /* service: the test menu reads the JVS player-1 service bit */
    g_input.p[0] = p1;
    g_input.test = K(ACT_TEST);
    g_input.tilt = 0;                               /* TEST only on the MIE cabinet port; see gpu.c */
    g_input.service = K(ACT_SERVICE);
}

/* ---- non-volatile memory (service-menu settings, bookkeeping): pharrier.nv in the settings folder ---- */
static void nv_path(wchar_t *p, wchar_t *tmp) { swprintf(p, MAX_PATH, L"%lspharrier.nv", settings_dir()); swprintf(tmp, MAX_PATH, L"%lspharrier.nv.tmp", settings_dir()); }
static void nv_load(void) {
    wchar_t p[MAX_PATH], t[MAX_PATH]; nv_path(p, t);
    FILE *f = _wfopen(p, L"rb"); if (!f) return;
    emu_nv_read(f); fclose(f);
}
static int nv_save(void) {
    wchar_t p[MAX_PATH], t[MAX_PATH]; nv_path(p, t);
    FILE *f = _wfopen(t, L"wb"); if (!f) return -1;
    int ok = emu_nv_write(f) == 0; if (fclose(f) != 0) ok = 0;
    if (!ok) { _wremove(t); return -1; }
    return MoveFileExW(t, p, MOVEFILE_REPLACE_EXISTING) ? 0 : -1;
}

/* ---- screenshots (F12): PNG in <settings folder>\capturas ---- */
static void save_screenshot(void) {
    int w, h; vkr_size(&w, &h);
    uint8_t *rgba = malloc((size_t)w * h * 4); if (!rgba) return;
    if (vkr_take_screenshot(rgba) == 0) {
        wchar_t dir[MAX_PATH], p[MAX_PATH]; swprintf(dir, MAX_PATH, L"%lscapturas", settings_dir()); CreateDirectoryW(dir, 0);
        SYSTEMTIME st; GetLocalTime(&st);
        swprintf(p, MAX_PATH, L"%ls\\pharrier_%04d%02d%02d_%02d%02d%02d_%03d.png", dir, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        FILE *f = _wfopen(p, L"wb");
        if (f) { sr_png_rgba_fp(f, rgba, w, h); snprintf(status_msg, sizeof status_msg, "%s", T2("Captura guardada en la carpeta 'capturas'", "Screenshot saved in the 'capturas' folder")); }
        else snprintf(status_msg, sizeof status_msg, "%s", T2("No se pudo guardar la captura", "The screenshot could not be saved"));
    }
    free(rgba);
}

/* ---- menus (ui.c) ---- */
static UiOptions uo; static UiInfo ui_info; static char rom_name[128], gpu_name[128];
static const char *bind_es[10] = { "Disparo", "Misil", "Bomba", "Vista izquierda", "Vista derecha", "Moneda", "Start", "Test", "Servicio", "Pausa" };
static const char *bind_en[10] = { "Shot", "Missile", "Bomb", "View left", "View right", "Coin", "Start", "Test", "Service", "Pause" };
static const int bind_id[10] = { ACT_SHOT, ACT_MISSILE, ACT_BOMB, ACT_VR_L, ACT_VR_R, ACT_COIN, ACT_START, ACT_TEST, ACT_SERVICE, ACT_PAUSE };
static void key_name(int vk, char *out, int n) {
    UINT sc = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC); LONG lp = (LONG)(sc << 16);
    if (vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN || vk == VK_PRIOR || vk == VK_NEXT || vk == VK_INSERT || vk == VK_DELETE || vk == VK_HOME || vk == VK_END) lp |= 1 << 24;
    if (!GetKeyNameTextA(lp, out, n)) snprintf(out, n, "0x%02X", vk);
}
static void ui_show(int screen) {
    uo.scale = g_set.scale; uo.wide = g_set.wide; uo.pillarbox = g_set.pillarbox; uo.backface_cull = g_set.cull; uo.msaa = g_set.msaa; uo.fullscreen = fullscreen; uo.stick_speed = g_set.stick_speed;
    uo.pad_mode = g_set.pad_mode; uo.invert_y = g_set.invert_y; uo.lang = g_set.lang; uo.region = emu_region(); uo.show_launcher = g_set.launcher;
    ui_info.version = PH_VERSION; ui_info.platform = "Windows"; ui_info.rom = rom_name; ui_info.lang_en = ph_lang_en;
    snprintf(gpu_name, sizeof gpu_name, "%s", vkr_device_name()); ui_info.gpu = gpu_name;
    ui_info.blocks = emu_block_count(); ui_info.msaa_gpu = vkr_msaa();
    XPad p; ui_info.pad_name = pad_read(&p) ? "XInput" : 0;
    static char kn[10][32]; ui_info.nbind = 10;
    for (int i = 0; i < 10; i++) { ui_info.bind_act[i] = (ph_lang_en() ? bind_en : bind_es)[i]; key_name(g_set.keys[bind_id[i]], kn[i], 32); ui_info.bind_key[i] = kn[i]; }
    ui_qn = 0; ui_open(screen, &uo, &ui_info);
}
static void ui_apply(int r) {
    g_set.stick_speed = uo.stick_speed; g_set.pad_mode = uo.pad_mode; g_set.invert_y = uo.invert_y; g_set.lang = uo.lang; g_set.launcher = uo.show_launcher;
    if (r == UI_VIDEO) { g_set.scale = uo.scale; g_set.wide = uo.wide; g_set.pillarbox = uo.pillarbox; vkr_set_pillarbox(g_set.pillarbox); g_set.cull = uo.backface_cull; g_gfx_backface_cull = g_set.cull; g_set.msaa = uo.msaa; vkr_reconfigure(g_set.scale, g_set.wide, g_set.msaa); ui_info.msaa_gpu = vkr_msaa(); }
    if (r == UI_FULLSCREEN && !!uo.fullscreen != !!fullscreen) toggle_fullscreen();
    g_set.fullscreen = fullscreen;
    if (uo.region != emu_region()) { emu_set_region(uo.region); g_set.region = uo.region; }
    settings_save();
}
/* gamepad in the menus: d-pad / stick, A, B, LB/RB, with repeat */
static void ui_pad_poll(void) {
    XPad p; if (!pad_read(&p)) return;
    static WORD prev; static int prev_dir[2]; static DWORD rep_t;
    WORD b = p.buttons, down = b & ~prev; prev = b;
    int dy = (b & 1) || p.ly > 20000 ? -1 : (b & 2) || p.ly < -20000 ? 1 : 0, dx = (b & 4) || p.lx < -20000 ? -1 : (b & 8) || p.lx > 20000 ? 1 : 0;
    DWORD now = GetTickCount();
    if (dy != prev_dir[0] || (dy && now - rep_t > 180)) { if (dy) { ui_q[ui_qn++] = dy < 0 ? UI_KEY_UP : UI_KEY_DOWN; rep_t = now + (dy != prev_dir[0] ? 220 : 0); } prev_dir[0] = dy; }
    if (dx != prev_dir[1]) { if (dx) ui_q[ui_qn++] = dx < 0 ? UI_KEY_LEFT : UI_KEY_RIGHT; prev_dir[1] = dx; }
    if (down & 0x1000) ui_q[ui_qn++] = UI_KEY_OK;
    if (down & (0x2000 | 0x0010)) ui_q[ui_qn++] = UI_KEY_BACK;
    if (down & 0x0100) ui_q[ui_qn++] = UI_KEY_PREV;
    if (down & 0x0200) ui_q[ui_qn++] = UI_KEY_NEXT;
    if (ui_qn > 28) ui_qn = 28;
}
static void ui_draw(double t) {
    static uint32_t *canvas; int w, h; vkr_ui_canvas(&w, &h);
    if (!canvas) canvas = malloc(1280 * 720 * 4);
    ui_render(canvas, w, h, t); vkr_ui_upload(canvas, w, h);
    static uint16_t lay[640 * 960];
    if (ui_screen() == UI_SCREEN_LAUNCHER) gfx_vk_redraw(lay, 0, 0);
    else { uint32_t ena = (uint32_t)gpu_layers(lay), dir = (uint32_t)gpu_layers_direct(); gfx_vk_redraw(lay, ena, dir); }
}

static const char *find_rom(const char *arg) {
    static char path[MAX_PATH];
    if (arg && *arg) return arg;
    char exe[MAX_PATH]; GetModuleFileNameA(0, exe, MAX_PATH);
    char *s = strrchr(exe, '\\'); if (s) s[1] = 0;
    const char *names[] = { "pharrier.zip", "roms\\pharrier.zip" };
    for (int i = 0; i < 2; i++) {
        snprintf(path, sizeof path, "%s%s", exe, names[i]);
        if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) return path;
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR cmd, int show) {
    (void)hp;
    char arg[MAX_PATH] = { 0 };
    if (cmd && *cmd) {
        snprintf(arg, sizeof arg, "%s", cmd);
        char *f; if ((f = strstr(arg, "--no-launcher"))) { restart_launcher = -1; memset(f, ' ', 13); } else if ((f = strstr(arg, "--launcher"))) { restart_launcher = 1; memset(f, ' ', 10); }
        size_t n = strlen(arg); while (n && arg[n - 1] == ' ') arg[--n] = 0;
        while (arg[0] == ' ') memmove(arg, arg + 1, n--);
        if (n && arg[0] == '"') { memmove(arg, arg + 1, n); n--; if (n && arg[n - 1] == '"') arg[n - 1] = 0; }
    }
    const char *rom = find_rom(arg);
    if (!rom) {
        settings_load();
        MessageBoxW(0, T2(L"No se encuentra pharrier.zip.\nCopia tu romset de MAME en la carpeta 'roms' (o junto al .exe), o arrastra el zip sobre el .exe.",
                          L"pharrier.zip not found.\nCopy your MAME romset to the 'roms' folder (or next to the .exe), or drag the zip onto the .exe."),
                    L"Planet Harriers - Recomp", MB_ICONERROR);
        return 1;
    }
    /* game module: the recompiled blocks built by CONSTRUIR_JUEGO.bat / BUILD_GAME.bat from the user's ROM */
    static char modpath[MAX_PATH]; { GetModuleFileNameA(0, modpath, MAX_PATH); char *s = strrchr(modpath, '\\'); if (s) s[1] = 0; strncat(modpath, "ph_game.dll", MAX_PATH - strlen(modpath) - 1); }
    if (getenv("PH_BUILDER_DUMP")) {       /* builder: run the game headless on the interpreter and dump the RAM of both CPUs */
        EmuConfig bc = { 0 }; bc.rom = rom; bc.hleboot = 1; bc.idle_skip = 1; bc.stop_cpu = -1; bc.prof_cpu = -1;
        { int er = emu_init(&bc); if (er) return er == HIKARU_ERR_BIOS ? 4 : er == HIKARU_ERR_GAME ? 5 : 2; }   /* 4: no BIOS, 5: no game program */
        for (int f = 0; f < 3000; f++) if (emu_run_frame() < 0) break;
        return emu_builder_dump(getenv("PH_BUILDER_DUMP")) ? 3 : 0;
    }
    WNDCLASSA wc = { 0 }; wc.style = CS_OWNDC; wc.lpfnWndProc = wndproc; wc.hInstance = hi; wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.lpszClassName = "pharrier"; wc.hIcon = LoadIcon(hi, MAKEINTRESOURCE(1));
    RegisterClassA(&wc);
    RECT r = { 0, 0, 1280, 960 }; AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    hwnd = CreateWindowA("pharrier", "Planet Harriers - Recomp", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                         r.right - r.left, r.bottom - r.top, 0, 0, hi, 0);
    ShowWindow(hwnd, show);

    settings_load();
    EmuConfig c = { 0 };
    c.rom = rom; c.hleboot = 1; c.aot = 1; c.idle_skip = 1; c.stop_cpu = -1; c.prof_cpu = -1;
    if (GetFileAttributesA(modpath) != INVALID_FILE_ATTRIBUTES) c.module = modpath;
    c.logpath = getenv("PH_LOG") ? "hikaru.log" : 0;
    { int er = emu_init(&c); if (er) {
        MessageBoxW(hwnd, er == HIKARU_ERR_BIOS ? T2(L"Falta la BIOS de Hikaru.\nTu pharrier.zip no la incluye (romset partido de MAME): copia también hikaru.zip en la carpeta 'roms' (o junto al .exe).",
                                                     L"The Hikaru BIOS is missing.\nYour pharrier.zip does not include it (MAME split set): copy hikaru.zip to the 'roms' folder (or next to the .exe) too.")
                        : er == HIKARU_ERR_GAME ? T2(L"A pharrier.zip le faltan archivos del juego (epr-23565a / epr-23566a).", L"pharrier.zip is missing game files (epr-23565a / epr-23566a).")
                        : T2(L"No se pudo cargar la ROM.", L"The ROM could not be loaded."), L"Planet Harriers - Recomp", MB_ICONERROR);
        return 1; } }
    nv_load();
    VkrConfig vc = { 0 }; vc.scale = getenv("PH_SCALE") ? atoi(getenv("PH_SCALE")) : g_set.scale; vc.vsync = 0; vc.wide = g_set.wide; vc.msaa = g_set.msaa;
    vc.win32_hinstance = hi; vc.win32_hwnd = hwnd;
    { RECT cr; GetClientRect(hwnd, &cr); vkr_resize(cr.right, cr.bottom); }
    if (gfx_vk_start(&vc)) {
        MessageBoxW(hwnd, T2(L"No se pudo inicializar Vulkan.\nActualiza el driver de la tarjeta gráfica.", L"Vulkan could not be initialised.\nUpdate your graphics driver."), L"Planet Harriers - Recomp", MB_ICONERROR);
        return 1;
    }
    vkr_set_pillarbox(g_set.pillarbox); g_gfx_backface_cull = g_set.cull;
    { RECT cr; GetClientRect(hwnd, &cr); vkr_resize(cr.right, cr.bottom); }
    if (g_set.fullscreen) toggle_fullscreen();
    { const char *b = strrchr(rom, '\\'); if (!b) b = strrchr(rom, '/'); snprintf(rom_name, sizeof rom_name, "%s", b ? b + 1 : rom); }
    if (restart_launcher > 0 || (g_set.launcher && restart_launcher == 0)) ui_show(UI_SCREEN_LAUNCHER);

    LARGE_INTEGER freq, t0, now; QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&t0);
    timeBeginPeriod(1);
    audio_init();
    const double frame_s = 1.0 / 60.0; double next = 0; long frames = 0; double fps_t = 0; long fps_n = 0;
    while (running) {
        MSG msg;
        while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); }
        if (want_menu && !ui_is_open()) { want_menu = 0; ui_show(UI_SCREEN_PAUSE); }
        if (ui_is_open()) {
            if (audio_on) waveOutPause(wo);
            ui_pad_poll();
            for (int i = 0; i < ui_qn && ui_is_open(); i++) {
                int r = ui_key(ui_q[i]);
                switch (r) {
                case UI_NONE: case UI_START: case UI_RESUME: break;
                case UI_QUIT: running = 0; break;
                case UI_SCREENSHOT: want_shot = 2; break;
                case UI_RESET: want_restart = 1; restart_launcher = -1; running = 0; break;
                case UI_LAUNCHER: want_restart = 1; restart_launcher = 1; running = 0; break;
                case UI_REGION: ui_apply(r); want_restart = 1; restart_launcher = -1; running = 0; break;
                default: ui_apply(r); break;
                }
            }
            ui_qn = 0;
            if (!ui_is_open() || !running) {
                vkr_ui_upload(0, 0, 0); memset(keys, 0, sizeof keys); want_menu = 0;
                if (audio_on) waveOutRestart(wo);
                QueryPerformanceCounter(&now); next = (double)(now.QuadPart - t0.QuadPart) / freq.QuadPart; aud_r = g_aud_w;
                continue;
            }
            QueryPerformanceCounter(&now); ui_draw((double)(now.QuadPart - t0.QuadPart) / freq.QuadPart);
            Sleep(15);
            continue;
        }
        if (want_settings) {                         /* F10: game paused while the window is open */
            want_settings = 0;
            if (audio_on) waveOutPause(wo);
            int restart = 0;
            if (settings_dialog(hi, hwnd, &restart)) {
                vkr_reconfigure(g_set.scale, g_set.wide, g_set.msaa);
                if (!!g_set.fullscreen != !!fullscreen) toggle_fullscreen();
                char d[MAX_PATH * 2]; WideCharToMultiByte(CP_UTF8, 0, settings_dir(), -1, d, sizeof d, 0, 0);
                snprintf(status_msg, sizeof status_msg, T2("Opciones guardadas en %spharrier.ini", "Options saved to %spharrier.ini"), d);
                if (restart) {   /* country changed: the BIOS reads it at boot -> save and start again */
                    nv_save();
                    wchar_t exe[MAX_PATH]; GetModuleFileNameW(0, exe, MAX_PATH);
                    STARTUPINFOW si = { sizeof si }; PROCESS_INFORMATION pi;
                    if (CreateProcessW(exe, GetCommandLineW(), 0, 0, FALSE, 0, 0, 0, &si, &pi)) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); running = 0; }
                }
            }
            if (audio_on) waveOutRestart(wo);
            memset(keys, 0, sizeof keys);
            QueryPerformanceCounter(&now); next = (double)(now.QuadPart - t0.QuadPart) / freq.QuadPart;
            aud_r = g_aud_w;
            continue;
        }
        static int was_paused;
        if (paused) {                                /* P: pause (audio stopped, last image stays) */
            if (!was_paused) { was_paused = 1; if (audio_on) waveOutPause(wo); SetWindowTextW(hwnd, T2(L"Planet Harriers - Recomp   [PAUSA]   P: continuar", L"Planet Harriers - Recomp   [PAUSED]   P: resume")); }
            Sleep(15);
            continue;
        }
        if (was_paused) {
            was_paused = 0; if (audio_on) waveOutRestart(wo);
            QueryPerformanceCounter(&now); next = (double)(now.QuadPart - t0.QuadPart) / freq.QuadPart; aud_r = g_aud_w;
        }
        update_input();
        if (want_shot == 1) { want_shot = 0; vkr_request_screenshot(); }
        if (want_shot == 2) want_shot = 1;           /* from the menu: next frame, without the menu on it */
        if (emu_run_frame() < 0) break;
        if (vkr_screenshot_ready()) save_screenshot();
        audio_pump();
        if (frames % 300 == 299 && emu_nv_dirty()) nv_save();   /* service-menu settings survive a crash */
        frames++; fps_n++;
        QueryPerformanceCounter(&now);
        double t = (double)(now.QuadPart - t0.QuadPart) / freq.QuadPart;
        next += frame_s;
        if (next > t) { DWORD ms = (DWORD)((next - t) * 1000.0); if (ms) Sleep(ms); }
        else if (t - next > 0.25) next = t;          /* too slow: don't try to catch up forever */
        if (status_msg[0] && status_until == 0) status_until = t + 4.0;
        if (status_msg[0] && t > status_until) { status_msg[0] = 0; status_until = 0; }
        if (t - fps_t >= 1.0 || (status_msg[0] && status_until == t + 4.0)) {
            char title[512];
            if (status_msg[0]) snprintf(title, sizeof title, "Planet Harriers - Recomp   [%ld fps]   %s", fps_n, status_msg);
            else if (emu_block_count() > 0) snprintf(title, sizeof title, T2("Planet Harriers - Recomp   [%ld fps, %s, %d bloques recompilados]   Esc: menú  F12: captura",
                                                             "Planet Harriers - Recomp   [%ld fps, %s, %d recompiled blocks]   Esc: menu  F12: screenshot"), fps_n, vkr_device_name(), emu_block_count());
            else snprintf(title, sizeof title, T2("Planet Harriers - Recomp   [%ld fps, %s, intérprete: ejecuta CONSTRUIR_JUEGO.bat]   Esc: menú",
                                                  "Planet Harriers - Recomp   [%ld fps, %s, interpreter: run BUILD_GAME.bat]   Esc: menu"), fps_n, vkr_device_name());
            wchar_t wt[512]; MultiByteToWideChar(CP_UTF8, 0, title, -1, wt, 512); SetWindowTextW(hwnd, wt);
            if (t - fps_t >= 1.0) { fps_t = t; fps_n = 0; }
        }
    }
    timeEndPeriod(1);
    nv_save();
    if (audio_on) { waveOutReset(wo); for (int i = 0; i < AB_N; i++) waveOutUnprepareHeader(wo, &whdr[i], sizeof whdr[i]); waveOutClose(wo); }
    vkr_shutdown();
    if (want_restart) {   /* reset, back to the launcher or country changed: start again */
        wchar_t exe[MAX_PATH], cl[MAX_PATH * 2 + 64]; GetModuleFileNameW(0, exe, MAX_PATH);
        swprintf(cl, MAX_PATH * 2 + 64, L"\"%ls\" %ls\"%hs\"", exe, restart_launcher > 0 ? L"--launcher " : L"--no-launcher ", rom);
        STARTUPINFOW si = { sizeof si }; PROCESS_INFORMATION pi;
        if (CreateProcessW(exe, cl, 0, 0, FALSE, 0, 0, 0, &si, &pi)) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
    }
    return 0;
}
