#ifndef F87_ANIMATE_INTERNAL_H
#define F87_ANIMATE_INTERNAL_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdatomic.h>
#include "protocol.h"
#include "ring_buffer.h"
#include "f87/animate.h"
#include "f87/audio_types.h"
#include "f87/lighting.h"

/* Frame buffer: 88 keys x RGB */
typedef struct {
    uint8_t keys[F87_KEY_COUNT][3];
} f87_frame_t;

/* Effect context passed to render functions */
typedef struct {
    f87_device *dev;
    uint8_t base_color[3];
    uint8_t brightness;        /* 1-4 */
    uint8_t speed;             /* 0-4 */
    uint64_t frame_count;
    uint64_t start_time_us;
    float gain;                /* Audio gain: 0=auto, >0=fixed multiplier */
    void *effect_data;         /* Effect-private state */
} f87_effect_ctx_t;

/* Software effect interface */
typedef struct {
    const char *name;
    f87_sw_effect_id id;
    bool needs_audio;
    bool needs_input;

    int  (*init)(f87_effect_ctx_t *ctx);
    void (*render)(f87_effect_ctx_t *ctx, f87_frame_t *frame,
                   const f87_audio_data_t *audio);
    void (*on_key)(f87_effect_ctx_t *ctx, int key_id);  /* NULL if not reactive */
    void (*destroy)(f87_effect_ctx_t *ctx);
} f87_sw_effect_t;

/* Animation context (opaque to callers) */
struct f87_anim_ctx {
    f87_device *dev;
    f87_anim_config_t config;

    /* Threads */
    pthread_t anim_thread;
    pthread_t audio_thread;
    atomic_bool running;
    atomic_int error;

    /* Current effect */
    const f87_sw_effect_t *active_effect;
    f87_effect_ctx_t effect_ctx;
    pthread_mutex_t effect_mutex;  /* Protects effect switch */

    /* Audio ring buffer (NULL if no audio) */
    f87_audio_ring_t *audio_ring;

    /* Frame timing */
    uint64_t frame_time_us;  /* Target frame time in microseconds */

    /* Input fd for reactive effects (-1 if not needed) */
    int input_fd;
};

/* Error codes */
#define F87_ERR_AUDIO    -7   /* PulseAudio connection/read error */
#define F87_ERR_ANIMATE  -8   /* Animation thread start error */

/* Effect registries */
const f87_sw_effect_t *f87_sw_find_effect(f87_sw_effect_id id);

/* Shared PRNG for effects */
uint32_t f87_effect_rand(uint32_t *seed);

/* Utility: get current time in microseconds */
uint64_t f87_time_us(void);

#endif /* F87_ANIMATE_INTERNAL_H */
