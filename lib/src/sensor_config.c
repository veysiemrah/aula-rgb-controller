#ifdef F87_HAS_JSON

#include "sensor_config.h"
#include <json-c/json.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static int find_key_by_name(const char *name, const f87_key_info *layout, int count)
{
    for (int i = 0; i < count; i++) {
        if (layout[i].name && strcasecmp(layout[i].name, name) == 0)
            return layout[i].key_id;
    }
    return -1;
}

static int parse_profile(struct json_object *root, f87_sensor_profile_t *profile,
                          const f87_key_info *layout, int key_count)
{
    memset(profile, 0, sizeof(*profile));

    struct json_object *jname;
    if (json_object_object_get_ex(root, "profile", &jname))
        strncpy(profile->profile_name, json_object_get_string(jname),
                sizeof(profile->profile_name) - 1);

    struct json_object *jmappings;
    if (!json_object_object_get_ex(root, "mappings", &jmappings))
        return -1;

    int n = json_object_array_length(jmappings);
    if (n > F87_SENSOR_MAP_MAX) n = F87_SENSOR_MAP_MAX;

    for (int i = 0; i < n; i++) {
        struct json_object *jmap = json_object_array_get_idx(jmappings, i);
        f87_sensor_mapping_t *m = &profile->mappings[profile->mapping_count];

        struct json_object *jsensor;
        if (!json_object_object_get_ex(jmap, "sensor", &jsensor)) continue;
        m->sensor_name = strdup(json_object_get_string(jsensor));
        if (!m->sensor_name) continue;

        if (!f87_sensor_find(m->sensor_name)) {
            fprintf(stderr, "f87: unknown sensor '%s', skipping\n", m->sensor_name);
            continue;
        }

        struct json_object *jmode;
        if (json_object_object_get_ex(jmap, "mode", &jmode)) {
            const char *mode_str = json_object_get_string(jmode);
            if (strcmp(mode_str, "bar") == 0)
                m->mode = F87_SENSOR_MODE_BAR;
            else
                m->mode = F87_SENSOR_MODE_COLOR;
        }

        struct json_object *jkeys;
        if (!json_object_object_get_ex(jmap, "keys", &jkeys)) continue;
        int nkeys = json_object_array_length(jkeys);
        if (nkeys > F87_SENSOR_KEYS_MAX) nkeys = F87_SENSOR_KEYS_MAX;

        m->key_count = 0;
        for (int k = 0; k < nkeys; k++) {
            const char *kname = json_object_get_string(
                json_object_array_get_idx(jkeys, k));
            int kid = find_key_by_name(kname, layout, key_count);
            if (kid < 0) {
                fprintf(stderr, "f87: unknown key '%s', skipping\n", kname);
                continue;
            }
            m->key_ids[m->key_count++] = kid;
        }

        if (m->key_count == 0) continue;

        struct json_object *jinterval;
        if (json_object_object_get_ex(jmap, "interval_ms", &jinterval))
            m->interval_ms = json_object_get_int(jinterval);
        else {
            const f87_sensor_t *s = f87_sensor_find(m->sensor_name);
            m->interval_ms = s ? s->default_interval_ms : 1000;
        }

        profile->mapping_count++;
    }

    return profile->mapping_count > 0 ? 0 : -1;
}

int f87_sensor_config_load(const char *path, f87_sensor_profile_t *profile,
                            const f87_key_info *layout, int key_count)
{
    struct json_object *root = json_object_from_file(path);
    if (!root) {
        fprintf(stderr, "f87: failed to load config '%s'\n", path);
        return -1;
    }

    int rc = parse_profile(root, profile, layout, key_count);
    json_object_put(root);
    return rc;
}

/* Built-in profiles as JSON strings */

static const char *builtin_developer =
    "{"
    "  \"profile\": \"developer\","
    "  \"mappings\": ["
    "    { \"sensor\": \"cpu_temp\", \"keys\": [\"F1\",\"F2\",\"F3\",\"F4\"], \"mode\": \"bar\", \"interval_ms\": 1000 },"
    "    { \"sensor\": \"cpu_load\", \"keys\": [\"F5\",\"F6\",\"F7\",\"F8\"], \"mode\": \"bar\", \"interval_ms\": 500 },"
    "    { \"sensor\": \"ram_usage\", \"keys\": [\"F9\",\"F10\",\"F11\",\"F12\"], \"mode\": \"bar\", \"interval_ms\": 1000 }"
    "  ]"
    "}";

static const char *builtin_gamer =
    "{"
    "  \"profile\": \"gamer\","
    "  \"mappings\": ["
    "    { \"sensor\": \"gpu_temp\", \"keys\": [\"F1\",\"F2\",\"F3\",\"F4\"], \"mode\": \"bar\", \"interval_ms\": 1000 },"
    "    { \"sensor\": \"cpu_temp\", \"keys\": [\"F5\",\"F6\",\"F7\",\"F8\"], \"mode\": \"bar\", \"interval_ms\": 1000 },"
    "    { \"sensor\": \"cpu_load\", \"keys\": [\"F9\",\"F10\",\"F11\",\"F12\"], \"mode\": \"bar\", \"interval_ms\": 500 }"
    "  ]"
    "}";

static const char *builtin_system =
    "{"
    "  \"profile\": \"system\","
    "  \"mappings\": ["
    "    { \"sensor\": \"cpu_temp\", \"keys\": [\"ESC\"], \"mode\": \"color\", \"interval_ms\": 1000 },"
    "    { \"sensor\": \"cpu_load\", \"keys\": [\"F1\",\"F2\",\"F3\",\"F4\",\"F5\"], \"mode\": \"bar\", \"interval_ms\": 500 },"
    "    { \"sensor\": \"gpu_temp\", \"keys\": [\"F6\"], \"mode\": \"color\", \"interval_ms\": 1000 },"
    "    { \"sensor\": \"ram_usage\", \"keys\": [\"F9\",\"F10\",\"F11\",\"F12\"], \"mode\": \"bar\", \"interval_ms\": 1000 }"
    "  ]"
    "}";

int f87_sensor_config_builtin(const char *name, f87_sensor_profile_t *profile,
                               const f87_key_info *layout, int key_count)
{
    const char *json_str = NULL;
    if (strcmp(name, "developer") == 0) json_str = builtin_developer;
    else if (strcmp(name, "gamer") == 0) json_str = builtin_gamer;
    else if (strcmp(name, "system") == 0) json_str = builtin_system;
    else return -1;

    struct json_object *root = json_tokener_parse(json_str);
    if (!root) return -1;

    int rc = parse_profile(root, profile, layout, key_count);
    json_object_put(root);
    return rc;
}

#endif /* F87_HAS_JSON */
