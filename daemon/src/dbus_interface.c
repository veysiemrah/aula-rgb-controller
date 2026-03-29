#include "dbus_interface.h"
#include <f87/logger.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#define DBUS_PATH "/org/f87/Control"
#define DBUS_IFACE "org.f87.Control"

/* ---- Helpers ---- */

static void snapshot_to_profile(f87d_dbus_ctx_t *ctx, f87d_profile_t *p)
{
    memset(p, 0, sizeof(*p));
    strncpy(p->category,
            f87d_effmgr_category_str(ctx->effmgr->category),
            sizeof(p->category) - 1);
    p->effect_id = ctx->effmgr->effect_id;
    p->brightness = ctx->effmgr->brightness;
    p->speed = ctx->effmgr->speed;
    p->colorful = ctx->effmgr->colorful;
    memcpy(p->color, ctx->effmgr->color, 3);
    p->side_light = ctx->effmgr->side_light;
    p->battery_light = ctx->effmgr->battery_light;
    p->gain = ctx->effmgr->gain;
    strncpy(p->sensor_profile, ctx->effmgr->sensor_profile,
            sizeof(p->sensor_profile) - 1);
    strncpy(p->sensor_config_path, ctx->effmgr->sensor_config_path,
            sizeof(p->sensor_config_path) - 1);
}

static void autosave_last(f87d_dbus_ctx_t *ctx)
{
    f87d_profile_t p;
    snapshot_to_profile(ctx, &p);
    f87d_profile_save_last(&p);
}

/* ---- Method handlers ---- */

static int method_set_effect(sd_bus_message *msg, void *userdata,
                              sd_bus_error *error)
{
    f87d_dbus_ctx_t *ctx = userdata;
    f87d_idle_touch(ctx->idle);
    int32_t effect_id;
    uint8_t brightness, speed, r, g, b;
    int colorful;

    int rc = sd_bus_message_read(msg, "iyybyyy", &effect_id, &brightness,
                                  &speed, &colorful, &r, &g, &b);
    if (rc < 0)
        return sd_bus_error_set_errno(error, -rc);

    f87_device *dev = f87d_devmgr_get_device(ctx->devmgr);
    if (!dev)
        return sd_bus_reply_method_errorf(msg,
            "org.f87.Error.NotConnected", "No keyboard connected");

    rc = f87d_effmgr_set_hw(ctx->effmgr, dev, effect_id, brightness, speed,
                             (uint8_t)colorful, r, g, b);
    if (rc < 0)
        return sd_bus_reply_method_errorf(msg,
            "org.f87.Error.USBError", "Failed to set effect: %d", rc);

    autosave_last(ctx);
    f87d_dbus_emit_effect_changed(ctx, effect_id, "hw");
    return sd_bus_reply_method_return(msg, "b", 1);
}

static int method_set_sw_effect(sd_bus_message *msg, void *userdata,
                                 sd_bus_error *error)
{
    f87d_dbus_ctx_t *ctx = userdata;
    f87d_idle_touch(ctx->idle);
    int32_t effect_id, fps;
    uint8_t brightness, speed, r, g, b;

    int rc = sd_bus_message_read(msg, "iyyyyyi", &effect_id, &brightness,
                                  &speed, &r, &g, &b, &fps);
    if (rc < 0)
        return sd_bus_error_set_errno(error, -rc);

    f87_device *dev = f87d_devmgr_get_device(ctx->devmgr);
    if (!dev)
        return sd_bus_reply_method_errorf(msg,
            "org.f87.Error.NotConnected", "No keyboard connected");

    rc = f87d_effmgr_set_sw(ctx->effmgr, dev, effect_id, brightness, speed,
                             r, g, b, fps);
    if (rc < 0)
        return sd_bus_reply_method_errorf(msg,
            "org.f87.Error.USBError", "Failed to start sw effect: %d", rc);

    autosave_last(ctx);
    f87d_dbus_emit_effect_changed(ctx, effect_id, "sw");
    return sd_bus_reply_method_return(msg, "b", 1);
}

