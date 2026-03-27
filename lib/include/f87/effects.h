#ifndef F87_EFFECTS_H
#define F87_EFFECTS_H

#include "device.h"
#include "lighting.h"
#include <stdint.h>

/*
 * Effect types — software index mapped to hardware effect ID.
 * Values are the hardware IDs from KB.ini LedOpt reverse engineering.
 * The driver sends these IDs directly to the keyboard firmware.
 */
typedef enum {
    F87_EFFECT_STATIC       =  1,  /* LedOpt1:  HW 1  — static color          */
    F87_EFFECT_BREATHING    =  3,  /* LedOpt2:  HW 3  — breathing             */
    F87_EFFECT_WAVE         =  2,  /* LedOpt3:  HW 2  — rainbow wave (no color) */
    F87_EFFECT_SPECTRUM     = 19,  /* LedOpt4:  HW 19 —                        */
    F87_EFFECT_RIPPLE       = 15,  /* LedOpt5:  HW 15 —                        */
    F87_EFFECT_REACTIVE     = 13,  /* LedOpt6:  HW 13 —                        */
    F87_EFFECT_STARLIGHT    = 20,  /* LedOpt7:  HW 20 —                        */
    F87_EFFECT_RAIN         = 16,  /* LedOpt8:  HW 16 —                        */
    F87_EFFECT_SNAKE        = 18,  /* LedOpt9:  HW 18 —                        */
    F87_EFFECT_MARQUEE      =  5,  /* LedOpt10: HW 5  — (default effect)       */
    F87_EFFECT_AURORA       =  7,  /* LedOpt11: HW 7  —                        */
    F87_EFFECT_LASER        = 17,  /* LedOpt12: HW 17 —                        */
    F87_EFFECT_FIREWORK     = 12,  /* LedOpt13: HW 12 —                        */
    F87_EFFECT_GRADIENT     =  8,  /* LedOpt14: HW 8  —                        */
    F87_EFFECT_RAINBOW_WAVE = 28,  /* LedOpt15: HW 28 — (no color/random)      */
    F87_EFFECT_PRISM        = 30,  /* LedOpt16: HW 30 — (no color/random)      */
    F87_EFFECT_CYCLE        = 14,  /* LedOpt17: HW 14 — (no color/random)      */
    F87_EFFECT_TIDAL        = 29,  /* LedOpt18: HW 29 —                        */
    F87_EFFECT_CUSTOM       =  0,  /* LedOpt19: HW 0  — per-key custom mode    */
    F87_EFFECT_COUNT        = 19   /* total number of effects                   */
} f87_effect_type;

typedef enum {
    F87_DIR_RIGHT = 0,
    F87_DIR_LEFT,
    F87_DIR_UP,
    F87_DIR_DOWN
} f87_direction;

typedef struct {
    f87_effect_type type;
    uint8_t speed;          /* 0-10 */
    uint8_t brightness;     /* 0-100 */
    f87_color color1;
    f87_color color2;
    f87_direction direction;
} f87_effect;

int f87_set_effect(f87_device *dev, const f87_effect *effect);
int f87_get_current_effect(f87_device *dev, f87_effect *effect);
int f87_get_supported_effects(f87_device *dev, f87_effect_type **list, int *count);

const char *f87_effect_name(f87_effect_type type);

#endif
