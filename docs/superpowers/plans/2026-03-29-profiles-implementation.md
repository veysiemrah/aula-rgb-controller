# F87 Profiles Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add profile save/load/delete/list, side/battery light control, and last-state restore to the daemon.

**Architecture:** A `profile_manager` module handles JSON serialization (json-c) and file I/O under `~/.config/f87control/`. The effect manager gains side/battery light tracking. D-Bus interface exposes 6 new methods and 2 properties. Proxy client and CLI get matching additions.

**Tech Stack:** C11, json-c, sd-bus, existing daemon infrastructure.

---

## File Structure

```
daemon/src/
├── profile_manager.h    # Profile data struct, save/load/list/delete API (NEW)
├── profile_manager.c    # JSON serialize/deserialize, file I/O (NEW)
├── effect_manager.h     # Add side_light, battery_light, colorful, gain, sensor fields
├── effect_manager.c     # Add set_side_light, set_battery_light
├── dbus_interface.c     # Add 6 new method handlers + 2 properties
├── main.c               # Load last.json on startup, save on effect change
├── CMakeLists.txt       # Add profile_manager.c, json-c dependency
lib/
├── include/f87/client.h # Add profile + side/battery client functions
├── src/client.c         # Add D-Bus proxy calls
cli/
├── src/main.c           # Add profile/sidelight/batterylight commands
tests/
├── test_profile.c       # Profile manager unit tests (NEW)
```

---

### Task 1: Profile Manager — Data Structure and JSON Serialization

**Files:**
- Create: `daemon/src/profile_manager.h`
- Create: `daemon/src/profile_manager.c`
- Create: `tests/test_profile.c`
- Modify: `daemon/CMakeLists.txt`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create profile_manager.h**

```c
/* daemon/src/profile_manager.h */
#ifndef F87D_PROFILE_MANAGER_H
#define F87D_PROFILE_MANAGER_H

#include <stdint.h>
#include <stdbool.h>

#define F87D_PROFILE_NAME_MAX 64
#define F87D_PROFILE_KEY_COUNT 88

typedef struct {
    char name[F87D_PROFILE_NAME_MAX];
    char category[16];      /* "hw", "sw", "music", "sensor", "" */
    int effect_id;
    uint8_t brightness;
    uint8_t speed;
    uint8_t colorful;
    uint8_t color[3];
    uint8_t side_light;     /* 0-4 */
    uint8_t battery_light;  /* 0-4 */
    double gain;            /* music: 0=auto, >0=fixed */
    char sensor_profile[64];
    char sensor_config_path[256];
    bool has_per_key;
    uint8_t per_key_colors[F87D_PROFILE_KEY_COUNT][3];
} f87d_profile_t;

/* Validate profile name: [a-zA-Z0-9_-]+, max 64 chars */
int f87d_profile_validate_name(const char *name);

/* Serialize profile to JSON string. Caller must free() result. */
char *f87d_profile_to_json(const f87d_profile_t *profile);

/* Deserialize JSON string to profile. Returns 0 on success. */
int f87d_profile_from_json(const char *json_str, f87d_profile_t *profile);

/* Get config directory path (~/.config/f87control). Creates if needed. */
int f87d_profile_get_config_dir(char *buf, size_t bufsz);

/* Get profiles directory path (~/.config/f87control/profiles). Creates if needed. */
int f87d_profile_get_profiles_dir(char *buf, size_t bufsz);

/* Save profile to ~/.config/f87control/profiles/<name>.json */
int f87d_profile_save(const f87d_profile_t *profile, const char *name);

/* Load profile from ~/.config/f87control/profiles/<name>.json */
int f87d_profile_load(const char *name, f87d_profile_t *profile);

/* Delete profile ~/.config/f87control/profiles/<name>.json */
int f87d_profile_delete(const char *name);

/* List profiles. Returns count, names allocated (caller frees each + array). */
int f87d_profile_list(char ***names);

/* Free profile list */
void f87d_profile_free_list(char **names, int count);

/* Save last state to ~/.config/f87control/last.json */
int f87d_profile_save_last(const f87d_profile_t *profile);

/* Load last state from ~/.config/f87control/last.json */
int f87d_profile_load_last(f87d_profile_t *profile);

#endif
```

- [ ] **Step 2: Create profile_manager.c**