static int method_set_music_effect(sd_bus_message *msg, void *userdata,
                                    sd_bus_error *error)
{
    f87d_dbus_ctx_t *ctx = userdata;
    f87d_idle_touch(ctx->idle);
    int32_t effect_id;
    uint8_t brightness, r, g, b;
    double gain;

    int rc = sd_bus_message_read(msg, "iyyyd", &effect_id, &brightness,
                                  &r, &g, &b, &gain);
    if (rc < 0)
        return sd_bus_error_set_errno(error, -rc);

    f87_device *dev = f87d_devmgr_get_device(ctx->devmgr);
    if (!dev)
        return sd_bus_reply_method_errorf(msg,
            "org.f87.Error.NotConnected", "No keyboard connected");

    rc = f87d_effmgr_set_music(ctx->effmgr, dev, effect_id, brightness,
                                r, g, b, gain);
    if (rc < 0)
        return sd_bus_reply_method_errorf(msg,
            "org.f87.Error.USBError", "Failed to start music effect: %d", rc);

    autosave_last(ctx);
    f87d_dbus_emit_effect_changed(ctx, effect_id, "music");
    return sd_bus_reply_method_return(msg, "b", 1);
}

static int method_set_sensor_effect(sd_bus_message *msg, void *userdata,
                                     sd_bus_error *error)
{
    f87d_dbus_ctx_t *ctx = userdata;
    f87d_idle_touch(ctx->idle);
    const char *profile = NULL, *config_path = NULL;

    int rc = sd_bus_message_read(msg, "ss", &profile, &config_path);
    if (rc < 0)
        return sd_bus_error_set_errno(error, -rc);

    f87_device *dev = f87d_devmgr_get_device(ctx->devmgr);
    if (!dev)
        return sd_bus_reply_method_errorf(msg,
            "org.f87.Error.NotConnected", "No keyboard connected");

    rc = f87d_effmgr_set_sensor(ctx->effmgr, dev,
                                 profile[0] ? profile : NULL,
                                 config_path[0] ? config_path : NULL);
    if (rc < 0)
        return sd_bus_reply_method_errorf(msg,
            "org.f87.Error.USBError", "Failed to start sensor effect: %d", rc);

    autosave_last(ctx);
    f87d_dbus_emit_effect_changed(ctx, F87_SW_SENSOR, "sensor");
    return sd_bus_reply_method_return(msg, "b", 1);
}

static int method_set_brightness(sd_bus_message *msg, void *userdata,
                                  sd_bus_error *error)
{
    f87d_dbus_ctx_t *ctx = userdata;
    f87d_idle_touch(ctx->idle);
    uint8_t level;

    int rc = sd_bus_message_read(msg, "y", &level);
    if (rc < 0)
        return sd_bus_error_set_errno(error, -rc);

    f87_device *dev = f87d_devmgr_get_device(ctx->devmgr);
    if (!dev)
        return sd_bus_reply_method_errorf(msg,
            "org.f87.Error.NotConnected", "No keyboard connected");

    rc = f87_set_brightness(dev, level);
    if (rc < 0)
        return sd_bus_reply_method_errorf(msg,
            "org.f87.Error.USBError", "Failed to set brightness: %d", rc);

    return sd_bus_reply_method_return(msg, "b", 1);
}

static int method_set_color(sd_bus_message *msg, void *userdata,
                             sd_bus_error *error)
{
    f87d_dbus_ctx_t *ctx = userdata;
    f87d_idle_touch(ctx->idle);
    uint8_t r, g, b;

    int rc = sd_bus_message_read(msg, "yyy", &r, &g, &b);
    if (rc < 0)
        return sd_bus_error_set_errno(error, -rc);

    f87_device *dev = f87d_devmgr_get_device(ctx->devmgr);
    if (!dev)
        return sd_bus_reply_method_errorf(msg,
            "org.f87.Error.NotConnected", "No keyboard connected");

    f87_set_all_keys(dev, (f87_color){r, g, b});
    rc = f87_apply(dev);
    if (rc < 0)
        return sd_bus_reply_method_errorf(msg,
            "org.f87.Error.USBError", "Failed to set color: %d", rc);

    return sd_bus_reply_method_return(msg, "b", 1);
}

static int method_stop(sd_bus_message *msg, void *userdata,
                        sd_bus_error *error)
{
    (void)error;
    f87d_dbus_ctx_t *ctx = userdata;
    f87d_idle_touch(ctx->idle);

    f87d_effmgr_stop(ctx->effmgr);
    autosave_last(ctx);
    f87d_dbus_emit_effect_changed(ctx, -1, "");
    return sd_bus_reply_method_return(msg, "b", 1);
}

