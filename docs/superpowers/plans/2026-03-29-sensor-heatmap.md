# Sensor Heatmap — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Map keyboard keys to system sensors (CPU temp, CPU load, GPU temp, RAM) with configurable JSON profiles and color/bar display modes.

**Architecture:** Plugin-based sensor interface with built-in implementations. Sensor thread reads values at configurable intervals into a shared array. Existing animation thread renders key colors based on JSON config mappings. Sensor effect registered as `F87_SW_SENSOR` in the effect system.

**Tech Stack:** C11, pthreads, json-c (already linked in CLI), sysfs/procfs for sensor data

---

## File Structure

```
lib/
  src/
    sensor.h           — f87_sensor_t interface, sensor registry, sensor thread API
    sensor.c           — built-in sensors (cpu_temp, cpu_load, gpu_temp, ram_usage),
                         sensor thread, dynamic path discovery
    sensor_config.h    — config types: f87_sensor_mapping_t, f87_sensor_profile_t
    sensor_config.c    — JSON config parsing (json-c)
    effects_sw.c       — add sensor effect to registry (modify existing)
configs/sensor/
    developer.json     — CPU temp/load + RAM
    gamer.json         — GPU temp/load + CPU temp
    system.json        — all sensors spread across keyboard
tests/
    test_sensor.c      — sensor init/read/destroy + config parse tests
```

---

### Task 1: Sensor Plugin Interface and Built-in Sensors

**Files:**
- Create: `lib/src/sensor.h`
- Create: `lib/src/sensor.c`
- Create: `tests/test_sensor.c`
- Modify: `lib/CMakeLists.txt`
- Modify: `CMakeLists.txt` (add test)

- [ ] **Step 1: Write sensor tests**

```c
/* tests/test_sensor.c */
#include <stdio.h>
#include <string.h>
#include "../lib/src/sensor.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    printf("  %-40s ", #name); \
    tests_run++; \
    if (test_##name()) { tests_passed++; printf("PASS\n"); } \
    else { printf("FAIL\n"); } \
} while(0)

static int test_registry_find(void) {
    const f87_sensor_t *s = f87_sensor_find("cpu_temp");
    return s != NULL && strcmp(s->name, "cpu_temp") == 0;
}

static int test_registry_find_all(void) {
    return f87_sensor_find("cpu_temp") != NULL &&
           f87_sensor_find("cpu_load") != NULL &&
           f87_sensor_find("ram_usage") != NULL;
}

static int test_registry_unknown(void) {
    return f87_sensor_find("nonexistent") == NULL;
}

static int test_cpu_temp_read(void) {
    const f87_sensor_t *s = f87_sensor_find("cpu_temp");
    if (!s) return 1; /* Skip if not found — still pass */
    void *ctx = NULL;
    if (s->init(&ctx) < 0) return 1; /* Sensor not available on this system */
    float val = s->read(ctx);
    s->destroy(ctx);
    /* Temperature should be between 0 and 150 C */
    return val >= 0.0f && val <= 150.0f;
}

static int test_cpu_load_read(void) {
    const f87_sensor_t *s = f87_sensor_find("cpu_load");
    if (!s) return 0;
    void *ctx = NULL;
    if (s->init(&ctx) < 0) return 0;
    /* First read may return 0 (needs delta), read twice */
    s->read(ctx);
    usleep(200000); /* 200ms for delta */
    float val = s->read(ctx);
    s->destroy(ctx);
    return val >= 0.0f && val <= 100.0f;
}

static int test_ram_read(void) {
    const f87_sensor_t *s = f87_sensor_find("ram_usage");
    if (!s) return 0;
    void *ctx = NULL;
    if (s->init(&ctx) < 0) return 0;
    float val = s->read(ctx);
    s->destroy(ctx);
    return val >= 0.0f && val <= 100.0f;
}

static int test_normalize(void) {
    float val = f87_sensor_normalize(65.0f, 30.0f, 100.0f);
    /* (65-30)/(100-30) = 0.5 */
    return val > 0.49f && val < 0.51f;
}

static int test_normalize_clamp(void) {
    float below = f87_sensor_normalize(10.0f, 30.0f, 100.0f);
    float above = f87_sensor_normalize(120.0f, 30.0f, 100.0f);
    return below == 0.0f && above == 1.0f;
}

int main(void) {
    printf("Sensor tests:\n");
    TEST(registry_find);
    TEST(registry_find_all);
    TEST(registry_unknown);
    TEST(cpu_temp_read);
    TEST(cpu_load_read);
    TEST(ram_read);
    TEST(normalize);
    TEST(normalize_clamp);
    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
```

