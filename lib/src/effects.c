#include "protocol.h"
#include <stdlib.h>
#include <string.h>

static const char *mode_names[F87_MODE_COUNT] = {
    [F87_MODE_OFF]            = "Off",
    [F87_MODE_STATIC]         = "Static",
    [F87_MODE_BREATHING]      = "Breathing",
    [F87_MODE_WAVE]           = "Wave",
    [F87_MODE_SPECTRUM]       = "Spectrum",
    [F87_MODE_RAIN]           = "Rain",
    [F87_MODE_RIPPLE]         = "Ripple",
    [F87_MODE_STARLIGHT]      = "Starlight",
    [F87_MODE_SNAKE]          = "Snake",
    [F87_MODE_AURORA]         = "Aurora",
    [F87_MODE_REACTIVE]       = "Reactive",
    [F87_MODE_MARQUEE]        = "Marquee",
    [F87_MODE_CIRCLE]         = "Circle",
    [F87_MODE_RAINDOWN]       = "Rain Down",
    [F87_MODE_RIPPLE_CENTER]  = "Center Ripple",
    [F87_MODE_CUSTOM]         = "Custom",
};

/*
 * Set a hardware lighting effect.
 *
 * For modes that use a color (static, breathing, etc.), sends 0x0A
 * with LED[0] = color first. Then writes config with the effect_id,
 * brightness, and speed.
 */
int f87_set_effect(f87_device *dev, const f87_effect *effect)
{
    if (!dev || !effect)
        return -1;

    uint8_t effect_id = (uint8_t)effect->mode;
    uint8_t brightness = effect->brightness;
    uint8_t speed = effect->speed;

    if (brightness < F87_BRIGHTNESS_MIN)
        brightness = F87_BRIGHTNESS_MAX;
    if (speed > F87_SPEED_MAX)
        speed = 0xFF; /* don't change */

    /* For color-based modes, send the color via 0x0A first */
    if (effect->color1.r || effect->color1.g || effect->color1.b) {
        if (effect->mode != F87_MODE_OFF &&
            effect->mode != F87_MODE_WAVE &&
            effect->mode != F87_MODE_CIRCLE &&
            effect->mode != F87_MODE_RAINDOWN &&
            effect->mode != F87_MODE_RIPPLE_CENTER) {
            f87_packet pkt;
            f87_pkt_build_led_custom(&pkt, effect->color1);
            int rc = f87_pkt_send(dev, &pkt);
            if (rc < 0)
                return rc;
        }
    }

    /* For custom per-key mode, apply buffered colors */
    if (effect->mode == F87_MODE_CUSTOM) {
        f87_packet pkt;
        f87_pkt_build_led_planar(&pkt, dev->key_colors, f87_led_index,
                                  dev->num_keys);
        int rc = f87_pkt_send(dev, &pkt);
        if (rc < 0)
            return rc;
    }

    /* Config read-modify-write */
    int rc = f87_config_read(dev);
    if (rc < 0)
        return rc;

    return f87_config_write(dev, effect_id, brightness, speed);
}

/*
 * Read the current effect from device config.
 */
int f87_get_current_effect(f87_device *dev, f87_effect *effect)
{
    if (!dev || !effect)
        return -1;

    int rc = f87_config_read(dev);
    if (rc < 0)
        return rc;

    memset(effect, 0, sizeof(*effect));
    effect->mode = (f87_mode)dev->config[F87_CFG_EFFECT_ID];

    /* Read per-effect brightness and speed */
    uint8_t eid = dev->config[F87_CFG_EFFECT_ID];
    if (eid > 0 && eid <= 18) {
        int off = F87_CFG_EFFECT_PARAM(eid);
        effect->brightness = dev->config[off];
        effect->speed = (dev->config[off + 1] >> 4) & 0x0F;
    }

    return 0;
}

/*
 * Set side light effect (config byte 26).
 */
int f87_set_side_light(f87_device *dev, f87_side_mode mode)
{
    if (!dev)
        return -1;

    int rc = f87_config_read(dev);
    if (rc < 0)
        return rc;

    dev->config[F87_CFG_SIDE_LIGHT] = (uint8_t)mode;

    /* Write config preserving current effect */
    uint8_t eid = dev->config[F87_CFG_EFFECT_ID];
    uint8_t brightness = F87_BRIGHTNESS_MAX;
    if (eid > 0 && eid <= 18)
        brightness = dev->config[F87_CFG_EFFECT_PARAM(eid)];

    return f87_config_write(dev, eid, brightness, 0xFF);
}

/*
 * Set battery light effect (config byte 36).
 */
int f87_set_battery_light(f87_device *dev, f87_side_mode mode)
{
    if (!dev)
        return -1;

    int rc = f87_config_read(dev);
    if (rc < 0)
        return rc;

    dev->config[F87_CFG_BATTERY_LIGHT] = (uint8_t)mode;

    uint8_t eid = dev->config[F87_CFG_EFFECT_ID];
    uint8_t brightness = F87_BRIGHTNESS_MAX;
    if (eid > 0 && eid <= 18)
        brightness = dev->config[F87_CFG_EFFECT_PARAM(eid)];

    return f87_config_write(dev, eid, brightness, 0xFF);
}

const char *f87_mode_name(f87_mode mode)
{
    if (mode >= F87_MODE_COUNT || !mode_names[mode])
        return "Unknown";
    return mode_names[mode];
}