```c
/* daemon/src/profile_manager.c */
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
        json_object_array_length(val) == F87D_PROFILE_KEY_COUNT) {
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

    unlink(path); /* silent success even if not found */
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

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        size_t len = strlen(ent->d_name);
        if (len < 6 || strcmp(ent->d_name + len - 5, ".json") != 0)
            continue;
        if (count >= cap) {
            cap *= 2;
            list = realloc(list, (size_t)cap * sizeof(char *));
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
```

- [ ] **Step 3: Create unit test tests/test_profile.c**

```c
/* tests/test_profile.c */
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

/* We include the source directly to test without daemon linkage */
#include "../daemon/src/profile_manager.h"

/* Forward declare — implemented in profile_manager.c */
extern char *f87d_profile_to_json(const f87d_profile_t *p);
extern int f87d_profile_from_json(const char *json_str, f87d_profile_t *p);
extern int f87d_profile_validate_name(const char *name);

static void test_validate_name(void)
{
    printf("test_validate_name... ");
    assert(f87d_profile_validate_name("gaming") == 0);
    assert(f87d_profile_validate_name("my-profile") == 0);
    assert(f87d_profile_validate_name("test_123") == 0);
    assert(f87d_profile_validate_name("") < 0);
    assert(f87d_profile_validate_name(NULL) < 0);
    assert(f87d_profile_validate_name("bad name") < 0);
    assert(f87d_profile_validate_name("bad/name") < 0);
    assert(f87d_profile_validate_name("bad.name") < 0);
    printf("PASS\n");
}

static void test_json_roundtrip(void)
{
    printf("test_json_roundtrip... ");
    f87d_profile_t p = {0};
    strncpy(p.name, "test", sizeof(p.name));
    strncpy(p.category, "hw", sizeof(p.category));
    p.effect_id = 3;
    p.brightness = 4;
    p.speed = 2;
    p.colorful = 0;
    p.color[0] = 255; p.color[1] = 0; p.color[2] = 0;
    p.side_light = 1;
    p.battery_light = 0;

    char *json = f87d_profile_to_json(&p);
    assert(json != NULL);

    f87d_profile_t p2 = {0};
    int rc = f87d_profile_from_json(json, &p2);
    assert(rc == 0);
    free(json);

    assert(strcmp(p2.name, "test") == 0);
    assert(strcmp(p2.category, "hw") == 0);
    assert(p2.effect_id == 3);
    assert(p2.brightness == 4);
    assert(p2.speed == 2);
    assert(p2.colorful == 0);
    assert(p2.color[0] == 255);
    assert(p2.color[1] == 0);
    assert(p2.side_light == 1);
    assert(p2.battery_light == 0);
    assert(p2.has_per_key == false);
    printf("PASS\n");
}

static void test_json_with_per_key(void)
{
    printf("test_json_with_per_key... ");
    f87d_profile_t p = {0};
    strncpy(p.name, "custom", sizeof(p.name));
    strncpy(p.category, "hw", sizeof(p.category));
    p.effect_id = 18;
    p.brightness = 3;
    p.has_per_key = true;
    for (int i = 0; i < F87D_PROFILE_KEY_COUNT; i++) {
        p.per_key_colors[i][0] = (uint8_t)(i * 2);
        p.per_key_colors[i][1] = (uint8_t)(i * 3);
        p.per_key_colors[i][2] = (uint8_t)(i);
    }

    char *json = f87d_profile_to_json(&p);
    assert(json != NULL);

    f87d_profile_t p2 = {0};
    assert(f87d_profile_from_json(json, &p2) == 0);
    free(json);

    assert(p2.has_per_key == true);
    assert(p2.per_key_colors[10][0] == 20);
    assert(p2.per_key_colors[10][1] == 30);
    assert(p2.per_key_colors[10][2] == 10);
    printf("PASS\n");
}

static void test_json_with_sensor(void)
{
    printf("test_json_with_sensor... ");
    f87d_profile_t p = {0};
    strncpy(p.name, "sensor-test", sizeof(p.name));
    strncpy(p.category, "sensor", sizeof(p.category));
    p.effect_id = 106;
    strncpy(p.sensor_profile, "gamer", sizeof(p.sensor_profile));

    char *json = f87d_profile_to_json(&p);
    assert(json != NULL);

    f87d_profile_t p2 = {0};
    assert(f87d_profile_from_json(json, &p2) == 0);
    free(json);

    assert(strcmp(p2.sensor_profile, "gamer") == 0);
    printf("PASS\n");
}

static void test_invalid_json(void)
{
    printf("test_invalid_json... ");
    f87d_profile_t p = {0};
    assert(f87d_profile_from_json("not json", &p) < 0);
    assert(f87d_profile_from_json("{}", &p) == 0); /* empty but valid */
    printf("PASS\n");
}

int main(void)
{
    printf("=== Profile Manager Tests ===\n\n");
    test_validate_name();
    test_json_roundtrip();
    test_json_with_per_key();
    test_json_with_sensor();
    test_invalid_json();
    printf("\nAll tests passed.\n");
    return 0;
}
```

