#include "f87/client.h"
#include "f87/logger.h"
#include <systemd/sd-bus.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#define DBUS_DEST  "org.f87.Control"
#define DBUS_PATH  "/org/f87/Control"
#define DBUS_IFACE "org.f87.Control"

struct f87_client {
    sd_bus *bus;
    sd_bus_slot *device_slot;
    sd_bus_slot *device_slot2;
    sd_bus_slot *effect_slot;
    f87_client_device_cb device_cb;
    void *device_cb_data;
    f87_client_effect_cb effect_cb;
    void *effect_cb_data;
};

f87_client *f87_client_connect(void)
{
    f87_client *c = calloc(1, sizeof(*c));
    if (!c) return NULL;

    int r = sd_bus_open_user(&c->bus);
    if (r < 0) {
        free(c);
        return NULL;
    }

    return c;
}

void f87_client_disconnect(f87_client *client)
{
    if (!client) return;
    if (client->device_slot)
        sd_bus_slot_unref(client->device_slot);
    if (client->device_slot2)
        sd_bus_slot_unref(client->device_slot2);
    if (client->effect_slot)
        sd_bus_slot_unref(client->effect_slot);
    if (client->bus)
        sd_bus_unref(client->bus);
    free(client);
}

/* Helper: call a method, read boolean return. Returns 0 on success. */
static int call_bool(f87_client *c, const char *method,
                      const char *types, ...)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *msg = NULL;
    sd_bus_message *reply = NULL;

    int r = sd_bus_message_new_method_call(c->bus, &msg,
        DBUS_DEST, DBUS_PATH, DBUS_IFACE, method);
    if (r < 0) return -1;

    if (types && types[0]) {
        va_list ap;
        va_start(ap, types);
        r = sd_bus_message_appendv(msg, types, ap);
        va_end(ap);
        if (r < 0) {
            sd_bus_message_unref(msg);
            return -1;
        }
    }

    r = sd_bus_call(c->bus, msg, 0, &error, &reply);
    sd_bus_message_unref(msg);
    if (r < 0) {
        sd_bus_error_free(&error);
        return -1;
    }

    int result = 0;
    sd_bus_message_read(reply, "b", &result);
    sd_bus_message_unref(reply);
    sd_bus_error_free(&error);
    return result ? 0 : -1;
}

int f87_client_set_effect(f87_client *client, int effect_id,
                           uint8_t brightness, uint8_t speed,
                           uint8_t colorful, uint8_t r, uint8_t g, uint8_t b)
{
    return call_bool(client, "SetEffect", "iyybyyy",
                     (int32_t)effect_id, brightness, speed,
                     (int)colorful, r, g, b);
}

int f87_client_set_sw_effect(f87_client *client, int effect_id,
                              uint8_t brightness, uint8_t speed,
                              uint8_t r, uint8_t g, uint8_t b, int fps)
{
    return call_bool(client, "SetSwEffect", "iyyyyyi",
                     (int32_t)effect_id, brightness, speed,
                     r, g, b, (int32_t)fps);
}

int f87_client_set_music_effect(f87_client *client, int effect_id,
                                 uint8_t brightness,
                                 uint8_t r, uint8_t g, uint8_t b, double gain)
{
    return call_bool(client, "SetMusicEffect", "iyyyd",
                     (int32_t)effect_id, brightness, r, g, b, gain);
}

int f87_client_set_sensor_effect(f87_client *client,
                                  const char *profile,
                                  const char *config_path)
{
    return call_bool(client, "SetSensorEffect", "ss",
                     profile ? profile : "",
                     config_path ? config_path : "");
}

int f87_client_set_color(f87_client *client, uint8_t r, uint8_t g, uint8_t b)
{
    return call_bool(client, "SetColor", "yyy", r, g, b);
}

int f87_client_set_brightness(f87_client *client, uint8_t level)
{
    return call_bool(client, "SetBrightness", "y", level);
}

int f87_client_stop(f87_client *client)
{
    return call_bool(client, "Stop", NULL);
}

