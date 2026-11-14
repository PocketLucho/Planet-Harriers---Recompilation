/* Windows frontend settings (pharrier.ini next to the exe) and the modal settings window (F10):
 * video (internal resolution, MSAA, 16:9, fullscreen), game (country), stick and gamepad, key bindings. */
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

enum {
    ACT_UP, ACT_DOWN, ACT_LEFT, ACT_RIGHT,     /* analog stick (the keys move it gradually) */
    ACT_SHOT, ACT_MISSILE, ACT_BOMB,           /* trigger (bullet), lock-on missile, bomb */
    ACT_VR_L, ACT_VR_R,                        /* view-change buttons */
    ACT_START, ACT_COIN, ACT_TEST, ACT_SERVICE, ACT_PAUSE,
    ACT_N
};

typedef struct {
    int scale;          /* internal resolution multiplier, 1..8 */
    int wide;           /* 16:9 */
    int msaa;           /* antialiasing samples 1 (off), 2, 4, 8 */
    int fullscreen;     /* start in fullscreen (F11 toggles and remembers) */
    int stick_speed;    /* how fast the keys move the stick, 1..10 */
    int pad_mode;       /* gamepad: 0 off, 1 on */
    int invert_y;       /* 1 = up and down swapped (flight-stick style) */
    int lang;           /* interface language: 0 follow Windows, 1 Spanish, 2 English */
    int region;         /* country written to the EEPROM: 0 Japan, 1 USA, 2 Export, 3 Australia, 4 Korea */
    int keys[ACT_N];    /* virtual-key codes */
} Settings;

extern Settings g_set;
extern const char *g_act_names[ACT_N];

void settings_defaults(Settings *s);
void settings_load(void);
int settings_save(void);                 /* 0 = ok */
const wchar_t *settings_dir(void);      /* folder for pharrier.ini / pharrier.nv / screenshots (ends in \\) */
/* modal settings window; returns 1 if the user accepted (g_set updated and saved).
 * *restart is set when an option needs the program to restart (country). */
int settings_dialog(HINSTANCE hi, HWND parent, int *restart);
extern const wchar_t *g_region_names[5];
int ph_lang_en(void);                   /* 1 = English interface */
const wchar_t *TR(const wchar_t *es);    /* Spanish UI string -> current language */
#define T2(es, en) (ph_lang_en() ? (en) : (es))