- [ ] **Step 4: Add profile_manager.c to daemon/CMakeLists.txt and link json-c**

In `daemon/CMakeLists.txt`, add json-c dependency and profile_manager.c:

```cmake
pkg_check_modules(JSON_C REQUIRED json-c)

add_executable(f87d
    src/main.c
    src/device_manager.c
    src/effect_manager.c
    src/dbus_interface.c
    src/idle_monitor.c
    src/profile_manager.c
)

target_include_directories(f87d PRIVATE
    ${SYSTEMD_INCLUDE_DIRS}
    ${JSON_C_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/lib/include
    ${CMAKE_SOURCE_DIR}/lib/src
)

target_link_libraries(f87d PRIVATE
    f87
    ${SYSTEMD_LIBRARIES}
    ${JSON_C_LIBRARIES}
)
```

- [ ] **Step 5: Add test_profile to root CMakeLists.txt**

After the `test_daemon` block:

```cmake
if(JSON_C_FOUND)
    add_executable(test_profile tests/test_profile.c daemon/src/profile_manager.c)
    target_include_directories(test_profile PRIVATE daemon/src ${JSON_C_INCLUDE_DIRS})
    target_link_libraries(test_profile PRIVATE ${JSON_C_LIBRARIES})
    add_test(NAME profile_tests COMMAND test_profile)
endif()
```

- [ ] **Step 6: Build and run tests**

```bash
cd build && cmake .. -DBUILD_DAEMON=ON && make test_profile && ./test_profile
```
Expected: All 5 tests PASS.

- [ ] **Step 7: Commit**

```bash
git add daemon/src/profile_manager.h daemon/src/profile_manager.c tests/test_profile.c daemon/CMakeLists.txt CMakeLists.txt
git commit -m "feat(daemon): add profile manager with JSON serialize/deserialize"
```

---

### Task 2: Effect Manager — Side/Battery Light Tracking

**Files:**
- Modify: `daemon/src/effect_manager.h`
- Modify: `daemon/src/effect_manager.c`

- [ ] **Step 1: Add side/battery light fields and functions to effect_manager.h**

Add to `f87d_effect_manager_t` struct after `uint8_t color[3]`:

```c
    uint8_t colorful;
    uint8_t side_light;     /* 0-4 */
    uint8_t battery_light;  /* 0-4 */
    double gain;            /* music gain */
    char sensor_profile[64];
    char sensor_config_path[256];
```

Add function declarations:

```c
int f87d_effmgr_set_side_light(f87d_effect_manager_t *mgr, f87_device *dev,
                                uint8_t mode);
int f87d_effmgr_set_battery_light(f87d_effect_manager_t *mgr, f87_device *dev,
                                   uint8_t mode);
```

- [ ] **Step 2: Implement side/battery light in effect_manager.c**

Add functions:

```c
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
```

Also update `set_hw` to track `colorful`, `set_music` to track `gain`, and `set_sensor` to track `sensor_profile`/`sensor_config_path`.

In `f87d_effmgr_set_hw`, after setting color, add:
```c
    mgr->colorful = colorful;
```

In `f87d_effmgr_set_music`, after setting color, add:
```c
    mgr->gain = gain;
```

In `f87d_effmgr_set_sensor`, after setting category, add:
```c
    if (profile) strncpy(mgr->sensor_profile, profile, sizeof(mgr->sensor_profile) - 1);
    else mgr->sensor_profile[0] = '\0';
    if (config_path) strncpy(mgr->sensor_config_path, config_path, sizeof(mgr->sensor_config_path) - 1);
    else mgr->sensor_config_path[0] = '\0';
```

- [ ] **Step 3: Build and verify**

```bash
cd build && make f87d
```
Expected: Compiles without errors.

- [ ] **Step 4: Commit**

```bash
git add daemon/src/effect_manager.h daemon/src/effect_manager.c
git commit -m "feat(daemon): add side/battery light tracking to effect manager"
```

---

### Task 3: D-Bus Interface — Profile Methods and Side/Battery Light

**Files:**
- Modify: `daemon/src/dbus_interface.c`
- Modify: `daemon/src/dbus_interface.h`

