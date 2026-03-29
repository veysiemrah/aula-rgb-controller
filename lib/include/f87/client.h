#ifndef F87_CLIENT_H
#define F87_CLIENT_H

#include <stdint.h>
#include <stdbool.h>
#include "device.h"
#include "animate.h"

typedef struct f87_client f87_client;

typedef struct {
    bool connected;
    int active_effect;
    char category[16];
    uint8_t brightness;
} f87_client_status_t;

f87_client *f87_client_connect(void);
void f87_client_disconnect(f87_client *client);

int f87_client_set_effect(f87_client *client, int effect_id,
                           uint8_t brightness, uint8_t speed,
                           uint8_t colorful, uint8_t r, uint8_t g, uint8_t b);
int f87_client_set_sw_effect(f87_client *client, int effect_id,
                              uint8_t brightness, uint8_t speed,
                              uint8_t r, uint8_t g, uint8_t b, int fps);
int f87_client_set_music_effect(f87_client *client, int effect_id,
                                 uint8_t brightness,
                                 uint8_t r, uint8_t g, uint8_t b, double gain);
int f87_client_set_sensor_effect(f87_client *client,
                                  const char *profile,
                                  const char *config_path);

int f87_client_set_color(f87_client *client, uint8_t r, uint8_t g, uint8_t b);
int f87_client_set_brightness(f87_client *client, uint8_t level);
int f87_client_stop(f87_client *client);
int f87_client_off(f87_client *client);

int f87_client_get_status(f87_client *client, f87_client_status_t *status);
int f87_client_is_connected(f87_client *client);
int f87_client_rescan(f87_client *client);
int f87_client_get_battery(f87_client *client);
int f87_client_is_wireless(f87_client *client);

/* Per-key custom colors: colors is 88x3 array (RGB per key) */
int f87_client_set_per_key_colors(f87_client *client,
                                   const uint8_t colors[][3], int count);

/* Side/battery light */
int f87_client_set_side_light(f87_client *client, uint8_t mode);
int f87_client_set_battery_light(f87_client *client, uint8_t mode);

/* Profiles */
int f87_client_save_profile(f87_client *client, const char *name);
int f87_client_load_profile(f87_client *client, const char *name);
int f87_client_delete_profile(f87_client *client, const char *name);
int f87_client_list_profiles(f87_client *client, char ***names, int *count);
void f87_client_free_profile_list(char **names, int count);

typedef void (*f87_client_device_cb)(bool connected, const char *product,
                                      void *userdata);
typedef void (*f87_client_effect_cb)(int effect_id, const char *category,
                                      void *userdata);

int f87_client_on_device_change(f87_client *client,
                                 f87_client_device_cb cb, void *userdata);
int f87_client_on_effect_change(f87_client *client,
                                 f87_client_effect_cb cb, void *userdata);

int f87_client_process(f87_client *client);
int f87_client_get_fd(f87_client *client);

#endif