- [ ] **Step 2: Create sensor.h**

```c
/* lib/src/sensor.h */
#ifndef F87_SENSOR_H
#define F87_SENSOR_H

#include <stdint.h>
#include <pthread.h>
#include <stdatomic.h>

#define F87_SENSOR_MAX 16

/* Sensor plugin interface */
typedef struct {
    const char *name;           /* "cpu_temp", "cpu_load", etc. */
    const char *description;    /* "CPU Temperature (C)" */
    float min_value;            /* Expected min (e.g. 30.0) */
    float max_value;            /* Expected max (e.g. 100.0) */
    int default_interval_ms;

    int   (*init)(void **ctx);
    float (*read)(void *ctx);
    void  (*destroy)(void *ctx);
} f87_sensor_t;

/* Sensor data shared between sensor thread and render */
typedef struct {
    float values[F87_SENSOR_MAX];       /* Normalized 0.0-1.0 */
    int   error[F87_SENSOR_MAX];        /* 0=ok, 1=read error */
    pthread_mutex_t mutex;
} f87_sensor_data_t;

/* Sensor thread context */
typedef struct {
    pthread_t thread;
    atomic_bool running;
    f87_sensor_data_t data;

    /* Active sensors */
    struct {
        const f87_sensor_t *sensor;
        void *ctx;
        int interval_ms;
        uint64_t next_read_us;
    } active[F87_SENSOR_MAX];
    int active_count;
} f87_sensor_ctx_t;

/* Registry */
const f87_sensor_t *f87_sensor_find(const char *name);
int f87_sensor_count(void);
const f87_sensor_t *f87_sensor_get(int index);

/* Normalization helper */
float f87_sensor_normalize(float value, float min_val, float max_val);

/* Thread management */
int  f87_sensor_thread_start(f87_sensor_ctx_t *sctx);
void f87_sensor_thread_stop(f87_sensor_ctx_t *sctx);

#endif /* F87_SENSOR_H */
```

- [ ] **Step 3: Implement sensor.c**