int f87_client_off(f87_client *client)
{
    return call_bool(client, "Off", NULL);
}

int f87_client_rescan(f87_client *client)
{
    return call_bool(client, "Rescan", NULL);
}

int f87_client_get_status(f87_client *client, f87_client_status_t *status)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;

    int r = sd_bus_call_method(client->bus, DBUS_DEST, DBUS_PATH, DBUS_IFACE,
                               "GetStatus", &error, &reply, "");
    if (r < 0) {
        sd_bus_error_free(&error);
        return -1;
    }

    memset(status, 0, sizeof(*status));

    r = sd_bus_message_enter_container(reply, 'a', "{sv}");
    if (r < 0) goto done;

    while ((r = sd_bus_message_enter_container(reply, 'e', "sv")) > 0) {
        const char *key = NULL;
        sd_bus_message_read(reply, "s", &key);

        if (strcmp(key, "Connected") == 0) {
            int val = 0;
            sd_bus_message_read(reply, "v", "b", &val);
            status->connected = val;
        } else if (strcmp(key, "ActiveEffect") == 0) {
            int32_t val = 0;
            sd_bus_message_read(reply, "v", "i", &val);
            status->active_effect = val;
        } else if (strcmp(key, "Category") == 0) {
            const char *val = NULL;
            sd_bus_message_read(reply, "v", "s", &val);
            if (val)
                strncpy(status->category, val, sizeof(status->category) - 1);
        } else {
            sd_bus_message_skip(reply, "v");
        }

        sd_bus_message_exit_container(reply);
    }

    sd_bus_message_exit_container(reply);

done:
    sd_bus_message_unref(reply);
    sd_bus_error_free(&error);
    return 0;
}

int f87_client_is_connected(f87_client *client)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    int val = 0;

    int r = sd_bus_get_property_trivial(client->bus, DBUS_DEST, DBUS_PATH,
                                         DBUS_IFACE, "Connected", &error,
                                         'b', &val);
    sd_bus_error_free(&error);
    return r < 0 ? -1 : val;
}

int f87_client_get_battery(f87_client *client)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;

    int r = sd_bus_call_method(client->bus, DBUS_DEST, DBUS_PATH, DBUS_IFACE,
                               "GetBatteryLevel", &error, &reply, "");
    if (r < 0) {
        sd_bus_error_free(&error);
        return -1;
    }

    int32_t level = -1;
    sd_bus_message_read(reply, "i", &level);
    sd_bus_message_unref(reply);
    sd_bus_error_free(&error);
    return level;
}

int f87_client_is_wireless(f87_client *client)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    int val = 0;

    int r = sd_bus_get_property_trivial(client->bus, DBUS_DEST, DBUS_PATH,
                                         DBUS_IFACE, "IsWireless", &error,
                                         'b', &val);
    sd_bus_error_free(&error);
    return r < 0 ? -1 : val;
}

int f87_client_set_per_key_colors(f87_client *client,
                                   const uint8_t colors[][3], int count)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *msg = NULL;
    sd_bus_message *reply = NULL;

    int r = sd_bus_message_new_method_call(client->bus, &msg,
        DBUS_DEST, DBUS_PATH, DBUS_IFACE, "SetPerKeyColors");
    if (r < 0) return -1;

    r = sd_bus_message_open_container(msg, 'a', "(yyy)");
    if (r < 0) { sd_bus_message_unref(msg); return -1; }

    for (int i = 0; i < count; i++) {
        r = sd_bus_message_append(msg, "(yyy)",
                                   colors[i][0], colors[i][1], colors[i][2]);
        if (r < 0) { sd_bus_message_unref(msg); return -1; }
    }

    r = sd_bus_message_close_container(msg);
    if (r < 0) { sd_bus_message_unref(msg); return -1; }

    r = sd_bus_call(client->bus, msg, 0, &error, &reply);
    sd_bus_message_unref(msg);
    if (r < 0) { sd_bus_error_free(&error); return -1; }

    int result = 0;
    sd_bus_message_read(reply, "b", &result);
    sd_bus_message_unref(reply);
    sd_bus_error_free(&error);
    return result ? 0 : -1;
}

