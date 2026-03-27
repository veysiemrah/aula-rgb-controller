#ifndef F87_LIGHTING_H
#define F87_LIGHTING_H

#include "device.h"
#include <stdint.h>

typedef struct {
    uint8_t r, g, b;
} f87_color;

#define F87_COLOR(r, g, b) ((f87_color){(r), (g), (b)})
#define F87_COLOR_OFF       F87_COLOR(0, 0, 0)
#define F87_COLOR_WHITE     F87_COLOR(255, 255, 255)
#define F87_COLOR_RED       F87_COLOR(255, 0, 0)
#define F87_COLOR_GREEN     F87_COLOR(0, 255, 0)
#define F87_COLOR_BLUE      F87_COLOR(0, 0, 255)

typedef struct {
    uint8_t key_id;
    const char *name;
    uint8_t row;
    uint8_t col;
} f87_key_info;

int f87_set_brightness(f87_device *dev, uint8_t level);
int f87_get_brightness(f87_device *dev, uint8_t *level);
int f87_lights_off(f87_device *dev);

int f87_set_key_color(f87_device *dev, uint8_t key_id, f87_color color);
int f87_set_all_keys(f87_device *dev, f87_color color);
int f87_set_key_map(f87_device *dev, const f87_color *colors, int count);
int f87_apply(f87_device *dev);

int f87_get_key_count(f87_device *dev);
const f87_key_info *f87_get_key_layout(f87_device *dev);

#endif
