#ifndef F87D_PROFILE_MANAGER_H
#define F87D_PROFILE_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define F87D_PROFILE_NAME_MAX 64
#define F87D_PROFILE_KEY_COUNT 88

typedef struct {
    char name[F87D_PROFILE_NAME_MAX];
    char category[16];
    int effect_id;
    uint8_t brightness;
    uint8_t speed;
    uint8_t colorful;
    uint8_t color[3];
    uint8_t side_light;
    uint8_t battery_light;
    double gain;
    char sensor_profile[64];
    char sensor_config_path[256];
    bool has_per_key;
    uint8_t per_key_colors[F87D_PROFILE_KEY_COUNT][3];
} f87d_profile_t;

int f87d_profile_validate_name(const char *name);
char *f87d_profile_to_json(const f87d_profile_t *profile);
int f87d_profile_from_json(const char *json_str, f87d_profile_t *profile);

int f87d_profile_get_config_dir(char *buf, size_t bufsz);
int f87d_profile_get_profiles_dir(char *buf, size_t bufsz);

int f87d_profile_save(const f87d_profile_t *profile, const char *name);
int f87d_profile_load(const char *name, f87d_profile_t *profile);
int f87d_profile_delete(const char *name);
int f87d_profile_list(char ***names);
void f87d_profile_free_list(char **names, int count);

int f87d_profile_save_last(const f87d_profile_t *profile);
int f87d_profile_load_last(f87d_profile_t *profile);

#endif