static int method_off(sd_bus_message *msg, void *userdata,
                       sd_bus_error *error)
{
    f87d_dbus_ctx_t *ctx = userdata;
    f87d_idle_touch(ctx->idle);

    f87d_effmgr_stop(ctx->effmgr);

    f87_device *dev = f87d_devmgr_get_device(ctx->devmgr);
    if (!dev)
        return sd_bus_reply_method_errorf(msg,
            "org.f87.Error.NotConnected", "No keyboard connected");

    int rc = f87_lights_off(dev);
    if (rc < 0)
        return sd_bus_reply_method_errorf(msg,
            "org.f87.Error.USBError", "Failed to turn off: %d", rc);

    autosave_last(ctx);
    f87d_dbus_emit_effect_changed(ctx, 0, "hw");
    return sd_bus_reply_method_return(msg, "b", 1);
}

static int method_rescan(sd_bus_message *msg, void *userdata,
                          sd_bus_error *error)
{
    (void)error;
    f87d_dbus_ctx_t *ctx = userdata;
    f87d_idle_touch(ctx->idle);

    int rc = f87d_devmgr_scan(ctx->devmgr, NULL);
    return sd_bus_reply_method_return(msg, "b", rc == 0);
}

static int method_get_status(sd_bus_message *msg, void *userdata,
                              sd_bus_error *error)
{
    (void)error;
    f87d_dbus_ctx_t *ctx = userdata;
    f87d_idle_touch(ctx->idle);

    sd_bus_message *reply = NULL;
    int r = sd_bus_message_new_method_return(msg, &reply);
    if (r < 0) return r;

    r = sd_bus_message_open_container(reply, 'a', "{sv}");
    if (r < 0) goto fail;

    r = sd_bus_message_append(reply, "{sv}", "Connected", "b",
                               (int)ctx->devmgr->connected);
    if (r < 0) goto fail;

    r = sd_bus_message_append(reply, "{sv}", "ActiveEffect", "i",
                               (int32_t)ctx->effmgr->effect_id);
    if (r < 0) goto fail;

    r = sd_bus_message_append(reply, "{sv}", "Category", "s",
                               f87d_effmgr_category_str(ctx->effmgr->category));
    if (r < 0) goto fail;

    r = sd_bus_message_append(reply, "{sv}", "IsWireless", "b",
                               (int)(ctx->devmgr->connected ?
                                     ctx->devmgr->connected_info.is_wireless : 0));
    if (r < 0) goto fail;

    int32_t battery = -1;
    f87_device *status_dev = f87d_devmgr_get_device(ctx->devmgr);
    if (status_dev)
        battery = f87_get_battery_level(status_dev);
    r = sd_bus_message_append(reply, "{sv}", "BatteryLevel", "i", battery);
    if (r < 0) goto fail;

    r = sd_bus_message_close_container(reply);
    if (r < 0) goto fail;

    return sd_bus_send(NULL, reply, NULL);

fail:
    sd_bus_message_unref(reply);
    return r;
}

/* ---- Properties ---- */

static int prop_get_connected(sd_bus *bus, const char *path,
                               const char *iface, const char *property,
                               sd_bus_message *reply, void *userdata,
                               sd_bus_error *error)
{
    (void)bus; (void)path; (void)iface; (void)property; (void)error;
    f87d_dbus_ctx_t *ctx = userdata;
    return sd_bus_message_append(reply, "b", (int)ctx->devmgr->connected);
}

static int prop_get_active_effect(sd_bus *bus, const char *path,
                                   const char *iface, const char *property,
                                   sd_bus_message *reply, void *userdata,
                                   sd_bus_error *error)
{
    (void)bus; (void)path; (void)iface; (void)property; (void)error;
    f87d_dbus_ctx_t *ctx = userdata;
    return sd_bus_message_append(reply, "i", (int32_t)ctx->effmgr->effect_id);
}

static int prop_get_category(sd_bus *bus, const char *path,
                              const char *iface, const char *property,
                              sd_bus_message *reply, void *userdata,
                              sd_bus_error *error)
{
    (void)bus; (void)path; (void)iface; (void)property; (void)error;
    f87d_dbus_ctx_t *ctx = userdata;
    return sd_bus_message_append(reply, "s",
        f87d_effmgr_category_str(ctx->effmgr->category));
}