- [ ] **Step 1: Add profile_manager include to dbus_interface.h**

```c
#include "profile_manager.h"
```

- [ ] **Step 2: Add helper to snapshot effect manager state into profile struct**

In `dbus_interface.c`, add helper:

```c
#include "profile_manager.h"

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
```

- [ ] **Step 3: Add auto-save helper — call after every effect change**

```c
static void autosave_last(f87d_dbus_ctx_t *ctx)
{
    f87d_profile_t p;
    snapshot_to_profile(ctx, &p);
    f87d_profile_save_last(&p); /* silent failure */
}
```

Add `autosave_last(ctx);` at the end of each method handler (after the `return` would be wrong — add before the final `return sd_bus_reply_method_return(...)` line): `method_set_effect`, `method_set_sw_effect`, `method_set_music_effect`, `method_set_sensor_effect`, `method_set_brightness`, `method_set_color`, `method_stop`, `method_off`.

- [ ] **Step 4: Add 6 new D-Bus method handlers**

```c
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

    /* Apply effect based on category */
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

    /* Apply side/battery light */
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
```

- [ ] **Step 5: Add side/battery light property getters**

```c
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
```

- [ ] **Step 6: Add new methods and properties to vtable**

Add before `SD_BUS_VTABLE_END`:

```c
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

    SD_BUS_PROPERTY("SideLight", "y", prop_get_side_light, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("BatteryLight", "y", prop_get_battery_light, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
```

- [ ] **Step 7: Build and verify**

```bash
cd build && make f87d
```

- [ ] **Step 8: Test with busctl**

```bash
./f87d &
# Save a profile
busctl --user call org.f87.Control /org/f87/Control org.f87.Control SaveProfile s "test1"
# List profiles
busctl --user call org.f87.Control /org/f87/Control org.f87.Control ListProfiles
# Load profile
busctl --user call org.f87.Control /org/f87/Control org.f87.Control LoadProfile s "test1"
# Delete profile
busctl --user call org.f87.Control /org/f87/Control org.f87.Control DeleteProfile s "test1"
# Check side/battery light
busctl --user get-property org.f87.Control /org/f87/Control org.f87.Control SideLight BatteryLight
kill %1
```

- [ ] **Step 9: Commit**

```bash
git add daemon/src/dbus_interface.c daemon/src/dbus_interface.h
git commit -m "feat(daemon): add profile and side/battery light D-Bus methods"
```

---

### Task 4: Last-State Restore on Startup

**Files:**
- Modify: `daemon/src/main.c`

- [ ] **Step 1: Add last.json restore after device scan**

In `main.c`, add `#include "profile_manager.h"` and after `f87d_devmgr_scan(...)`:

```c
    /* Restore last state */
    f87d_profile_t last_profile;
    if (f87d_profile_load_last(&last_profile) == 0) {
        f87_device *dev = f87d_devmgr_get_device(&g_devmgr);
        if (dev) {
            printf("f87d: restoring last state (%s, effect %d)\n",
                   last_profile.category, last_profile.effect_id);
            if (strcmp(last_profile.category, "hw") == 0 && last_profile.effect_id >= 0) {
                f87d_effmgr_set_hw(&g_effmgr, dev, last_profile.effect_id,
                                    last_profile.brightness, last_profile.speed,
                                    last_profile.colorful,
                                    last_profile.color[0], last_profile.color[1],
                                    last_profile.color[2]);
            } else if (strcmp(last_profile.category, "sw") == 0) {
                f87d_effmgr_set_sw(&g_effmgr, dev, last_profile.effect_id,
                                    last_profile.brightness, last_profile.speed,
                                    last_profile.color[0], last_profile.color[1],
                                    last_profile.color[2], 0);
            } else if (strcmp(last_profile.category, "music") == 0) {
                f87d_effmgr_set_music(&g_effmgr, dev, last_profile.effect_id,
                                       last_profile.brightness,
                                       last_profile.color[0], last_profile.color[1],
                                       last_profile.color[2], last_profile.gain);
            } else if (strcmp(last_profile.category, "sensor") == 0) {
                f87d_effmgr_set_sensor(&g_effmgr, dev,
                    last_profile.sensor_profile[0] ? last_profile.sensor_profile : NULL,
                    last_profile.sensor_config_path[0] ? last_profile.sensor_config_path : NULL);
            }
            if (last_profile.side_light > 0)
                f87d_effmgr_set_side_light(&g_effmgr, dev, last_profile.side_light);
            if (last_profile.battery_light > 0)
                f87d_effmgr_set_battery_light(&g_effmgr, dev, last_profile.battery_light);
        } else {
            printf("f87d: last state found but no device connected\n");
        }
    }
```