```c
/* lib/src/sensor.c */
#include "sensor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>

/* ===== NORMALIZATION ===== */

float f87_sensor_normalize(float value, float min_val, float max_val)
{
    if (max_val <= min_val) return 0.0f;
    float v = (value - min_val) / (max_val - min_val);
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

/* ===== CPU TEMPERATURE ===== */

typedef struct {
    char path[256];
} cpu_temp_ctx_t;

static int cpu_temp_init(void **ctx)
{
    /* Scan thermal zones for CPU */
    char base[256], type[64];
    for (int i = 0; i < 20; i++) {
        snprintf(base, sizeof(base), "/sys/class/thermal/thermal_zone%d/type", i);
        FILE *f = fopen(base, "r");
        if (!f) continue;
        type[0] = '\0';
        if (fgets(type, sizeof(type), f)) {
            char *nl = strchr(type, '\n');
            if (nl) *nl = '\0';
        }
        fclose(f);

        if (strstr(type, "x86_pkg") || strstr(type, "cpu") ||
            strstr(type, "coretemp") || strstr(type, "k10temp") ||
            strstr(type, "zenpower")) {
            cpu_temp_ctx_t *c = calloc(1, sizeof(cpu_temp_ctx_t));
            if (!c) return -1;
            snprintf(c->path, sizeof(c->path),
                     "/sys/class/thermal/thermal_zone%d/temp", i);
            *ctx = c;
            return 0;
        }
    }
    /* Fallback: use zone 0 */
    cpu_temp_ctx_t *c = calloc(1, sizeof(cpu_temp_ctx_t));
    if (!c) return -1;
    snprintf(c->path, sizeof(c->path), "/sys/class/thermal/thermal_zone0/temp");
    FILE *test = fopen(c->path, "r");
    if (!test) { free(c); return -1; }
    fclose(test);
    *ctx = c;
    return 0;
}

static float cpu_temp_read(void *ctx)
{
    cpu_temp_ctx_t *c = ctx;
    FILE *f = fopen(c->path, "r");
    if (!f) return -1.0f;
    int millideg;
    if (fscanf(f, "%d", &millideg) != 1) { fclose(f); return -1.0f; }
    fclose(f);
    return (float)millideg / 1000.0f;
}

static void cpu_temp_destroy(void *ctx) { free(ctx); }

static const f87_sensor_t sensor_cpu_temp = {
    .name = "cpu_temp",
    .description = "CPU Temperature (C)",
    .min_value = 30.0f,
    .max_value = 100.0f,
    .default_interval_ms = 1000,
    .init = cpu_temp_init,
    .read = cpu_temp_read,
    .destroy = cpu_temp_destroy,
};

/* ===== CPU LOAD ===== */

typedef struct {
    long prev_idle, prev_total;
} cpu_load_ctx_t;

static int cpu_load_init(void **ctx)
{
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return -1;
    fclose(f);
    cpu_load_ctx_t *c = calloc(1, sizeof(cpu_load_ctx_t));
    if (!c) return -1;
    *ctx = c;
    return 0;
}

static float cpu_load_read(void *ctx)
{
    cpu_load_ctx_t *c = ctx;
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return -1.0f;
    long user, nice, sys, idle, iowait, irq, softirq, steal;
    if (fscanf(f, "cpu %ld %ld %ld %ld %ld %ld %ld %ld",
               &user, &nice, &sys, &idle, &iowait, &irq, &softirq, &steal) != 8) {
        fclose(f);
        return -1.0f;
    }
    fclose(f);

    long total = user + nice + sys + idle + iowait + irq + softirq + steal;
    long idle_all = idle + iowait;

    float load = 0.0f;
    if (c->prev_total > 0) {
        long d_total = total - c->prev_total;
        long d_idle = idle_all - c->prev_idle;
        if (d_total > 0)
            load = (1.0f - (float)d_idle / (float)d_total) * 100.0f;
    }
    c->prev_idle = idle_all;
    c->prev_total = total;
    return load;
}

static void cpu_load_destroy(void *ctx) { free(ctx); }

static const f87_sensor_t sensor_cpu_load = {
    .name = "cpu_load",
    .description = "CPU Load (%)",
    .min_value = 0.0f,
    .max_value = 100.0f,
    .default_interval_ms = 500,
    .init = cpu_load_init,
    .read = cpu_load_read,
    .destroy = cpu_load_destroy,
};

/* ===== GPU TEMPERATURE ===== */

typedef struct {
    char path[256];
} gpu_temp_ctx_t;

static int gpu_temp_init(void **ctx)
{
    /* Scan hwmon for GPU temp — AMD/Intel via DRM, NVIDIA via hwmon */
    char path[256], name[64];
    DIR *dir = opendir("/sys/class/hwmon");
    if (!dir) return -1;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        snprintf(path, sizeof(path), "/sys/class/hwmon/%s/name", ent->d_name);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        name[0] = '\0';
        if (fgets(name, sizeof(name), f)) {
            char *nl = strchr(name, '\n');
            if (nl) *nl = '\0';
        }
        fclose(f);

        if (strstr(name, "amdgpu") || strstr(name, "nvidia") ||
            strstr(name, "nouveau") || strstr(name, "i915")) {
            gpu_temp_ctx_t *c = calloc(1, sizeof(gpu_temp_ctx_t));
            if (!c) { closedir(dir); return -1; }
            snprintf(c->path, sizeof(c->path),
                     "/sys/class/hwmon/%s/temp1_input", ent->d_name);
            FILE *test = fopen(c->path, "r");
            if (!test) { free(c); continue; }
            fclose(test);
            *ctx = c;
            closedir(dir);
            return 0;
        }
    }
    closedir(dir);
    return -1; /* No GPU sensor found */
}

static float gpu_temp_read(void *ctx)
{
    gpu_temp_ctx_t *c = ctx;
    FILE *f = fopen(c->path, "r");
    if (!f) return -1.0f;
    int millideg;
    if (fscanf(f, "%d", &millideg) != 1) { fclose(f); return -1.0f; }
    fclose(f);
    return (float)millideg / 1000.0f;
}

static void gpu_temp_destroy(void *ctx) { free(ctx); }

static const f87_sensor_t sensor_gpu_temp = {
    .name = "gpu_temp",
    .description = "GPU Temperature (C)",
    .min_value = 30.0f,
    .max_value = 100.0f,
    .default_interval_ms = 1000,
    .init = gpu_temp_init,
    .read = gpu_temp_read,
    .destroy = gpu_temp_destroy,
};

/* ===== RAM USAGE ===== */

static int ram_init(void **ctx)
{
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return -1;
    fclose(f);
    *ctx = NULL;
    return 0;
}

static float ram_read(void *ctx)
{
    (void)ctx;
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return -1.0f;
    long total = 0, available = 0;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "MemTotal: %ld", &total) == 1) continue;
        if (sscanf(line, "MemAvailable: %ld", &available) == 1) break;
    }
    fclose(f);
    if (total <= 0) return -1.0f;
    return (1.0f - (float)available / (float)total) * 100.0f;
}

static void ram_destroy(void *ctx) { (void)ctx; }

static const f87_sensor_t sensor_ram = {
    .name = "ram_usage",
    .description = "RAM Usage (%)",
    .min_value = 0.0f,
    .max_value = 100.0f,
    .default_interval_ms = 1000,
    .init = ram_init,
    .read = ram_read,
    .destroy = ram_destroy,
};

/* ===== SENSOR REGISTRY ===== */

static const f87_sensor_t *all_sensors[] = {
    &sensor_cpu_temp,
    &sensor_cpu_load,
    &sensor_gpu_temp,
    &sensor_ram,
    NULL
};

const f87_sensor_t *f87_sensor_find(const char *name)
{
    for (int i = 0; all_sensors[i]; i++) {
        if (strcmp(all_sensors[i]->name, name) == 0)
            return all_sensors[i];
    }
    return NULL;
}

int f87_sensor_count(void)
{
    int n = 0;
    while (all_sensors[n]) n++;
    return n;
}

const f87_sensor_t *f87_sensor_get(int index)
{
    if (index < 0 || index >= f87_sensor_count()) return NULL;
    return all_sensors[index];
}

/* ===== SENSOR THREAD ===== */

static uint64_t sensor_time_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

static void *sensor_thread_func(void *arg)
{
    f87_sensor_ctx_t *sctx = arg;

    while (atomic_load(&sctx->running)) {
        uint64_t now = sensor_time_us();
        int min_sleep_ms = 100;

        for (int i = 0; i < sctx->active_count; i++) {
            if (now >= sctx->active[i].next_read_us) {
                float raw = sctx->active[i].sensor->read(sctx->active[i].ctx);

                pthread_mutex_lock(&sctx->data.mutex);
                if (raw < 0.0f) {
                    sctx->data.error[i] = 1;
                } else {
                    sctx->data.values[i] = f87_sensor_normalize(
                        raw,
                        sctx->active[i].sensor->min_value,
                        sctx->active[i].sensor->max_value
                    );
                    sctx->data.error[i] = 0;
                }
                pthread_mutex_unlock(&sctx->data.mutex);

                sctx->active[i].next_read_us = now +
                    (uint64_t)sctx->active[i].interval_ms * 1000ULL;
            }

            /* Find shortest remaining sleep */
            int remaining_ms = (int)((sctx->active[i].next_read_us - now) / 1000ULL);
            if (remaining_ms < min_sleep_ms)
                min_sleep_ms = remaining_ms;
        }

        if (min_sleep_ms > 0)
            usleep((useconds_t)(min_sleep_ms * 1000));
    }

    return NULL;
}

int f87_sensor_thread_start(f87_sensor_ctx_t *sctx)
{
    atomic_store(&sctx->running, true);
    pthread_mutex_init(&sctx->data.mutex, NULL);

    int rc = pthread_create(&sctx->thread, NULL, sensor_thread_func, sctx);
    if (rc != 0) {
        atomic_store(&sctx->running, false);
        return -1;
    }
    return 0;
}

void f87_sensor_thread_stop(f87_sensor_ctx_t *sctx)
{
    atomic_store(&sctx->running, false);
    pthread_join(sctx->thread, NULL);

    /* Destroy sensor contexts */
    for (int i = 0; i < sctx->active_count; i++) {
        if (sctx->active[i].sensor->destroy && sctx->active[i].ctx)
            sctx->active[i].sensor->destroy(sctx->active[i].ctx);
    }

    pthread_mutex_destroy(&sctx->data.mutex);
}
```

