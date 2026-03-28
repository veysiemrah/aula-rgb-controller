#include "protocol.h"
#include <string.h>

/*
 * Set brightness for the current effect.
 * Uses per-effect parameter at offset 64 + 2*effect_id.
 * Input: 1-4 directly (hardware range).
 */
int f87_set_brightness(f87_device *dev, uint8_t level)
{
    if (!dev)
        return -1;

    if (level < F87_BRIGHTNESS_MIN)
        level = F87_BRIGHTNESS_MIN;
    if (level > F87_BRIGHTNESS_MAX)
        level = F87_BRIGHTNESS_MAX;

    int rc = f87_config_read(dev);
    if (rc < 0)
        return rc;

    uint8_t effect_id = dev->config[F87_CFG_EFFECT_ID];
    return f87_config_write(dev, effect_id, level, 0xFF);
}

int f87_get_brightness(f87_device *dev, uint8_t *level)
{
    if (!dev || !level)
        return -1;

    int rc = f87_config_read(dev);
    if (rc < 0)
        return rc;

    uint8_t eid = dev->config[F87_CFG_EFFECT_ID];
    if (eid > 0 && eid <= 18) {
        *level = dev->config[F87_CFG_EFFECT_PARAM(eid)];
    } else {
        *level = F87_BRIGHTNESS_MAX;
    }
    return 0;
}

/*
 * Turn off all LEDs by setting effect_id to 0x00 (OFF).
 */
int f87_lights_off(f87_device *dev)
{
    if (!dev)
        return -1;

    int rc = f87_config_read(dev);
    if (rc < 0)
        return rc;

    return f87_config_write(dev, 0x00, F87_BRIGHTNESS_MAX, 0xFF);
}

int f87_set_key_color(f87_device *dev, uint8_t key_id, f87_color color)
{
    if (!dev || key_id >= dev->num_keys)
        return -1;

    dev->key_colors[key_id] = color;
    dev->key_dirty[key_id] = 1;
    return 0;
}

int f87_set_all_keys(f87_device *dev, f87_color color)
{
    if (!dev)
        return -1;

    for (int i = 0; i < dev->num_keys; i++) {
        dev->key_colors[i] = color;
        dev->key_dirty[i] = 1;
    }
    return 0;
}

int f87_set_key_map(f87_device *dev, const f87_color *colors, int count)
{
    if (!dev || !colors)
        return -1;

    int n = count;
    if (n > dev->num_keys)
        n = dev->num_keys;

    memcpy(dev->key_colors, colors, (size_t)n * sizeof(f87_color));
    for (int i = 0; i < n; i++)
        dev->key_dirty[i] = 1;

    return 0;
}

/*
 * Apply pending key color changes using the 4-step protocol.
 *
 * If all keys are the same color → static mode (0x0A + effect_id=0x01).
 * If keys differ → per-key custom mode (0x06 + effect_id=0x12).
 */
int f87_apply(f87_device *dev)
{
    if (!dev)
        return -1;

    int has_dirty = 0;
    for (int i = 0; i < dev->num_keys; i++) {
        if (dev->key_dirty[i]) {
            has_dirty = 1;
            break;
        }
    }
    if (!has_dirty)
        return 0;

    /* Check if all keys have the same color */
    int all_same = 1;
    f87_color first = dev->key_colors[0];
    for (int i = 1; i < dev->num_keys; i++) {
        if (dev->key_colors[i].r != first.r ||
            dev->key_colors[i].g != first.g ||
            dev->key_colors[i].b != first.b) {
            all_same = 0;
            break;
        }
    }

    f87_packet pkt;
    int rc;

    if (all_same) {
        /* Static single-color: cmd 0x0A + effect_id 0x01 */
        f87_pkt_build_led_custom(&pkt, first);
        rc = f87_pkt_send(dev, &pkt);
        if (rc < 0)
            return rc;

        rc = f87_config_read(dev);
        if (rc < 0)
            return rc;

        uint8_t brightness = dev->config[F87_CFG_EFFECT_PARAM(F87_MODE_STATIC)];
        if (brightness < F87_BRIGHTNESS_MIN)
            brightness = F87_BRIGHTNESS_MAX;

        rc = f87_config_write(dev, F87_MODE_STATIC, brightness, 0xFF);
    } else {
        /* Per-key: cmd 0x06 planar + effect_id 0x12 (custom) */
        f87_pkt_build_led_planar(&pkt, dev->key_colors, f87_led_index,
                                  dev->num_keys);
        rc = f87_pkt_send(dev, &pkt);
        if (rc < 0)
            return rc;

        rc = f87_config_read(dev);
        if (rc < 0)
            return rc;

        uint8_t brightness = dev->config[F87_CFG_EFFECT_PARAM(F87_MODE_CUSTOM)];
        if (brightness < F87_BRIGHTNESS_MIN)
            brightness = F87_BRIGHTNESS_MAX;

        rc = f87_config_write(dev, F87_MODE_CUSTOM, brightness, 0xFF);
    }

    if (rc < 0)
        return rc;

    memset(dev->key_dirty, 0, sizeof(dev->key_dirty));
    return 0;
}

int f87_get_key_count(f87_device *dev)
{
    if (!dev)
        return -1;
    return dev->num_keys;
}

const f87_key_info *f87_get_key_layout(f87_device *dev)
{
    (void)dev;
    return f87_key_layout;
}
