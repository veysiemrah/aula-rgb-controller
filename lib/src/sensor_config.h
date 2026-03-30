#ifndef F87_SENSOR_CONFIG_H
#define F87_SENSOR_CONFIG_H

#include "sensor.h"
#include "f87/lighting.h"

#define F87_SENSOR_MAP_MAX  16
#define F87_SENSOR_KEYS_MAX 20

typedef enum {
    F87_SENSOR_MODE_COLOR = 0,
    F87_SENSOR_MODE_BAR   = 1,
} f87_sensor_mode_t;

typedef struct {
    char *sensor_name;          /* owned copy — must be freed */
    int sensor_index;               /* Index into active sensors array */
    f87_sensor_mode_t mode;
    int key_ids[F87_SENSOR_KEYS_MAX];
    int key_count;
    int interval_ms;
} f87_sensor_mapping_t;

typedef struct {
    char profile_name[64];
    f87_sensor_mapping_t mappings[F87_SENSOR_MAP_MAX];
    int mapping_count;
} f87_sensor_profile_t;

/* Parse a JSON config file into a profile. */
int f87_sensor_config_load(const char *path, f87_sensor_profile_t *profile,
                            const f87_key_info *layout, int key_count);

/* Load a built-in profile by name ("developer", "gamer", "system"). */
int f87_sensor_config_builtin(const char *name, f87_sensor_profile_t *profile,
                               const f87_key_info *layout, int key_count);

/* Save profile to JSON file. Returns 0 on success, -1 on error. */
int f87_sensor_config_save(const char *path, const f87_sensor_profile_t *profile,
                            const f87_key_info *layout, int key_count);

#endif /* F87_SENSOR_CONFIG_H */
