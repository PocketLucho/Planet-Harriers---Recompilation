/* Menus drawn on an RGBA canvas (premultiplied alpha) that the Vulkan renderer lays over the picture
 * (vkr_ui_upload): the launcher shown before the game and the pause menu with its pages. Shared by the
 * Linux (SDL) and Windows frontends; they translate their input into UI_KEY_* and act on the UI_* results. */
#pragma once
#include <stdint.h>

typedef struct {
    int scale, wide, msaa, fullscreen;          /* video */
    int pillarbox;                              /* widescreen: black sides on full-screen 4:3 pictures (title screen) */
    int stick_speed, pad_mode, invert_y;        /* controls: key stick speed 1..10, gamepad on/off, up/down swapped */
    int lang;                                   /* 0 auto, 1 Spanish, 2 English */
    int region;                                 /* 0 Japan, 1 USA, 2 Export, 3 Australia, 4 Korea */
    int show_launcher;                          /* show the launcher when the program starts */
} UiOptions;

typedef struct {
    const char *version;                        /* "0.5" */
    const char *platform;                       /* "Linux", "Steam Deck", "Windows" */
    const char *gpu;                            /* Vulkan device name */
    int blocks;                                 /* recompiled blocks loaded (0 = interpreter) */
    int msaa_gpu;                               /* samples the GPU gave (0 = unknown) */
    const char *pad_name;                       /* connected gamepad, 0 = none */
    const char *rom;                            /* ROM file name, 0 = not found */
    int (*lang_en)(void);                       /* 1 = English interface */
    int nbind;                                  /* controls page: action / key pairs */
    const char *bind_act[20], *bind_key[20];
} UiInfo;

enum { UI_KEY_UP, UI_KEY_DOWN, UI_KEY_LEFT, UI_KEY_RIGHT, UI_KEY_OK, UI_KEY_BACK, UI_KEY_PREV, UI_KEY_NEXT };
enum { UI_NONE, UI_START, UI_RESUME, UI_RESET, UI_LAUNCHER, UI_QUIT, UI_VIDEO, UI_FULLSCREEN, UI_REGION, UI_SCREENSHOT, UI_SAVE };
enum { UI_SCREEN_LAUNCHER, UI_SCREEN_PAUSE };

void ui_open(int screen, UiOptions *o, UiInfo *info);   /* the options are edited in place */
void ui_close(void);
int  ui_is_open(void);
int  ui_screen(void);
int  ui_key(int key);                                   /* one UI_* result */
void ui_render(uint32_t *rgba, int w, int h, double t); /* t = seconds, for the animation */