static int prop_get_brightness(sd_bus *bus, const char *path,
                                const char *iface, const char *property,
                                sd_bus_message *reply, void *userdata,
                                sd_bus_error *error)
{
    (void)bus; (void)path; (void)iface; (void)property; (void)error;
    f87d_dbus_ctx_t *ctx = userdata;
    return sd_bus_message_append(reply, "y", ctx->effmgr->brightness);
}

static int method_get_battery_level(sd_bus_message *msg, void *userdata,
                                     sd_bus_error *error)
{
    (void)error;
    f87d_dbus_ctx_t *ctx = userdata;
    f87d_idle_touch(ctx->idle);

    f87_device *dev = f87d_devmgr_get_device(ctx->devmgr);
    int32_t level = dev ? f87_get_battery_level(dev) : -1;

    return sd_bus_reply_method_return(msg, "i", level);
}

static int method_set_per_key_colors(sd_bus_message *msg, void *userdata,
                                      sd_bus_error *error)
{
    f87d_dbus_ctx_t *ctx = userdata;
    f87d_idle_touch(ctx->idle);

    f87_device *dev = f87d_devmgr_get_device(ctx->devmgr);
    if (!dev)
        return sd_bus_reply_method_errorf(msg,
            "org.f87.Error.NotConnected", "No keyboard connected");

    int rc = sd_bus_message_enter_container(msg, 'a', "(yyy)");
    if (rc < 0) return sd_bus_error_set_errno(error, -rc);

    int i = 0;
    uint8_t r, g, b;
    while (sd_bus_message_read(msg, "(yyy)", &r, &g, &b) > 0 && i < 88) {
        f87_set_key_color(dev, (uint8_t)i, (f87_color){r, g, b});
        i++;
    }
    sd_bus_message_exit_container(msg);

    rc = f87_apply(dev);
    if (rc < 0)
        return sd_bus_reply_method_errorf(msg,
            "org.f87.Error.USBError", "Failed to apply per-key colors: %d", rc);

    ctx->effmgr->category = F87D_CAT_HW;
    ctx->effmgr->effect_id = 18; /* Custom */
    autosave_last(ctx);
    f87d_dbus_emit_effect_changed(ctx, 18, "hw");
    return sd_bus_reply_method_return(msg, "b", 1);
}

/* ---- Side/battery light methods ---- */

static int method_set_side_light(sd_bus_message *msg, void *userdata,
                                  sd_bus_error *error)
{
    f87d_dbus_ctx_t *ctx = userdata;
    f87d_idle_touch(ctx->idle);
    uint8_t mode;

    int rc = sd_bus_message_read(msg, "y", &mode);
    if (rc < 0) return sd_bus_error_set_errno(error, -rc);

    f87_device *dev = f87d_devmgr_get_device(ctx->devmgr);
    if (!dev)
        return sd_bus_reply_method_errorf(msg,
            "org.f87.Error.NotConnected", "No keyboard connected");

    rc = f87d_effmgr_set_side_light(ctx->effmgr, dev, mode);
    if (rc < 0)
        return sd_bus_reply_method_errorf(msg,
            "org.f87.Error.USBError", "Failed: %d", rc);

    autosave_last(ctx);
    return sd_bus_reply_method_return(msg, "b", 1);
}

static int method_set_battery_light(sd_bus_message *msg, void *userdata,
                                     sd_bus_error *error)
{
    f87d_dbus_ctx_t *ctx = userdata;
    f87d_idle_touch(ctx->idle);
    uint8_t mode;

    int rc = sd_bus_message_read(msg, "y", &mode);
    if (rc < 0) return sd_bus_error_set_errno(error, -rc);

    f87_device *dev = f87d_devmgr_get_device(ctx->devmgr);
    if (!dev)
        return sd_bus_reply_method_errorf(msg,
            "org.f87.Error.NotConnected", "No keyboard connected");

    rc = f87d_effmgr_set_battery_light(ctx->effmgr, dev, mode);
    if (rc < 0)
        return sd_bus_reply_method_errorf(msg,
            "org.f87.Error.USBError", "Failed: %d", rc);

    autosave_last(ctx);
    return sd_bus_reply_method_return(msg, "b", 1);
}

