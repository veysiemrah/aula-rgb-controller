#ifndef F87_ANIMATE_H
#define F87_ANIMATE_H

#include <stdint.h>
#include "audio_types.h"

/* Forward declaration */
struct f87_device;

typedef struct f87_anim_ctx f87_anim_ctx_t;

/* Software effect IDs */
typedef enum {
    /* Non-reactive software effects */
    F87_SW_FIRE       = 100,
    F87_SW_MATRIX     = 101,
    F87_SW_PLASMA     = 102,
    F87_SW_HEATMAP    = 103,
    F87_SW_RADAR      = 104,
    F87_SW_LIGHTNING  = 105,
    /* Reactive software effects (need /dev/input) */
    F87_SW_EXPLODE    = 110,
    F87_SW_RIPPLE     = 111,
    F87_SW_TYPEWRITER = 112,
    F87_SW_LIFE       = 113,
    F87_SW_KEYHEAT    = 114,
    /* Music-reactive effects */
    F87_MU_SPECTRUM   = 200,
    F87_MU_BEAT       = 201,
    F87_MU_ENERGY     = 202,
    F87_MU_VU         = 203,
    F87_MU_FREQ_MAP   = 204,
} f87_sw_effect_id;

typedef struct {
    uint8_t color[3];                /* Base color RGB (default: effect-specific) */
    uint8_t brightness;              /* 1-4 (default: 3) */
    uint8_t speed;                   /* 0-4 (default: 2) */
    f87_audio_source_t audio_source; /* Monitor or mic (music effects only) */
    int fps;                         /* Target FPS (0 = auto: 30 for SW, 60 for music) */
    float gain;                      /* Audio gain: 0=auto-gain, >0=fixed multiplier */
} f87_anim_config_t;

/* Start animation — launches threads, returns context. Non-blocking. */
f87_anim_ctx_t *f87_anim_start(struct f87_device *dev, f87_sw_effect_id effect_id,
                                const f87_anim_config_t *config);

/* Stop animation — joins threads, restores previous hardware effect. */
int f87_anim_stop(f87_anim_ctx_t *ctx);

/* Change active effect while running. */
int f87_anim_set_effect(f87_anim_ctx_t *ctx, f87_sw_effect_id effect_id);

/* Change base color while running. */
int f87_anim_set_color(f87_anim_ctx_t *ctx, uint8_t r, uint8_t g, uint8_t b);

/* Check if animation is still running (false if error stopped it). */
int f87_anim_is_running(f87_anim_ctx_t *ctx);

/* Get error code if animation stopped unexpectedly. */
int f87_anim_get_error(f87_anim_ctx_t *ctx);

/* Get effect name string from sw effect ID. */
const char *f87_sw_effect_name(f87_sw_effect_id id);

#endif /* F87_ANIMATE_H */
