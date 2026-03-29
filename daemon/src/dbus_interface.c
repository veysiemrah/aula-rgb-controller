#include "dbus_interface.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>

#define DBUS_PATH "/org/f87/Control"
#define DBUS_IFACE "org.f87.Control"

/* ---- Method handlers ---- */

static int method_set_effect(sd_bus_message *msg, void *userdata,
                              sd_bus_error *error)
{
    f87d_dbus_ctx_t *ctx = userdata;
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

    f87d_dbus_emit_effect_changed(ctx, effect_id, "hw");
    return sd_bus_reply_method_return(msg, "b", 1);
}

static int method_set_sw_effect(sd_bus_message *msg, void *userdata,
                                 sd_bus_error *error)
{
    f87d_dbus_ctx_t *ctx = userdata;
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

    f87d_dbus_emit_effect_changed(ctx, effect_id, "sw");
    return sd_bus_reply_method_return(msg, "b", 1);
}

static int method_set_music_effect(sd_bus_message *msg, void *userdata,
                                    sd_bus_error *error)
{
    f87d_dbus_ctx_t *ctx = userdata;
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

    f87d_dbus_emit_effect_changed(ctx, effect_id, "music");
    return sd_bus_reply_method_return(msg, "b", 1);
}

static int method_set_sensor_effect(sd_bus_message *msg, void *userdata,
                                     sd_bus_error *error)
{
    f87d_dbus_ctx_t *ctx = userdata;
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

    f87d_dbus_emit_effect_changed(ctx, F87_SW_SENSOR, "sensor");
    return sd_bus_reply_method_return(msg, "b", 1);
}

static int method_set_brightness(sd_bus_message *msg, void *userdata,
                                  sd_bus_error *error)
{
    f87d_dbus_ctx_t *ctx = userdata;
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

    f87d_effmgr_stop(ctx->effmgr);
    f87d_dbus_emit_effect_changed(ctx, -1, "");
    return sd_bus_reply_method_return(msg, "b", 1);
}

static int method_off(sd_bus_message *msg, void *userdata,
                       sd_bus_error *error)
{
    f87d_dbus_ctx_t *ctx = userdata;

    f87d_effmgr_stop(ctx->effmgr);

    f87_device *dev = f87d_devmgr_get_device(ctx->devmgr);
    if (!dev)
        return sd_bus_reply_method_errorf(msg,
            "org.f87.Error.NotConnected", "No keyboard connected");

    int rc = f87_lights_off(dev);
    if (rc < 0)
        return sd_bus_reply_method_errorf(msg,
            "org.f87.Error.USBError", "Failed to turn off: %d", rc);

    f87d_dbus_emit_effect_changed(ctx, 0, "hw");
    return sd_bus_reply_method_return(msg, "b", 1);
}

static int method_rescan(sd_bus_message *msg, void *userdata,
                          sd_bus_error *error)
{
    (void)error;
    f87d_dbus_ctx_t *ctx = userdata;

    int rc = f87d_devmgr_scan(ctx->devmgr, NULL);
    return sd_bus_reply_method_return(msg, "b", rc == 0);
}

static int method_get_status(sd_bus_message *msg, void *userdata,
                              sd_bus_error *error)
{
    (void)error;
    f87d_dbus_ctx_t *ctx = userdata;

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
