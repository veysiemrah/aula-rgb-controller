#ifndef F87_EFFECTS_H
#define F87_EFFECTS_H

#include "device.h"
#include "lighting.h"
#include <stdint.h>

typedef enum {
    F87_EFFECT_STATIC = 0,
    F87_EFFECT_BREATHING,
    F87_EFFECT_WAVE,
    F87_EFFECT_RAINBOW,
    F87_EFFECT_RIPPLE,
    F87_EFFECT_REACTIVE,
    F87_EFFECT_COUNT
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
