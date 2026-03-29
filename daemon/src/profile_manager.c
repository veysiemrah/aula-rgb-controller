#include "profile_manager.h"
#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>

int f87d_profile_validate_name(const char *name)
{
    if (!name || !name[0]) return -1;
    size_t len = strlen(name);
    if (len > F87D_PROFILE_NAME_MAX) return -1;
    for (size_t i = 0; i < len; i++) {
        char c = name[i];
        if (!isalnum((unsigned char)c) && c != '_' && c != '-')
            return -1;
    }
    return 0;
}

char *f87d_profile_to_json(const f87d_profile_t *p)
{
    json_object *root = json_object_new_object();
    json_object_object_add(root, "name", json_object_new_string(p->name));
    json_object_object_add(root, "category", json_object_new_string(p->category));
    json_object_object_add(root, "effect_id", json_object_new_int(p->effect_id));
    json_object_object_add(root, "brightness", json_object_new_int(p->brightness));
    json_object_object_add(root, "speed", json_object_new_int(p->speed));
    json_object_object_add(root, "colorful", json_object_new_boolean(p->colorful));

    json_object *color = json_object_new_array_ext(3);
    json_object_array_add(color, json_object_new_int(p->color[0]));
    json_object_array_add(color, json_object_new_int(p->color[1]));
    json_object_array_add(color, json_object_new_int(p->color[2]));
    json_object_object_add(root, "color", color);

    json_object_object_add(root, "side_light", json_object_new_int(p->side_light));
    json_object_object_add(root, "battery_light", json_object_new_int(p->battery_light));

    if (p->gain > 0.0)
        json_object_object_add(root, "gain", json_object_new_double(p->gain));

    if (p->sensor_profile[0])
        json_object_object_add(root, "sensor_profile",
                                json_object_new_string(p->sensor_profile));
    if (p->sensor_config_path[0])
        json_object_object_add(root, "sensor_config_path",
                                json_object_new_string(p->sensor_config_path));

    if (p->has_per_key) {
        json_object *keys = json_object_new_array_ext(F87D_PROFILE_KEY_COUNT);
        for (int i = 0; i < F87D_PROFILE_KEY_COUNT; i++) {
            json_object *rgb = json_object_new_array_ext(3);
            json_object_array_add(rgb, json_object_new_int(p->per_key_colors[i][0]));
            json_object_array_add(rgb, json_object_new_int(p->per_key_colors[i][1]));
            json_object_array_add(rgb, json_object_new_int(p->per_key_colors[i][2]));
            json_object_array_add(keys, rgb);
        }
        json_object_object_add(root, "per_key_colors", keys);
    } else {
        json_object_object_add(root, "per_key_colors", NULL);
    }

    const char *str = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PRETTY);
    char *result = strdup(str);
    json_object_put(root);
    return result;
}

int f87d_profile_from_json(const char *json_str, f87d_profile_t *p)
{
    memset(p, 0, sizeof(*p));
    p->effect_id = -1;

    json_object *root = json_tokener_parse(json_str);
    if (!root) return -1;

    json_object *val;
    if (json_object_object_get_ex(root, "name", &val))
        strncpy(p->name, json_object_get_string(val), sizeof(p->name) - 1);
    if (json_object_object_get_ex(root, "category", &val))
        strncpy(p->category, json_object_get_string(val), sizeof(p->category) - 1);
    if (json_object_object_get_ex(root, "effect_id", &val))
        p->effect_id = json_object_get_int(val);
    if (json_object_object_get_ex(root, "brightness", &val))
        p->brightness = (uint8_t)json_object_get_int(val);
    if (json_object_object_get_ex(root, "speed", &val))
        p->speed = (uint8_t)json_object_get_int(val);
    if (json_object_object_get_ex(root, "colorful", &val))
        p->colorful = json_object_get_boolean(val) ? 1 : 0;

    if (json_object_object_get_ex(root, "color", &val) &&
        json_object_get_type(val) == json_type_array &&
        json_object_array_length(val) == 3) {
        p->color[0] = (uint8_t)json_object_get_int(json_object_array_get_idx(val, 0));
        p->color[1] = (uint8_t)json_object_get_int(json_object_array_get_idx(val, 1));
        p->color[2] = (uint8_t)json_object_get_int(json_object_array_get_idx(val, 2));
    }

    if (json_object_object_get_ex(root, "side_light", &val))
        p->side_light = (uint8_t)json_object_get_int(val);
    if (json_object_object_get_ex(root, "battery_light", &val))
        p->battery_light = (uint8_t)json_object_get_int(val);
    if (json_object_object_get_ex(root, "gain", &val))
        p->gain = json_object_get_double(val);
    if (json_object_object_get_ex(root, "sensor_profile", &val))
        strncpy(p->sensor_profile, json_object_get_string(val), sizeof(p->sensor_profile) - 1);
    if (json_object_object_get_ex(root, "sensor_config_path", &val))
        strncpy(p->sensor_config_path, json_object_get_string(val), sizeof(p->sensor_config_path) - 1);

    if (json_object_object_get_ex(root, "per_key_colors", &val) &&
        json_object_get_type(val) == json_type_array &&
        (int)json_object_array_length(val) == F87D_PROFILE_KEY_COUNT) {
        p->has_per_key = true;
        for (int i = 0; i < F87D_PROFILE_KEY_COUNT; i++) {
            json_object *rgb = json_object_array_get_idx(val, i);
            if (json_object_get_type(rgb) == json_type_array &&
                json_object_array_length(rgb) == 3) {
                p->per_key_colors[i][0] = (uint8_t)json_object_get_int(json_object_array_get_idx(rgb, 0));
                p->per_key_colors[i][1] = (uint8_t)json_object_get_int(json_object_array_get_idx(rgb, 1));
                p->per_key_colors[i][2] = (uint8_t)json_object_get_int(json_object_array_get_idx(rgb, 2));
            }
        }
    }

    json_object_put(root);
    return 0;
}