int f87_client_set_side_light(f87_client *client, uint8_t mode)
{
    return call_bool(client, "SetSideLight", "y", mode);
}

int f87_client_set_battery_light(f87_client *client, uint8_t mode)
{
    return call_bool(client, "SetBatteryLight", "y", mode);
}

int f87_client_save_profile(f87_client *client, const char *name)
{
    return call_bool(client, "SaveProfile", "s", name);
}

int f87_client_load_profile(f87_client *client, const char *name)
{
    return call_bool(client, "LoadProfile", "s", name);
}

int f87_client_delete_profile(f87_client *client, const char *name)
{
    return call_bool(client, "DeleteProfile", "s", name);
}

int f87_client_list_profiles(f87_client *client, char ***names, int *count)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;

    int r = sd_bus_call_method(client->bus, DBUS_DEST, DBUS_PATH, DBUS_IFACE,
                               "ListProfiles", &error, &reply, "");
    if (r < 0) {
        sd_bus_error_free(&error);
        *names = NULL;
        *count = 0;
        return -1;
    }

    r = sd_bus_message_enter_container(reply, 'a', "s");
    if (r < 0) { sd_bus_message_unref(reply); sd_bus_error_free(&error); return -1; }

    int n = 0, cap = 16;
    char **list = malloc((size_t)cap * sizeof(char *));
    if (!list) { sd_bus_message_unref(reply); sd_bus_error_free(&error); return -1; }

    const char *name_str = NULL;
    while (sd_bus_message_read(reply, "s", &name_str) > 0) {
        if (n >= cap) {
            cap *= 2;
            char **tmp = realloc(list, (size_t)cap * sizeof(char *));
            if (!tmp) break;
            list = tmp;
        }
        list[n++] = strdup(name_str);
    }

    sd_bus_message_exit_container(reply);
    sd_bus_message_unref(reply);
    sd_bus_error_free(&error);

    *names = list;
    *count = n;
    return 0;
}

void f87_client_free_profile_list(char **names, int count)
{
    if (!names) return;
    for (int i = 0; i < count; i++)
        free(names[i]);
    free(names);
}

/* ---- Error history / log level ---- */

int f87_client_get_error_history(f87_client *client,
                                  f87_log_entry_t *entries, int max_entries)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;

    int r = sd_bus_call_method(client->bus, DBUS_DEST, DBUS_PATH, DBUS_IFACE,
                               "GetErrorHistory", &error, &reply, "");
    if (r < 0) {
        sd_bus_error_free(&error);
        return -1;
    }

    r = sd_bus_message_enter_container(reply, 'a', "(tsssis)");
    if (r < 0) goto done;

    int count = 0;
    uint64_t ts;
    const char *level_str, *source_str, *msg_str, *strerr;
    int32_t ec;

    while (sd_bus_message_read(reply, "(tsssis)",
           &ts, &level_str, &source_str, &msg_str, &ec, &strerr) > 0) {
        if (count >= max_entries) break;
        entries[count].timestamp_us = ts;
        entries[count].level = f87_log_level_from_string(level_str);
        entries[count].error_code = ec;
        strncpy(entries[count].message, msg_str,
                sizeof(entries[count].message) - 1);
        entries[count].message[sizeof(entries[count].message) - 1] = '\0';
        if (strcmp(source_str, "USB") == 0)         entries[count].source = F87_SRC_USB;
        else if (strcmp(source_str, "AUDIO") == 0)  entries[count].source = F87_SRC_AUDIO;
        else if (strcmp(source_str, "DBUS") == 0)   entries[count].source = F87_SRC_DBUS;
        else if (strcmp(source_str, "DEVICE") == 0) entries[count].source = F87_SRC_DEVICE;
        else if (strcmp(source_str, "EFFECT") == 0) entries[count].source = F87_SRC_EFFECT;
        else if (strcmp(source_str, "GUI") == 0)    entries[count].source = F87_SRC_GUI;
        count++;
    }

    sd_bus_message_exit_container(reply);

