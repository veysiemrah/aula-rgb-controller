#ifndef F87_SENSOR_H
#define F87_SENSOR_H

#include <stdbool.h>
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
