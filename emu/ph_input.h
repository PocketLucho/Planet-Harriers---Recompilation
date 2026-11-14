#pragma once
/* Planet Harriers cabinet controls, shared by the frontends.
 * JVS analog 0 = stick up/down, analog 1 = stick left/right (0x8000 = centre); player 1 switches: 0x0200 trigger
 * (bullet), 0x0100 missile, 0x0080 bomb, 0x0800 view-change left, 0x0400 view-change right, 0x8000 start. */
#include "mie.h"
enum { PH_BTN_SHOT = 0x0200, PH_BTN_MISSILE = 0x0100, PH_BTN_BOMB = 0x0080, PH_BTN_VR_L = 0x0800, PH_BTN_VR_R = 0x0400,
       PH_BTN_START = 0x8000, PH_BTN_SERVICE = 0x4000 };
typedef struct { float x, y; } PhStick;      /* smoothed keyboard stick, -1..1 */
/* keys: target direction from the keyboard (-1, 0, 1 each axis; y > 0 = stick pushed up/forward);
 * padx/pady: analog stick of a gamepad, -1..1 (used when the keyboard is idle on that axis);
 * speed 1..10: how fast the keyboard stick travels; invert_y swaps up and down (flight-stick style).
 * Measured on the game: a low value on analog 1 moves the player left, a low value on analog 0 moves it up. */
static inline void ph_stick_update(PhStick *s, int kx, int ky, float padx, float pady, int speed, int invert_y) {
    float d = 0.02f * (float)speed;
    float tx = (float)kx, ty = (float)ky;
    if (s->x < tx) { s->x += d; if (s->x > tx) s->x = tx; } else if (s->x > tx) { s->x -= d; if (s->x < tx) s->x = tx; }
    if (s->y < ty) { s->y += d; if (s->y > ty) s->y = ty; } else if (s->y > ty) { s->y -= d; if (s->y < ty) s->y = ty; }
    float x = kx || s->x != 0 ? s->x : padx, y = ky || s->y != 0 ? s->y : pady;
    if (invert_y) y = -y;
    if (x > 1) x = 1; if (x < -1) x = -1; if (y > 1) y = 1; if (y < -1) y = -1;
    g_input.analog[1] = (uint16_t)(0x8000 + (int)(x * 0x7f00));
    g_input.analog[0] = (uint16_t)(0x8000 - (int)(y * 0x7f00));
}