- [ ] **Step 4: Add test to CMakeLists.txt**

In root `CMakeLists.txt`, add after the ring_buffer test:

```cmake
add_executable(test_sensor tests/test_sensor.c lib/src/sensor.c)
target_include_directories(test_sensor PRIVATE lib/src lib/include)
target_link_libraries(test_sensor Threads::Threads)
add_test(NAME sensor_tests COMMAND test_sensor)
```

- [ ] **Step 5: Add sensor.c to lib/CMakeLists.txt**

Add `src/sensor.c` to the `add_library(f87 SHARED ...)` source list.

- [ ] **Step 6: Build and run tests**

```bash
cd build && cmake .. -DBUILD_GUI=OFF && make test_sensor && ./test_sensor
```

Expected: All tests pass.

- [ ] **Step 7: Commit**

```bash
git add lib/src/sensor.h lib/src/sensor.c tests/test_sensor.c CMakeLists.txt lib/CMakeLists.txt
git commit -m "feat: add sensor plugin interface with built-in CPU/GPU/RAM sensors"
```

---

### Task 2: Config Parser

**Files:**
- Create: `lib/src/sensor_config.h`
- Create: `lib/src/sensor_config.c`
- Modify: `lib/CMakeLists.txt`

- [ ] **Step 1: Create sensor_config.h**