/* ---- Profile methods ---- */

static int method_save_profile(sd_bus_message *msg, void *userdata,
                                sd_bus_error *error)
{
    f87d_dbus_ctx_t *ctx = userdata;
    f87d_idle_touch(ctx->idle);
    const char *name = NULL;

    int rc = sd_bus_message_read(msg, "s", &name);
    if (rc < 0) return sd_bus_error_set_errno(error, -rc);

    if (f87d_profile_validate_name(name) < 0)
        return sd_bus_reply_method_errorf(msg,
            "org.f87.Error.InvalidName", "Invalid profile name: %s", name);

    f87d_profile_t p;
    snapshot_to_profile(ctx, &p);
    strncpy(p.name, name, sizeof(p.name) - 1);

    rc = f87d_profile_save(&p, name);
    if (rc < 0)
        return sd_bus_reply_method_errorf(msg,
            "org.f87.Error.IOError", "Failed to save profile");

    printf("f87d: profile saved: %s\n", name);
    return sd_bus_reply_method_return(msg, "b", 1);
}

static int method_load_profile(sd_bus_message *msg, void *userdata,
                                sd_bus_error *error)
{
    f87d_dbus_ctx_t *ctx = userdata;
    f87d_idle_touch(ctx->idle);
    const char *name = NULL;

    int rc = sd_bus_message_read(msg, "s", &name);
    if (rc < 0) return sd_bus_error_set_errno(error, -rc);

    f87d_profile_t p;
    rc = f87d_profile_load(name, &p);
    if (rc < 0)
        return sd_bus_reply_method_errorf(msg,
            "org.f87.Error.NotFound", "Profile not found: %s", name);

    f87_device *dev = f87d_devmgr_get_device(ctx->devmgr);
    if (!dev)
        return sd_bus_reply_method_errorf(msg,
            "org.f87.Error.NotConnected", "No keyboard connected");

    if (strcmp(p.category, "hw") == 0) {
        f87d_effmgr_set_hw(ctx->effmgr, dev, p.effect_id, p.brightness,
                            p.speed, p.colorful, p.color[0], p.color[1], p.color[2]);
    } else if (strcmp(p.category, "sw") == 0) {
        f87d_effmgr_set_sw(ctx->effmgr, dev, p.effect_id, p.brightness,
                            p.speed, p.color[0], p.color[1], p.color[2], 0);
    } else if (strcmp(p.category, "music") == 0) {
        f87d_effmgr_set_music(ctx->effmgr, dev, p.effect_id, p.brightness,
                               p.color[0], p.color[1], p.color[2], p.gain);
    } else if (strcmp(p.category, "sensor") == 0) {
        f87d_effmgr_set_sensor(ctx->effmgr, dev,
                                p.sensor_profile[0] ? p.sensor_profile : NULL,
                                p.sensor_config_path[0] ? p.sensor_config_path : NULL);
    }

    if (p.side_light <= 4)
        f87d_effmgr_set_side_light(ctx->effmgr, dev, p.side_light);
    if (p.battery_light <= 4)
        f87d_effmgr_set_battery_light(ctx->effmgr, dev, p.battery_light);

    autosave_last(ctx);
    f87d_dbus_emit_effect_changed(ctx, p.effect_id, p.category);
    printf("f87d: profile loaded: %s\n", name);
    return sd_bus_reply_method_return(msg, "b", 1);
}

static int method_delete_profile(sd_bus_message *msg, void *userdata,
                                  sd_bus_error *error)
{
    f87d_dbus_ctx_t *ctx = userdata;
    f87d_idle_touch(ctx->idle);
    const char *name = NULL;

    int rc = sd_bus_message_read(msg, "s", &name);
    if (rc < 0) return sd_bus_error_set_errno(error, -rc);

    f87d_profile_delete(name);
    printf("f87d: profile deleted: %s\n", name);
    return sd_bus_reply_method_return(msg, "b", 1);
}

