#include "protocol.h"
#include <string.h>

int f87_set_brightness(f87_device *dev, uint8_t level)
{
    if (!dev)
        return -1;

    /* Clamp to 100 */
    if (level > 100)
        level = 100;

    /*
     * In direct mode there is no separate brightness command.
     * Scale all key colors by the brightness level and send a full
     * LED update.  This is a simple software-side brightness control.
     */
    f87_color scaled[F87_KEY_COUNT];
    for (int i = 0; i < dev->num_keys; i++) {
        scaled[i].r = (uint8_t)((dev->key_colors[i].r * level) / 100);
        scaled[i].g = (uint8_t)((dev->key_colors[i].g * level) / 100);
        scaled[i].b = (uint8_t)((dev->key_colors[i].b * level) / 100);
    }

    f87_packet pkt;
    f87_pkt_build_direct_leds(&pkt, scaled, f87_led_index, dev->num_keys);
    int rc = f87_pkt_send(dev, &pkt);
    return (rc < 0) ? rc : 0;
}

int f87_get_brightness(f87_device *dev, uint8_t *level)
{
    (void)dev;
    (void)level;
    /* TODO: no known query for current brightness in direct mode */
    return -1;
}

int f87_lights_off(f87_device *dev)
{
    if (!dev)
        return -1;

    /* Send an all-black LED frame */
    f87_color off[F87_KEY_COUNT];
    memset(off, 0, sizeof(off));

    f87_packet pkt;
    f87_pkt_build_direct_leds(&pkt, off, f87_led_index, F87_KEY_COUNT);
    int rc = f87_pkt_send(dev, &pkt);
    return (rc < 0) ? rc : 0;
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
 * Apply all pending key color changes by sending a single 520-byte
 * direct-mode LED feature report.
 *
 * Unlike the old 64-byte protocol that required multiple batched packets,
 * the 520-byte report contains RGB data for ALL LEDs in one transfer.
 * The hardware LED index mapping (f87_led_index[]) maps each key_id to
 * its position in the LED data buffer.
 */
int f87_apply(f87_device *dev)
{
    if (!dev)
        return -1;

    /* Check if anything is dirty */
    int has_dirty = 0;
    for (int i = 0; i < dev->num_keys; i++) {
        if (dev->key_dirty[i]) {
            has_dirty = 1;
            break;
        }
    }
    if (!has_dirty)
        return 0;

    /* Build a single feature report with all key colors */
    f87_packet pkt;
    f87_pkt_build_direct_leds(&pkt, dev->key_colors, f87_led_index,
                               dev->num_keys);

    int rc = f87_pkt_send(dev, &pkt);
    if (rc < 0)
        return rc;

    /* Clear all dirty flags */
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
