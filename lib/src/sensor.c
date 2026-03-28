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
    return -1;
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

            int64_t remaining = (int64_t)(sctx->active[i].next_read_us - now) / 1000LL;
            if (remaining < min_sleep_ms && remaining > 0)
                min_sleep_ms = (int)remaining;
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

    for (int i = 0; i < sctx->active_count; i++) {
        if (sctx->active[i].sensor->destroy && sctx->active[i].ctx)
            sctx->active[i].sensor->destroy(sctx->active[i].ctx);
    }

    pthread_mutex_destroy(&sctx->data.mutex);
}