static int method_list_profiles(sd_bus_message *msg, void *userdata,
                                 sd_bus_error *error)
{
    (void)error;
    f87d_dbus_ctx_t *ctx = userdata;
    f87d_idle_touch(ctx->idle);

    char **names = NULL;
    int count = f87d_profile_list(&names);

    sd_bus_message *reply = NULL;
    int r = sd_bus_message_new_method_return(msg, &reply);
    if (r < 0) goto done;

    r = sd_bus_message_open_container(reply, 'a', "s");
    if (r < 0) goto done;

    for (int i = 0; i < count; i++) {
        r = sd_bus_message_append(reply, "s", names[i]);
        if (r < 0) goto done;
    }

    r = sd_bus_message_close_container(reply);
    if (r < 0) goto done;

    r = sd_bus_send(NULL, reply, NULL);

done:
    f87d_profile_free_list(names, count);
    return r;
}

/* ---- Error history / log level methods ---- */

static int method_get_error_history(sd_bus_message *msg, void *userdata,
                                     sd_bus_error *error)
{
    (void)error;
    f87d_dbus_ctx_t *ctx = userdata;

    f87_log_entry_t entries[F87D_ERROR_RING_SIZE];
    int count = f87d_error_ring_get_all(ctx->error_ring, entries,
                                         F87D_ERROR_RING_SIZE);

    sd_bus_message *reply = NULL;
    int r = sd_bus_message_new_method_return(msg, &reply);
    if (r < 0) return r;

    r = sd_bus_message_open_container(reply, 'a', "(tsssis)");
    if (r < 0) goto fail;

    for (int i = 0; i < count; i++) {
        r = sd_bus_message_append(reply, "(tsssis)",
            entries[i].timestamp_us,
            f87_log_level_to_string(entries[i].level),
            f87_log_source_to_string(entries[i].source),
            entries[i].message,
            (int32_t)entries[i].error_code,
            entries[i].error_code != 0 ?
                f87_strerror(entries[i].error_code) : "");
        if (r < 0) goto fail;
    }

    r = sd_bus_message_close_container(reply);
    if (r < 0) goto fail;

    return sd_bus_send(NULL, reply, NULL);

fail:
    sd_bus_message_unref(reply);
    return r;
}

static int method_clear_error_history(sd_bus_message *msg, void *userdata,
                                       sd_bus_error *error)
{
    (void)error;
    f87d_dbus_ctx_t *ctx = userdata;
    f87d_error_ring_clear(ctx->error_ring);
    return sd_bus_reply_method_return(msg, "b", 1);
}

static int method_set_log_level(sd_bus_message *msg, void *userdata,
                                 sd_bus_error *error)
{
    (void)userdata;
    const char *level_str = NULL;

    int rc = sd_bus_message_read(msg, "s", &level_str);
    if (rc < 0)
        return sd_bus_error_set_errno(error, -rc);

    int level = f87_log_level_from_string(level_str);
    if (level < 0)
        return sd_bus_reply_method_errorf(msg,
            "org.f87.Error.InvalidParam",
            "Invalid log level: %s", level_str);

    f87_log_set_level(level);
    F87_INFO(F87_SRC_DBUS, "Log level changed to %s", level_str);
    return sd_bus_reply_method_return(msg, "b", 1);
}

static int method_get_log_level(sd_bus_message *msg, void *userdata,
                                 sd_bus_error *error)
{
    (void)userdata;
    (void)error;
    return sd_bus_reply_method_return(msg, "s",
        f87_log_level_to_string(f87_log_get_level()));
}

/* ---- Side/battery light properties ---- */

static int prop_get_side_light(sd_bus *bus, const char *path,
                                const char *iface, const char *property,
                                sd_bus_message *reply, void *userdata,
                                sd_bus_error *error)
{
    (void)bus; (void)path; (void)iface; (void)property; (void)error;
    f87d_dbus_ctx_t *ctx = userdata;
    return sd_bus_message_append(reply, "y", ctx->effmgr->side_light);
}

static int prop_get_battery_light(sd_bus *bus, const char *path,
                                   const char *iface, const char *property,
                                   sd_bus_message *reply, void *userdata,
                                   sd_bus_error *error)
{
    (void)bus; (void)path; (void)iface; (void)property; (void)error;
    f87d_dbus_ctx_t *ctx = userdata;
    return sd_bus_message_append(reply, "y", ctx->effmgr->battery_light);
}