```c
/* lib/src/sensor_config.h */
#ifndef F87_SENSOR_CONFIG_H
#define F87_SENSOR_CONFIG_H

#include "sensor.h"
#include "f87/lighting.h"

#define F87_SENSOR_MAP_MAX  16
#define F87_SENSOR_KEYS_MAX 20

typedef enum {
    F87_SENSOR_MODE_COLOR = 0,  /* Single key — color gradient */
    F87_SENSOR_MODE_BAR   = 1,  /* Key array — bar fill */
} f87_sensor_mode_t;

typedef struct {
    const char *sensor_name;        /* Reference to sensor registry name */
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

/* Parse a JSON config file into a profile.
 * Returns 0 on success, <0 on error. */
int f87_sensor_config_load(const char *path, f87_sensor_profile_t *profile,
                            const f87_key_info *layout, int key_count);

/* Load a built-in profile by name ("developer", "gamer", "system").
 * Returns 0 on success, <0 if not found. */
int f87_sensor_config_builtin(const char *name, f87_sensor_profile_t *profile,
                               const f87_key_info *layout, int key_count);

#endif /* F87_SENSOR_CONFIG_H */
```

- [ ] **Step 2: Implement sensor_config.c**

```c
/* lib/src/sensor_config.c */
#include "sensor_config.h"
#include <json-c/json.h>
#include <stdio.h>
#include <string.h>

/* Find key_id by name from layout table */
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

        /* Sensor name */
        struct json_object *jsensor;
        if (!json_object_object_get_ex(jmap, "sensor", &jsensor)) continue;
        m->sensor_name = json_object_get_string(jsensor);

        /* Verify sensor exists */
        if (!f87_sensor_find(m->sensor_name)) {
            fprintf(stderr, "f87: unknown sensor '%s', skipping\n", m->sensor_name);
            continue;
        }

        /* Mode */
        struct json_object *jmode;
        if (json_object_object_get_ex(jmap, "mode", &jmode)) {
            const char *mode_str = json_object_get_string(jmode);
            if (strcmp(mode_str, "bar") == 0)
                m->mode = F87_SENSOR_MODE_BAR;
            else
                m->mode = F87_SENSOR_MODE_COLOR;
        }

        /* Keys */
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

        /* Interval */
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

/* Built-in profiles defined as JSON strings */

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
```

- [ ] **Step 3: Add sensor_config.c to lib/CMakeLists.txt**

Add `src/sensor_config.c` to the library sources. Also add json-c dependency to lib:

```cmake
pkg_check_modules(JSON_C json-c)
if(JSON_C_FOUND)
    target_sources(f87 PRIVATE src/sensor_config.c)
    target_include_directories(f87 PRIVATE ${JSON_C_INCLUDE_DIRS})
    target_link_libraries(f87 PRIVATE ${JSON_C_LIBRARIES})
    target_compile_definitions(f87 PRIVATE F87_HAS_JSON=1)
endif()
```

- [ ] **Step 4: Build and verify**

```bash
cd build && cmake .. -DBUILD_GUI=OFF && make
```

Expected: Clean build.

- [ ] **Step 5: Commit**

```bash
git add lib/src/sensor_config.h lib/src/sensor_config.c lib/CMakeLists.txt
git commit -m "feat: add JSON config parser for sensor-key mappings"
```

---

### Task 3: Sensor Effect and CLI Integration