- [ ] **Step 2: Build and test restore**

```bash
cd build && make f87d
# Start daemon, set an effect, kill, restart — should restore
./f87d &
busctl --user call org.f87.Control /org/f87/Control org.f87.Control SetEffect iyybyyy 3 4 2 false 255 0 0
kill %1
sleep 1
./f87d &
# Check it restored
busctl --user get-property org.f87.Control /org/f87/Control org.f87.Control ActiveEffect
kill %1
```
Expected: ActiveEffect = 3 after restart.

- [ ] **Step 3: Commit**

```bash
git add daemon/src/main.c
git commit -m "feat(daemon): restore last state on startup from last.json"
```

---

### Task 5: Proxy Client — Profile and Side/Battery Light Functions

**Files:**
- Modify: `lib/include/f87/client.h`
- Modify: `lib/src/client.c`

- [ ] **Step 1: Add function declarations to client.h**

Add before the signal callback typedefs:

```c
/* Side/battery light */
int f87_client_set_side_light(f87_client *client, uint8_t mode);
int f87_client_set_battery_light(f87_client *client, uint8_t mode);

/* Profiles */
int f87_client_save_profile(f87_client *client, const char *name);
int f87_client_load_profile(f87_client *client, const char *name);
int f87_client_delete_profile(f87_client *client, const char *name);
int f87_client_list_profiles(f87_client *client, char ***names, int *count);
void f87_client_free_profile_list(char **names, int count);
```

- [ ] **Step 2: Implement in client.c**

```c
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
    if (r < 0) goto done;

    int n = 0, cap = 16;
    char **list = malloc((size_t)cap * sizeof(char *));

    const char *name_str = NULL;
    while (sd_bus_message_read(reply, "s", &name_str) > 0) {
        if (n >= cap) {
            cap *= 2;
            list = realloc(list, (size_t)cap * sizeof(char *));
        }
        list[n++] = strdup(name_str);
    }

    sd_bus_message_exit_container(reply);

    *names = list;
    *count = n;

done:
    sd_bus_message_unref(reply);
    sd_bus_error_free(&error);
    return 0;
}

void f87_client_free_profile_list(char **names, int count)
{
    if (!names) return;
    for (int i = 0; i < count; i++)
        free(names[i]);
    free(names);
}
```

- [ ] **Step 3: Build and verify**

```bash
cd build && make
```
Expected: All targets compile.

- [ ] **Step 4: Commit**

```bash
git add lib/include/f87/client.h lib/src/client.c
git commit -m "feat(lib): add profile and side/battery light client functions"
```

---

### Task 6: CLI — Profile and Side/Battery Light Commands

**Files:**
- Modify: `cli/src/main.c`

- [ ] **Step 1: Add profile/sidelight/batterylight to dispatch_client**

Add to `dispatch_client()` before the final `else`:

```c
    } else if (strcmp(cmd, "profile") == 0) {
        if (argc < 1) {
            fprintf(stderr, "Usage: f87ctl profile <save|load|delete|list> [name]\n");
            return 1;
        }
        const char *sub = argv[0];
        if (strcmp(sub, "save") == 0) {
            if (argc < 2) { fprintf(stderr, "Usage: f87ctl profile save <name>\n"); return 1; }
            if (f87_client_save_profile(client, argv[1]) < 0) {
                fprintf(stderr, "Failed to save profile.\n"); return 1;
            }
            printf("Profile '%s' saved.\n", argv[1]);
            return 0;
        } else if (strcmp(sub, "load") == 0) {
            if (argc < 2) { fprintf(stderr, "Usage: f87ctl profile load <name>\n"); return 1; }
            if (f87_client_load_profile(client, argv[1]) < 0) {
                fprintf(stderr, "Failed to load profile '%s'.\n", argv[1]); return 1;
            }
            printf("Profile '%s' loaded.\n", argv[1]);
            return 0;
        } else if (strcmp(sub, "delete") == 0) {
            if (argc < 2) { fprintf(stderr, "Usage: f87ctl profile delete <name>\n"); return 1; }
            if (f87_client_delete_profile(client, argv[1]) < 0) {
                fprintf(stderr, "Failed to delete profile.\n"); return 1;
            }
            printf("Profile '%s' deleted.\n", argv[1]);
            return 0;
        } else if (strcmp(sub, "list") == 0) {
            char **names = NULL;
            int count = 0;
            if (f87_client_list_profiles(client, &names, &count) < 0) {
                fprintf(stderr, "Failed to list profiles.\n"); return 1;
            }
            if (count == 0) {
                printf("No profiles saved.\n");
            } else {
                printf("Profiles (%d):\n", count);
                for (int i = 0; i < count; i++)
                    printf("  %s\n", names[i]);
            }
            f87_client_free_profile_list(names, count);
            return 0;
        } else {
            fprintf(stderr, "Unknown profile subcommand '%s'.\n", sub);
            return 1;
        }

    } else if (strcmp(cmd, "sidelight") == 0) {
        if (argc < 1) { fprintf(stderr, "Usage: f87ctl sidelight <0-4>\n"); return 1; }
        int mode = atoi(argv[0]);
        if (mode < 0 || mode > 4) { fprintf(stderr, "Mode must be 0-4.\n"); return 1; }
        if (f87_client_set_side_light(client, (uint8_t)mode) < 0) {
            fprintf(stderr, "Failed to set side light.\n"); return 1;
        }
        printf("Side light set to %d.\n", mode);
        return 0;

    } else if (strcmp(cmd, "batterylight") == 0) {
        if (argc < 1) { fprintf(stderr, "Usage: f87ctl batterylight <0-4>\n"); return 1; }
        int mode = atoi(argv[0]);
        if (mode < 0 || mode > 4) { fprintf(stderr, "Mode must be 0-4.\n"); return 1; }
        if (f87_client_set_battery_light(client, (uint8_t)mode) < 0) {
            fprintf(stderr, "Failed to set battery light.\n"); return 1;
        }
        printf("Battery light set to %d.\n", mode);
        return 0;
```

- [ ] **Step 2: Update usage()**

Add to usage string:

```
"  profile save <name>              Save current state as profile\n"
"  profile load <name>              Load and apply a profile\n"
"  profile delete <name>            Delete a profile\n"
"  profile list                     List saved profiles\n"
"  sidelight <0-4>                  Set side light mode\n"
"  batterylight <0-4>               Set battery light mode\n"
```

- [ ] **Step 3: Build and test**

```bash
cd build && make f87ctl
./f87d &
./f87ctl effect wave --brightness 4
./f87ctl profile save gaming
./f87ctl profile list
./f87ctl effect static ff0000
./f87ctl profile load gaming
./f87ctl profile delete gaming
./f87ctl sidelight 1
./f87ctl batterylight 0
kill %1
```

- [ ] **Step 4: Commit**

```bash
git add cli/src/main.c
git commit -m "feat(cli): add profile, sidelight, batterylight commands"
```

---

### Task 7: Full Integration Test and CLAUDE.md Update

**Files:**
- Modify: `CLAUDE.md`

- [ ] **Step 1: Run all tests**

```bash
cd build && ctest --output-on-failure
```
Expected: All tests pass (including new profile tests).

- [ ] **Step 2: End-to-end test**

```bash
./f87d &
# Set effect, save profile
./f87ctl effect wave --brightness 4 --speed 2
./f87ctl sidelight 1
./f87ctl profile save gaming
# Change effect, save another profile
./f87ctl effect static ff0000
./f87ctl profile save work
# List profiles
./f87ctl profile list
# Load gaming profile
./f87ctl profile load gaming
# Kill and restart — should restore wave effect
kill %1
sleep 1
./f87d &
./f87ctl info
# Delete profiles
./f87ctl profile delete gaming
./f87ctl profile delete work
./f87ctl profile list
kill %1
```

- [ ] **Step 3: Update CLAUDE.md**

Add to Testing section:
```
# Profile management
./f87ctl profile save gaming
./f87ctl profile load gaming
./f87ctl profile list
./f87ctl profile delete gaming

# Side/battery light
./f87ctl sidelight 1
./f87ctl batterylight 0
```

Update Project Status — change Faz 6.2 line to:
```
- Faz 6.2: Profiles (complete)
  - JSON profiles in ~/.config/f87control/profiles/
  - Last-state restore on daemon startup
  - Side/battery light control
  - CLI profile save/load/delete/list commands
```

Add to Key Files:
```
- `daemon/src/profile_manager.c` — JSON profile serialize/deserialize, file I/O
```

- [ ] **Step 4: Commit**

```bash
git add CLAUDE.md
git commit -m "docs: update CLAUDE.md for profiles (Faz 6.2)"
```