done:
    sd_bus_message_unref(reply);
    sd_bus_error_free(&error);
    return count;
}

int f87_client_clear_error_history(f87_client *client)
{
    return call_bool(client, "ClearErrorHistory", NULL);
}

int f87_client_set_log_level(f87_client *client, const char *level)
{
    return call_bool(client, "SetLogLevel", "s", level);
}

int f87_client_get_log_level(f87_client *client, char *out, int out_size)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;

    int r = sd_bus_call_method(client->bus, DBUS_DEST, DBUS_PATH, DBUS_IFACE,
                               "GetLogLevel", &error, &reply, "");
    if (r < 0) {
        sd_bus_error_free(&error);
        return -1;
    }

    const char *val = NULL;
    sd_bus_message_read(reply, "s", &val);
    if (val)
        strncpy(out, val, (size_t)(out_size - 1));
    sd_bus_message_unref(reply);
    sd_bus_error_free(&error);
    return 0;
}

/* Signal match callbacks */
static int on_device_signal(sd_bus_message *msg, void *userdata,
                             sd_bus_error *error)
{
    (void)error;
    f87_client *c = userdata;
    if (!c->device_cb) return 0;

    const char *member = sd_bus_message_get_member(msg);
    if (strcmp(member, "DeviceConnected") == 0) {
        const char *product = NULL;
        uint16_t vid = 0, pid = 0;
        sd_bus_message_read(msg, "sqq", &product, &vid, &pid);
        c->device_cb(true, product, c->device_cb_data);
    } else if (strcmp(member, "DeviceDisconnected") == 0) {
        c->device_cb(false, NULL, c->device_cb_data);
    }
    return 0;
}

static int on_effect_signal(sd_bus_message *msg, void *userdata,
                             sd_bus_error *error)
{
    (void)error;
    f87_client *c = userdata;
    if (!c->effect_cb) return 0;

    int32_t effect_id = 0;
    const char *category = NULL;
    sd_bus_message_read(msg, "is", &effect_id, &category);
    c->effect_cb(effect_id, category, c->effect_cb_data);
    return 0;
}

int f87_client_on_device_change(f87_client *client,
                                 f87_client_device_cb cb, void *userdata)
{
    client->device_cb = cb;
    client->device_cb_data = userdata;

    if (client->device_slot) {
        sd_bus_slot_unref(client->device_slot);
        client->device_slot = NULL;
    }
    if (client->device_slot2) {
        sd_bus_slot_unref(client->device_slot2);
        client->device_slot2 = NULL;
    }

    int r = sd_bus_match_signal(client->bus, &client->device_slot,
                                 DBUS_DEST, DBUS_PATH, DBUS_IFACE,
                                 "DeviceConnected", on_device_signal, client);
    if (r < 0) return -1;

    r = sd_bus_match_signal(client->bus, &client->device_slot2,
                             DBUS_DEST, DBUS_PATH, DBUS_IFACE,
                             "DeviceDisconnected", on_device_signal, client);
    return r < 0 ? -1 : 0;
}

int f87_client_on_effect_change(f87_client *client,
                                 f87_client_effect_cb cb, void *userdata)
{
    client->effect_cb = cb;
    client->effect_cb_data = userdata;

    if (client->effect_slot) {
        sd_bus_slot_unref(client->effect_slot);
        client->effect_slot = NULL;
    }

    return sd_bus_match_signal(client->bus, &client->effect_slot,
                                DBUS_DEST, DBUS_PATH, DBUS_IFACE,
                                "EffectChanged", on_effect_signal, client);
}

int f87_client_process(f87_client *client)
{
    int r;
    while ((r = sd_bus_process(client->bus, NULL)) > 0)
        ;
    return 0;
}

int f87_client_get_fd(f87_client *client)
{
    return sd_bus_get_fd(client->bus);
}
