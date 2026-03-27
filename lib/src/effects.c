#include "protocol.h"
#include <stdlib.h>

/* Static effect name strings indexed by f87_effect_type */
static const char *effect_names[F87_EFFECT_COUNT] = {
    [F87_EFFECT_STATIC]    = "Static",
    [F87_EFFECT_BREATHING] = "Breathing",
    [F87_EFFECT_WAVE]      = "Wave",
    [F87_EFFECT_RAINBOW]   = "Rainbow",
    [F87_EFFECT_RIPPLE]    = "Ripple",
    [F87_EFFECT_REACTIVE]  = "Reactive",
};

/* Static array of all supported effect types */
static f87_effect_type all_effects[F87_EFFECT_COUNT] = {
    F87_EFFECT_STATIC,
    F87_EFFECT_BREATHING,
    F87_EFFECT_WAVE,
    F87_EFFECT_RAINBOW,
    F87_EFFECT_RIPPLE,
    F87_EFFECT_REACTIVE,
};

int f87_set_effect(f87_device *dev, const f87_effect *effect)
{
    if (!dev || !effect)
        return -1;

    f87_packet pkt;
    int rc = f87_pkt_build_effect(&pkt, effect);
    if (rc != 0)
        return rc;

    rc = f87_pkt_send(dev, &pkt);
    return (rc < 0) ? rc : 0;
}

int f87_get_current_effect(f87_device *dev, f87_effect *effect)
{
    (void)dev;
    (void)effect;
    /* TODO: query current effect from device */
    return -1;
}

int f87_get_supported_effects(f87_device *dev, f87_effect_type **list, int *count)
{
    (void)dev;
    if (!list || !count)
        return -1;

    *list = all_effects;
    *count = F87_EFFECT_COUNT;
    return 0;
}

const char *f87_effect_name(f87_effect_type type)
{
    if (type >= F87_EFFECT_COUNT)
        return "Unknown";
    return effect_names[type];
}
