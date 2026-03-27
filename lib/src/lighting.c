#include "protocol.h"
#include <string.h>

int f87_set_brightness(f87_device *dev, uint8_t level)
{
    if (!dev)
        return -1;

    /* Clamp to 100 */
    if (level > 100)
        level = 100;

    f87_packet pkt;
    f87_pkt_build_brightness(&pkt, level);
    int rc = f87_pkt_send(dev, &pkt);
    return (rc < 0) ? rc : 0;
}

int f87_get_brightness(f87_device *dev, uint8_t *level)
{
    (void)dev;
    (void)level;
    /* TODO: query brightness from device */
    return -1;
}

int f87_lights_off(f87_device *dev)
{
    return f87_set_brightness(dev, 0);
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

int f87_apply(f87_device *dev)
{
    if (!dev)
        return -1;

    /* Send dirty keys in batches */
    int offset = 0;
    while (offset < dev->num_keys) {
        /* Check if any keys in this range are dirty */
        int has_dirty = 0;
        for (int i = offset; i < dev->num_keys && i < offset + 20; i++) {
            if (dev->key_dirty[i]) {
                has_dirty = 1;
                break;
            }
        }

        if (has_dirty) {
            f87_packet pkt;
            int remaining = dev->num_keys - offset;
            int batch_count = remaining > 20 ? 20 : remaining;

            int packed = f87_pkt_build_per_key_batch(
                &pkt, dev->key_colors, dev->key_dirty, offset, batch_count);

            if (packed > 0) {
                int rc = f87_pkt_send(dev, &pkt);
                if (rc < 0)
                    return rc;

                /* Clear dirty flags for keys we successfully sent */
                for (int i = offset; i < offset + batch_count; i++) {
                    if (i < dev->num_keys)
                        dev->key_dirty[i] = 0;
                }
            }
        }

        offset += 20;
    }

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
