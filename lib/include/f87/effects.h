#ifndef F87_EFFECTS_H
#define F87_EFFECTS_H

#include "device.h"
#include "lighting.h"
#include <stdint.h>

/*
 * Lighting modes — hardware effect IDs.
 *
 * These are the UI_Index values from KB.ini LedOpt entries,
 * used as effect_id in config byte 18. Confirmed via USB capture.
 */
/*
 * Effect IDs confirmed via keyboard hardware scan (FN+INS cycling).
 * IDs 6, 9, 14 do NOT exist in this firmware — skipped by keyboard.
 */
typedef enum {
    F87_MODE_OFF       =  0,   /* LEDs off                              */
    F87_MODE_STATIC    =  1,   /* static single color (from 0x0A LED[0])*/
    F87_MODE_BREATHING =  2,   /* breathing (colorful)                  */
    F87_MODE_WAVE      =  3,   /* rainbow wave                          */
    F87_MODE_SPECTRUM  =  4,   /* spectrum (keypress spread)            */
    F87_MODE_RAIN      =  5,   /* rain                                  */
    /* 6 = does not exist */
    F87_MODE_RIPPLE    =  7,   /* ripple (keypress spread)              */
    F87_MODE_STARLIGHT =  8,   /* starlight (random twinkle)            */
    /* 9 = does not exist */
    F87_MODE_SNAKE     = 10,   /* snake                                 */
    F87_MODE_AURORA    = 11,   /* aurora                                */
    F87_MODE_REACTIVE  = 12,   /* reactive (single key lights up)       */
    F87_MODE_MARQUEE   = 13,   /* marquee / wave                        */
    /* 14 = does not exist */
    F87_MODE_CIRCLE    = 15,   /* circle                                */
    F87_MODE_RAINDOWN  = 16,   /* rain downward wave                    */
    F87_MODE_RIPPLE_CENTER = 17, /* center ripple / spread              */
    F87_MODE_CUSTOM    = 18,   /* custom static (byte 17=1)             */
    F87_MODE_COUNT     = 19
} f87_mode;

/* Side light / battery light effect IDs (config bytes 26, 36) */
typedef enum {
    F87_SIDE_OFF           = 0,
    F87_SIDE_RAINBOW       = 1,
    F87_SIDE_BREATHING_MIX = 2,
    F87_SIDE_STATIC_RED    = 3,
    F87_SIDE_BREATHING_RED = 4,
} f87_side_mode;

typedef enum {
    F87_DIR_RIGHT = 0,   /* col 0→17: soldan sağa */
    F87_DIR_LEFT,        /* col 17→0: sağdan sola */
    F87_DIR_UP,          /* row 5→0: aşağıdan yukarı */
    F87_DIR_DOWN,        /* row 0→5: yukarıdan aşağı */
    F87_DIR_DIAG_RIGHT,  /* col+row: çapraz sağ-aşağı */
    F87_DIR_DIAG_LEFT,   /* col-row: çapraz sol-aşağı */
    F87_DIR_CENTER_OUT,  /* merkeze uzaklık: içten dışa */
} f87_direction;

typedef struct {
    f87_mode mode;
    uint8_t speed;          /* 0-4  (0=slowest, 4=fastest) */
    uint8_t brightness;     /* 1-4  (1=dimmest, 4=brightest) */
    uint8_t colorful;       /* 0=single color, 1=colorful/random */
    f87_color color1;       /* primary color (for static, breathing, etc.) */
    f87_color color2;
    f87_direction direction;
} f87_effect;

/* Side/battery light control */
int f87_set_side_light(f87_device *dev, f87_side_mode mode);
int f87_set_battery_light(f87_device *dev, f87_side_mode mode);

int f87_set_effect(f87_device *dev, const f87_effect *effect);
int f87_get_current_effect(f87_device *dev, f87_effect *effect);

const char *f87_mode_name(f87_mode mode);

#endif
