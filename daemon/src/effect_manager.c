#include "effect_manager.h"
#include <string.h>
#include <stdio.h>

void f87d_effmgr_init(f87d_effect_manager_t *mgr)
{
    memset(mgr, 0, sizeof(*mgr));
    mgr->category = F87D_CAT_NONE;
    mgr->effect_id = -1;
}

void f87d_effmgr_destroy(f87d_effect_manager_t *mgr)
{
    f87d_effmgr_stop(mgr);
}

static void stop_anim(f87d_effect_manager_t *mgr)
{
    if (mgr->anim) {
        f87_anim_stop(mgr->anim);
        mgr->anim = NULL;
    }
}

int f87d_effmgr_set_hw(f87d_effect_manager_t *mgr, f87_device *dev,
                        int effect_id, uint8_t brightness, uint8_t speed,
                        uint8_t colorful, uint8_t r, uint8_t g, uint8_t b)
{
    if (!dev) return -1;

    stop_anim(mgr);

    f87_effect effect = {0};
    effect.mode = (f87_mode)effect_id;
    effect.brightness = brightness;
    effect.speed = speed;
    effect.colorful = colorful;
    effect.color1 = (f87_color){r, g, b};

    int rc = f87_set_effect(dev, &effect);
    if (rc < 0) return rc;

    mgr->category = F87D_CAT_HW;
    mgr->effect_id = effect_id;
    mgr->brightness = brightness;
    mgr->speed = speed;
    mgr->color[0] = r;
    mgr->color[1] = g;
    mgr->color[2] = b;
    mgr->colorful = colorful;
    return 0;
}

/* Hot-switch is only safe within the same category because different
 * categories need different thread configurations:
 *   SW: render thread only
 *   SW reactive: render thread + input capture
 *   Music: render thread + audio capture thread
 *   Sensor: render thread + sensor polling
 *
 * Switching between these requires full stop + restart.
 * Within the same category, f87_anim_set_effect safely swaps the renderer.
 *
 * For SW effects, we further check needs_input compatibility:
 * non-reactive <-> reactive also needs restart for input thread.
 */
static int can_hot_switch(f87d_effect_manager_t *mgr,
                           f87d_effect_category_t new_cat)
{
    if (!mgr->anim || !f87_anim_is_running(mgr->anim))
        return 0;
    return mgr->category == new_cat;
}

int f87d_effmgr_set_sw(f87d_effect_manager_t *mgr, f87_device *dev,
                        int effect_id, uint8_t brightness, uint8_t speed,
                        uint8_t r, uint8_t g, uint8_t b, int fps)
{
    if (!dev) return -1;

    if (can_hot_switch(mgr, F87D_CAT_SW)) {
        f87_anim_set_effect(mgr->anim, (f87_sw_effect_id)effect_id);
        f87_anim_set_color(mgr->anim, r, g, b);
    } else {
        stop_anim(mgr);

        f87_anim_config_t config = {
            .color = {r, g, b},
            .brightness = brightness,
            .speed = speed,
            .fps = fps,
        };

        mgr->anim = f87_anim_start(dev, (f87_sw_effect_id)effect_id, &config);
        if (!mgr->anim) return -1;
    }

    mgr->category = F87D_CAT_SW;
    mgr->effect_id = effect_id;
    mgr->brightness = brightness;
    mgr->speed = speed;
    mgr->color[0] = r;
    mgr->color[1] = g;
    mgr->color[2] = b;
    return 0;
}

int f87d_effmgr_set_music(f87d_effect_manager_t *mgr, f87_device *dev,
                           int effect_id, uint8_t brightness,
                           uint8_t r, uint8_t g, uint8_t b, double gain)
{
    if (!dev) return -1;

    if (can_hot_switch(mgr, F87D_CAT_MUSIC)) {
        f87_anim_set_effect(mgr->anim, (f87_sw_effect_id)effect_id);
        f87_anim_set_color(mgr->anim, r, g, b);
    } else {
        stop_anim(mgr);

        f87_anim_config_t config = {
            .color = {r, g, b},
            .brightness = brightness,
            .speed = 2,
            .audio_source = F87_AUDIO_MONITOR,
            .gain = (float)gain,
        };

        mgr->anim = f87_anim_start(dev, (f87_sw_effect_id)effect_id, &config);
        if (!mgr->anim) return -1;
    }

    mgr->category = F87D_CAT_MUSIC;
    mgr->effect_id = effect_id;
    mgr->brightness = brightness;
    mgr->color[0] = r;
    mgr->color[1] = g;
    mgr->color[2] = b;
    mgr->gain = gain;
    return 0;
}

int f87d_effmgr_set_sensor(f87d_effect_manager_t *mgr, f87_device *dev,
                            const char *profile, const char *config_path)
{
    if (!dev) return -1;

    /* Sensor always needs fresh start (different config structure) */
    stop_anim(mgr);

    f87_anim_config_t config = {
        .brightness = 3,
        .speed = 2,
        .sensor_profile = profile,
        .sensor_config_path = config_path,
    };

    mgr->anim = f87_anim_start(dev, F87_SW_SENSOR, &config);
    if (!mgr->anim) return -1;

    mgr->category = F87D_CAT_SENSOR;
    mgr->effect_id = F87_SW_SENSOR;
    if (profile)
        strncpy(mgr->sensor_profile, profile, sizeof(mgr->sensor_profile) - 1);
    else
        mgr->sensor_profile[0] = '\0';
    if (config_path)
        strncpy(mgr->sensor_config_path, config_path, sizeof(mgr->sensor_config_path) - 1);
    else
        mgr->sensor_config_path[0] = '\0';
    return 0;
}

int f87d_effmgr_set_side_light(f87d_effect_manager_t *mgr, f87_device *dev,
                                uint8_t mode)
{
    if (!dev) return -1;
    int rc = f87_set_side_light(dev, (f87_side_mode)mode);
    if (rc < 0) return rc;
    mgr->side_light = mode;
    return 0;
}

int f87d_effmgr_set_battery_light(f87d_effect_manager_t *mgr, f87_device *dev,
                                   uint8_t mode)
{
    if (!dev) return -1;
    int rc = f87_set_battery_light(dev, (f87_side_mode)mode);
    if (rc < 0) return rc;
    mgr->battery_light = mode;
    return 0;
}

int f87d_effmgr_stop(f87d_effect_manager_t *mgr)
{
    stop_anim(mgr);
    mgr->category = F87D_CAT_NONE;
    mgr->effect_id = -1;
    return 0;
}

const char *f87d_effmgr_category_str(f87d_effect_category_t cat)
{
    switch (cat) {
    case F87D_CAT_HW:     return "hw";
    case F87D_CAT_SW:     return "sw";
    case F87D_CAT_MUSIC:  return "music";
    case F87D_CAT_SENSOR: return "sensor";
    default:              return "";
    }
}

bool f87d_effmgr_has_sw_running(const f87d_effect_manager_t *mgr)
{
    return mgr->anim != NULL &&
           (mgr->category == F87D_CAT_SW ||
            mgr->category == F87D_CAT_MUSIC ||
            mgr->category == F87D_CAT_SENSOR);
}