static int mkdirp(const char *path)
{
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    return mkdir(tmp, 0755) == 0 || errno == EEXIST ? 0 : -1;
}

int f87d_profile_get_config_dir(char *buf, size_t bufsz)
{
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0])
        snprintf(buf, bufsz, "%s/f87control", xdg);
    else {
        const char *home = getenv("HOME");
        if (!home) return -1;
        snprintf(buf, bufsz, "%s/.config/f87control", home);
    }
    return mkdirp(buf);
}

int f87d_profile_get_profiles_dir(char *buf, size_t bufsz)
{
    char config[512];
    if (f87d_profile_get_config_dir(config, sizeof(config)) < 0)
        return -1;
    snprintf(buf, bufsz, "%s/profiles", config);
    return mkdirp(buf);
}

static int write_file(const char *path, const char *content)
{
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fputs(content, f);
    fclose(f);
    return 0;
}

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0 || len > 1024 * 1024) { fclose(f); return NULL; }
    char *buf = malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t nr = fread(buf, 1, (size_t)len, f);
    buf[nr] = '\0';
    fclose(f);
    return buf;
}

int f87d_profile_save(const f87d_profile_t *profile, const char *name)
{
    if (f87d_profile_validate_name(name) < 0) return -1;

    char dir[512];
    if (f87d_profile_get_profiles_dir(dir, sizeof(dir)) < 0) return -1;

    char path[768];
    snprintf(path, sizeof(path), "%s/%s.json", dir, name);

    char *json = f87d_profile_to_json(profile);
    if (!json) return -1;
    int rc = write_file(path, json);
    free(json);
    return rc;
}

int f87d_profile_load(const char *name, f87d_profile_t *profile)
{
    if (f87d_profile_validate_name(name) < 0) return -1;

    char dir[512];
    if (f87d_profile_get_profiles_dir(dir, sizeof(dir)) < 0) return -1;

    char path[768];
    snprintf(path, sizeof(path), "%s/%s.json", dir, name);

    char *json = read_file(path);
    if (!json) return -1;
    int rc = f87d_profile_from_json(json, profile);
    free(json);
    return rc;
}

int f87d_profile_delete(const char *name)
{
    if (f87d_profile_validate_name(name) < 0) return -1;

    char dir[512];
    if (f87d_profile_get_profiles_dir(dir, sizeof(dir)) < 0) return -1;

    char path[768];
    snprintf(path, sizeof(path), "%s/%s.json", dir, name);

    if (unlink(path) < 0)
        return -1;
    return 0;
}

int f87d_profile_list(char ***names)
{
    char dir[512];
    if (f87d_profile_get_profiles_dir(dir, sizeof(dir)) < 0) {
        *names = NULL;
        return 0;
    }

    DIR *d = opendir(dir);
    if (!d) {
        *names = NULL;
        return 0;
    }

    int count = 0, cap = 16;
    char **list = malloc((size_t)cap * sizeof(char *));
    if (!list) { closedir(d); *names = NULL; return 0; }

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        size_t len = strlen(ent->d_name);
        if (len < 6 || strcmp(ent->d_name + len - 5, ".json") != 0)
            continue;
        if (count >= cap) {
            cap *= 2;
            char **tmp = realloc(list, (size_t)cap * sizeof(char *));
            if (!tmp) break;
            list = tmp;
        }
        list[count] = strndup(ent->d_name, len - 5);
        count++;
    }
    closedir(d);

    *names = list;
    return count;
}

void f87d_profile_free_list(char **names, int count)
{
    if (!names) return;
    for (int i = 0; i < count; i++)
        free(names[i]);
    free(names);
}

int f87d_profile_save_last(const f87d_profile_t *profile)
{
    char dir[512];
    if (f87d_profile_get_config_dir(dir, sizeof(dir)) < 0) return -1;

    char path[768];
    snprintf(path, sizeof(path), "%s/last.json", dir);

    char *json = f87d_profile_to_json(profile);
    if (!json) return -1;
    int rc = write_file(path, json);
    free(json);
    return rc;
}

int f87d_profile_load_last(f87d_profile_t *profile)
{
    char dir[512];
    if (f87d_profile_get_config_dir(dir, sizeof(dir)) < 0) return -1;

    char path[768];
    snprintf(path, sizeof(path), "%s/last.json", dir);

    char *json = read_file(path);
    if (!json) return -1;
    int rc = f87d_profile_from_json(json, profile);
    free(json);
    return rc;
}
