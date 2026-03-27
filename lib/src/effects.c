#include "protocol.h"
#include <stdlib.h>
#include <string.h>

/* Mode name strings */
static const char *mode_names[F87_MODE_COUNT] = {
    [F87_MODE_OFF]    = "Off",
    [F87_MODE_DIRECT] = "Direct",
};

int f87_set_effect(f87_device *dev, const f87_effect *effect)
{
    if (!dev || !effect)
        return -1;

    switch (effect->mode) {
    case F87_MODE_OFF:
        return f87_lights_off(dev);

    case F87_MODE_DIRECT:
        /*
         * In direct mode, colors are set per-key via f87_set_key_color()
         * and pushed with f87_apply().  If a color is provided in the
         * effect struct, set all keys to that color and apply.
         */
        if (effect->color1.r || effect->color1.g || effect->color1.b) {
            f87_set_all_keys(dev, effect->color1);
        }
        return f87_apply(dev);

    default:
        return -1;
    }
}

int f87_get_current_effect(f87_device *dev, f87_effect *effect)
{
    (void)dev;
    (void)effect;
    /* TODO: no known query for current mode in direct protocol */
    return -1;
}

const char *f87_mode_name(f87_mode mode)
{
    if (mode >= F87_MODE_COUNT)
        return "Unknown";
    return mode_names[mode];
}