static int prop_get_is_wireless(sd_bus *bus, const char *path,
                                const char *iface, const char *property,
                                sd_bus_message *reply, void *userdata,
                                sd_bus_error *error)
{
    (void)bus; (void)path; (void)iface; (void)property; (void)error;
    f87d_dbus_ctx_t *ctx = userdata;
    int val = ctx->devmgr->connected ? ctx->devmgr->connected_info.is_wireless : 0;
    return sd_bus_message_append(reply, "b", val);
}

static int prop_get_battery_level(sd_bus *bus, const char *path,
                                   const char *iface, const char *property,
                                   sd_bus_message *reply, void *userdata,
                                   sd_bus_error *error)
{
    (void)bus; (void)path; (void)iface; (void)property; (void)error;
    f87d_dbus_ctx_t *ctx = userdata;
    f87_device *dev = f87d_devmgr_get_device(ctx->devmgr);
    int32_t level = dev ? f87_get_battery_level(dev) : -1;
    return sd_bus_message_append(reply, "i", level);
}

/* ---- vtable ---- */

static const sd_bus_vtable f87_vtable[] = {
    SD_BUS_VTABLE_START(0),

    SD_BUS_METHOD("SetEffect", "iyybyyy", "b", method_set_effect,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SetSwEffect", "iyyyyyi", "b", method_set_sw_effect,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SetMusicEffect", "iyyyd", "b", method_set_music_effect,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SetSensorEffect", "ss", "b", method_set_sensor_effect,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SetBrightness", "y", "b", method_set_brightness,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SetColor", "yyy", "b", method_set_color,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Stop", "", "b", method_stop,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Off", "", "b", method_off,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Rescan", "", "b", method_rescan,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetStatus", "", "a{sv}", method_get_status,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SetSideLight", "y", "b", method_set_side_light,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SetBatteryLight", "y", "b", method_set_battery_light,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SaveProfile", "s", "b", method_save_profile,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("LoadProfile", "s", "b", method_load_profile,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("DeleteProfile", "s", "b", method_delete_profile,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("ListProfiles", "", "as", method_list_profiles,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SetPerKeyColors", "a(yyy)", "b", method_set_per_key_colors,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetBatteryLevel", "", "i", method_get_battery_level,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetErrorHistory", "", "a(tsssis)",
                  method_get_error_history, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("ClearErrorHistory", "", "b",
                  method_clear_error_history, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SetLogLevel", "s", "b",
                  method_set_log_level, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetLogLevel", "", "s",
                  method_get_log_level, SD_BUS_VTABLE_UNPRIVILEGED),

    SD_BUS_SIGNAL("DeviceConnected", "sqq", 0),
    SD_BUS_SIGNAL("DeviceDisconnected", "", 0),
    SD_BUS_SIGNAL("EffectChanged", "is", 0),

    SD_BUS_PROPERTY("Connected", "b", prop_get_connected, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("ActiveEffect", "i", prop_get_active_effect, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("ActiveCategory", "s", prop_get_category, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Brightness", "y", prop_get_brightness, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("SideLight", "y", prop_get_side_light, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("BatteryLight", "y", prop_get_battery_light, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("IsWireless", "b", prop_get_is_wireless, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("BatteryLevel", "i", prop_get_battery_level, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),

    SD_BUS_VTABLE_END,
};

/* ---- Public functions ---- */

int f87d_dbus_register(sd_bus *bus, f87d_dbus_ctx_t *ctx)
{
    return sd_bus_add_object_vtable(bus, NULL, DBUS_PATH, DBUS_IFACE,
                                    f87_vtable, ctx);
}

int f87d_dbus_emit_device_connected(f87d_dbus_ctx_t *ctx,
                                     const char *product,
                                     uint16_t vid, uint16_t pid)
{
    return sd_bus_emit_signal(ctx->bus, DBUS_PATH, DBUS_IFACE,
                              "DeviceConnected", "sqq", product, vid, pid);
}

int f87d_dbus_emit_device_disconnected(f87d_dbus_ctx_t *ctx)
{
    return sd_bus_emit_signal(ctx->bus, DBUS_PATH, DBUS_IFACE,
                              "DeviceDisconnected", "");
}

int f87d_dbus_emit_effect_changed(f87d_dbus_ctx_t *ctx,
                                   int effect_id, const char *category)
{
    return sd_bus_emit_signal(ctx->bus, DBUS_PATH, DBUS_IFACE,
                              "EffectChanged", "is",
                              (int32_t)effect_id, category);
}