**Files:**
- Modify: `lib/include/f87/animate.h` (add F87_SW_SENSOR ID)
- Modify: `lib/src/effects_sw.c` (add sensor effect)
- Modify: `lib/src/animate.c` (sensor thread lifecycle)
- Modify: `lib/src/animate_internal.h` (sensor context in anim_ctx)
- Modify: `cli/src/main.c` (sensor command)

- [ ] **Step 1: Add F87_SW_SENSOR to effect IDs**

In `lib/include/f87/animate.h`, add after `F87_SW_HEATMAP`:

```c
    F87_SW_SENSOR     = 106,
```

Also add to `f87_anim_config_t`:

```c
    const char *sensor_profile;      /* Sensor profile name (NULL = "developer") */
    const char *sensor_config_path;  /* Custom config path (NULL = use profile) */
```

- [ ] **Step 2: Implement sensor effect in effects_sw.c**

Add after the lightning effect, before the registry:

```c
/* ===== SENSOR EFFECT ===== */
#ifdef F87_HAS_JSON

#include "sensor.h"
#include "sensor_config.h"

typedef struct {
    f87_sensor_profile_t profile;
    f87_sensor_ctx_t sensor_ctx;
    int blink_counter;  /* For error blink animation */
} sensor_effect_data_t;

/* Color gradient for sensor values: blue->green->yellow->orange->red */
static void sensor_value_to_color(float v, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (v < 0.2f) {
        float t = v / 0.2f;
        *r = 0; *g = 0; *b = (uint8_t)(t * 200.0f);
    } else if (v < 0.4f) {
        float t = (v - 0.2f) / 0.2f;
        *r = 0; *g = (uint8_t)((0.4f + t * 0.6f) * 255.0f); *b = (uint8_t)((1 - t) * 200.0f);
    } else if (v < 0.6f) {
        float t = (v - 0.4f) / 0.2f;
        *r = (uint8_t)(t * 255.0f); *g = 255; *b = 0;
    } else if (v < 0.8f) {
        float t = (v - 0.6f) / 0.2f;
        *r = 255; *g = (uint8_t)((1 - t * 0.6f) * 255.0f); *b = 0;
    } else {
        float t = (v - 0.8f) / 0.2f;
        *r = 255; *g = (uint8_t)((0.4f - t * 0.4f) * 255.0f); *b = 0;
    }
}

static int sensor_effect_init(f87_effect_ctx_t *ctx)
{
    sensor_effect_data_t *sd = calloc(1, sizeof(sensor_effect_data_t));
    if (!sd) return F87_ERR_NOMEM;

    /* Load profile — check anim config for profile/path.
     * Since effect_data is the only way to pass extra config,
     * we access it through the device's key layout. */
    const f87_key_info *layout = f87_key_layout;
    int key_count = F87_KEY_COUNT;

    /* Default to "developer" profile */
    int rc = f87_sensor_config_builtin("developer", &sd->profile, layout, key_count);
    if (rc < 0) {
        free(sd);
        return -1;
    }

    /* Initialize active sensors from profile */
    for (int i = 0; i < sd->profile.mapping_count; i++) {
        f87_sensor_mapping_t *m = &sd->profile.mappings[i];
        const f87_sensor_t *s = f87_sensor_find(m->sensor_name);
        if (!s) continue;

        int idx = sd->sensor_ctx.active_count;
        sd->sensor_ctx.active[idx].sensor = s;
        sd->sensor_ctx.active[idx].interval_ms = m->interval_ms;
        sd->sensor_ctx.active[idx].next_read_us = 0;
        m->sensor_index = idx;

        if (s->init(&sd->sensor_ctx.active[idx].ctx) < 0) {
            fprintf(stderr, "f87: sensor '%s' init failed, skipping\n", s->name);
            m->sensor_index = -1;
            continue;
        }
        sd->sensor_ctx.active_count++;
    }

    /* Start sensor thread */
    if (sd->sensor_ctx.active_count > 0)
        f87_sensor_thread_start(&sd->sensor_ctx);

    ctx->effect_data = sd;
    return F87_OK;
}

static void sensor_effect_render(f87_effect_ctx_t *ctx, f87_frame_t *frame,
                                  const f87_audio_data_t *audio)
{
    (void)audio;
    sensor_effect_data_t *sd = ctx->effect_data;
    float br_scale = (float)ctx->brightness / 4.0f;

    sd->blink_counter++;
    int blink_on = (sd->blink_counter / 15) % 2;  /* ~500ms at 30fps */

    pthread_mutex_lock(&sd->sensor_ctx.data.mutex);

    for (int i = 0; i < sd->profile.mapping_count; i++) {
        f87_sensor_mapping_t *m = &sd->profile.mappings[i];
        if (m->sensor_index < 0) continue;

        float value = sd->sensor_ctx.data.values[m->sensor_index];
        int error = sd->sensor_ctx.data.error[m->sensor_index];

        if (m->mode == F87_SENSOR_MODE_COLOR) {
            /* Single key — color based on value */
            uint8_t r, g, b;
            sensor_value_to_color(value, &r, &g, &b);

            for (int k = 0; k < m->key_count; k++) {
                int kid = m->key_ids[k];
                if (error && !blink_on) {
                    frame->keys[kid][0] = 0;
                    frame->keys[kid][1] = 0;
                    frame->keys[kid][2] = 0;
                } else {
                    frame->keys[kid][0] = (uint8_t)((float)r * br_scale);
                    frame->keys[kid][1] = (uint8_t)((float)g * br_scale);
                    frame->keys[kid][2] = (uint8_t)((float)b * br_scale);
                }
            }
        } else {
            /* Bar mode — fill keys left to right */
            for (int k = 0; k < m->key_count; k++) {
                int kid = m->key_ids[k];
                float key_pos = (float)k / (float)m->key_count;
                float key_end = (float)(k + 1) / (float)m->key_count;

                if (error && !blink_on) {
                    frame->keys[kid][0] = 0;
                    frame->keys[kid][1] = 0;
                    frame->keys[kid][2] = 0;
                    continue;
                }

                if (value >= key_end) {
                    /* Fully lit — color based on position */
                    uint8_t r, g, b;
                    sensor_value_to_color(key_end, &r, &g, &b);
                    frame->keys[kid][0] = (uint8_t)((float)r * br_scale);
                    frame->keys[kid][1] = (uint8_t)((float)g * br_scale);
                    frame->keys[kid][2] = (uint8_t)((float)b * br_scale);
                } else if (value > key_pos) {
                    /* Partially lit — dimmed based on fill fraction */
                    float fill = (value - key_pos) / (key_end - key_pos);
                    uint8_t r, g, b;
                    sensor_value_to_color(value, &r, &g, &b);
                    frame->keys[kid][0] = (uint8_t)((float)r * fill * br_scale);
                    frame->keys[kid][1] = (uint8_t)((float)g * fill * br_scale);
                    frame->keys[kid][2] = (uint8_t)((float)b * fill * br_scale);
                }
                /* else: key stays dark (below threshold) */
            }
        }
    }

    pthread_mutex_unlock(&sd->sensor_ctx.data.mutex);
}

static void sensor_effect_destroy(f87_effect_ctx_t *ctx)
{
    sensor_effect_data_t *sd = ctx->effect_data;
    if (!sd) return;

    if (sd->sensor_ctx.active_count > 0)
        f87_sensor_thread_stop(&sd->sensor_ctx);

    free(sd);
    ctx->effect_data = NULL;
}

static const f87_sw_effect_t effect_sensor = {
    .name = "Sensor",
    .id = F87_SW_SENSOR,
    .needs_audio = false,
    .needs_input = false,
    .init = sensor_effect_init,
    .render = sensor_effect_render,
    .on_key = NULL,
    .destroy = sensor_effect_destroy,
};

#endif /* F87_HAS_JSON */
```

