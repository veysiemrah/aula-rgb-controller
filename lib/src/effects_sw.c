#include "animate_internal.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define GRID_ROWS 6
#define GRID_COLS 22

/* ===== FIRE EFFECT ===== */
/* Doom fire algorithm adapted for 6-row keyboard layout */

typedef struct {
    float heat[GRID_ROWS][GRID_COLS];
    uint32_t seed;
} fire_data_t;

static int fire_init(f87_effect_ctx_t *ctx)
{
    fire_data_t *fd = calloc(1, sizeof(fire_data_t));
    if (!fd) return F87_ERR_NOMEM;
    fd->seed = 42;
    ctx->effect_data = fd;
    return F87_OK;
}

static void fire_render(f87_effect_ctx_t *ctx, f87_frame_t *frame,
                         const f87_audio_data_t *audio)
{
    (void)audio;
    fire_data_t *fd = ctx->effect_data;

    float decay_base = 0.15f - (float)ctx->speed * 0.02f;
    if (decay_base < 0.05f) decay_base = 0.05f;

    /* Set bottom row to max heat */
    for (int c = 0; c < GRID_COLS; c++)
        fd->heat[GRID_ROWS - 1][c] = 1.0f;

    /* Propagate heat upward with decay and random spread */
    for (int r = 0; r < GRID_ROWS - 1; r++) {
        for (int c = 0; c < GRID_COLS; c++) {
            int src_c = c + (int)(f87_effect_rand(&fd->seed) % 3) - 1;
            if (src_c < 0) src_c = 0;
            if (src_c >= GRID_COLS) src_c = GRID_COLS - 1;

            float decay = decay_base * (float)(f87_effect_rand(&fd->seed) % 100) / 100.0f;
            fd->heat[r][c] = fd->heat[r + 1][src_c] - decay;
            if (fd->heat[r][c] < 0.0f) fd->heat[r][c] = 0.0f;
        }
    }

    float br_scale = (float)ctx->brightness / 4.0f;

    for (int k = 0; k < F87_KEY_COUNT; k++) {
        int row = f87_key_layout[k].row;
        int col = f87_key_layout[k].col;
        if (row >= GRID_ROWS || col >= GRID_COLS) continue;

        float h = fd->heat[row][col];
        float r, g, b;

        /* Fire gradient: black -> red -> orange -> yellow -> white */
        if (h < 0.33f) {
            float t = h / 0.33f;
            r = t * (float)ctx->base_color[0];
            g = 0; b = 0;
        } else if (h < 0.66f) {
            float t = (h - 0.33f) / 0.33f;
            r = (float)ctx->base_color[0];
            g = t * (float)ctx->base_color[1];
            b = 0;
        } else {
            float t = (h - 0.66f) / 0.34f;
            r = (float)ctx->base_color[0];
            g = (float)ctx->base_color[1];
            b = t * 255.0f;
        }

        frame->keys[k][0] = (uint8_t)(r * br_scale);
        frame->keys[k][1] = (uint8_t)(g * br_scale);
        frame->keys[k][2] = (uint8_t)(b * br_scale);
    }
}

static void fire_destroy(f87_effect_ctx_t *ctx)
{
    free(ctx->effect_data);
    ctx->effect_data = NULL;
}

static const f87_sw_effect_t effect_fire = {
    .name = "Fire",
    .id = F87_SW_FIRE,
    .needs_audio = false,
    .needs_input = false,
    .init = fire_init,
    .render = fire_render,
    .on_key = NULL,
    .destroy = fire_destroy,
};

/* ===== EFFECT REGISTRY ===== */

static const f87_sw_effect_t *all_effects[] = {
    &effect_fire,
    NULL
};

/* Forward declarations for other registries */
#ifdef F87_HAS_AUDIO
extern const f87_sw_effect_t *f87_viz_find_effect(f87_sw_effect_id id);
#endif
extern const f87_sw_effect_t *f87_reactive_find_effect(f87_sw_effect_id id);

const f87_sw_effect_t *f87_sw_find_effect(f87_sw_effect_id id)
{
    for (int i = 0; all_effects[i] != NULL; i++) {
        if (all_effects[i]->id == id)
            return all_effects[i];
    }

#ifdef F87_HAS_AUDIO
    const f87_sw_effect_t *viz = f87_viz_find_effect(id);
    if (viz) return viz;
#endif

    return f87_reactive_find_effect(id);
}

const char *f87_sw_effect_name(f87_sw_effect_id id)
{
    const f87_sw_effect_t *e = f87_sw_find_effect(id);
    return e ? e->name : "Unknown";
}