- [ ] **Step 3: Add sensor effect to registry**

In `effects_sw.c`, update the `all_effects[]` array:

```c
static const f87_sw_effect_t *all_effects[] = {
    &effect_fire,
    &effect_matrix,
    &effect_plasma,
    &effect_heatmap,
    &effect_radar,
    &effect_lightning,
#ifdef F87_HAS_JSON
    &effect_sensor,
#endif
    NULL
};
```

- [ ] **Step 4: Add CLI sensor command**

In `cli/src/main.c`, add "sensor" to `parse_sw_effect()`:

```c
        {"sensor",     F87_SW_SENSOR},
```

Add `--profile` and `--config` parsing to `cmd_animate()`:

```c
        } else if (strcmp(argv[i], "--profile") == 0 && i + 1 < argc) {
            config.sensor_profile = argv[++i];
        } else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config.sensor_config_path = argv[++i];
        }
```

- [ ] **Step 5: Handle profile/config in sensor effect init**

Update `sensor_effect_init()` to check `ctx` for profile/config overrides. Since the effect context doesn't directly carry anim config, pass it through `effect_data` from `f87_anim_start()`. In `animate.c`, before calling `effect->init()`, store config pointers in a way the sensor effect can access them.

Simplest approach: store sensor profile/config in the effect context's `base_color` or add a `void *config_extra` field to `f87_effect_ctx_t`:

In `animate_internal.h`, add to `f87_effect_ctx_t`:

```c
    const char *sensor_profile;
    const char *sensor_config_path;
```

In `animate.c` `f87_anim_start()`, after setting `ctx->effect_ctx` fields:

```c
    ctx->effect_ctx.sensor_profile = ctx->config.sensor_profile;
    ctx->effect_ctx.sensor_config_path = ctx->config.sensor_config_path;
```

Update `sensor_effect_init()` to use these:

```c
    /* Load profile from config */
    int rc;
    if (ctx->sensor_config_path) {
        rc = f87_sensor_config_load(ctx->sensor_config_path, &sd->profile, layout, key_count);
    } else {
        const char *profile = ctx->sensor_profile ? ctx->sensor_profile : "developer";
        rc = f87_sensor_config_builtin(profile, &sd->profile, layout, key_count);
    }
```

- [ ] **Step 6: Build and test**

```bash
cd build && cmake .. -DBUILD_GUI=OFF && make && ctest --output-on-failure
```

- [ ] **Step 7: Hardware test**

```bash
./cli/f87ctl animate sensor
./cli/f87ctl animate sensor --profile gamer
./cli/f87ctl animate sensor --profile system
```

- [ ] **Step 8: Commit**

```bash
git add lib/include/f87/animate.h lib/src/animate_internal.h lib/src/animate.c lib/src/effects_sw.c cli/src/main.c
git commit -m "feat: add sensor heatmap effect with profile support"
```

---

### Task 4: JSON Config Files and CLAUDE.md Update

**Files:**
- Create: `configs/sensor/developer.json`
- Create: `configs/sensor/gamer.json`
- Create: `configs/sensor/system.json`
- Modify: `CLAUDE.md`

- [ ] **Step 1: Create config files**

```json
// configs/sensor/developer.json
{
    "profile": "developer",
    "mappings": [
        { "sensor": "cpu_temp", "keys": ["F1","F2","F3","F4"], "mode": "bar", "interval_ms": 1000 },
        { "sensor": "cpu_load", "keys": ["F5","F6","F7","F8"], "mode": "bar", "interval_ms": 500 },
        { "sensor": "ram_usage", "keys": ["F9","F10","F11","F12"], "mode": "bar", "interval_ms": 1000 }
    ]
}
```

```json
// configs/sensor/gamer.json
{
    "profile": "gamer",
    "mappings": [
        { "sensor": "gpu_temp", "keys": ["F1","F2","F3","F4"], "mode": "bar", "interval_ms": 1000 },
        { "sensor": "cpu_temp", "keys": ["F5","F6","F7","F8"], "mode": "bar", "interval_ms": 1000 },
        { "sensor": "cpu_load", "keys": ["F9","F10","F11","F12"], "mode": "bar", "interval_ms": 500 }
    ]
}
```

```json
// configs/sensor/system.json
{
    "profile": "system",
    "mappings": [
        { "sensor": "cpu_temp", "keys": ["ESC"], "mode": "color", "interval_ms": 1000 },
        { "sensor": "cpu_load", "keys": ["F1","F2","F3","F4","F5"], "mode": "bar", "interval_ms": 500 },
        { "sensor": "gpu_temp", "keys": ["F6"], "mode": "color", "interval_ms": 1000 },
        { "sensor": "ram_usage", "keys": ["F9","F10","F11","F12"], "mode": "bar", "interval_ms": 1000 }
    ]
}
```

- [ ] **Step 2: Update CLAUDE.md**

Add sensor info to Key Files, Testing, and Project Status sections.

- [ ] **Step 3: Final test with custom config**

```bash
./cli/f87ctl animate sensor --config configs/sensor/system.json
```

- [ ] **Step 4: Commit**

```bash
git add configs/sensor/ CLAUDE.md
git commit -m "docs: add sensor config profiles, update CLAUDE.md for Faz 4"
```
