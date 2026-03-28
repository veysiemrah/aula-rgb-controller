#include "animate_internal.h"
#include <stdio.h>
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

/* ===== MATRIX EFFECT ===== */

typedef struct {
    float drops[GRID_COLS];
    float speeds[GRID_COLS];
    float trail[GRID_ROWS][GRID_COLS];
    uint32_t seed;
} matrix_data_t;

static int matrix_init(f87_effect_ctx_t *ctx)
{
    matrix_data_t *md = calloc(1, sizeof(matrix_data_t));
    if (!md) return F87_ERR_NOMEM;
    md->seed = 12345;
    for (int c = 0; c < GRID_COLS; c++) {
        md->drops[c] = -(float)(f87_effect_rand(&md->seed) % (GRID_ROWS * 3));
        md->speeds[c] = 0.1f + (float)(f87_effect_rand(&md->seed) % 50) / 100.0f;
    }
    ctx->effect_data = md;
    return F87_OK;
}

static void matrix_render(f87_effect_ctx_t *ctx, f87_frame_t *frame,
                           const f87_audio_data_t *audio)
{
    (void)audio;
    matrix_data_t *md = ctx->effect_data;
    float speed_mult = 0.5f + (float)ctx->speed * 0.3f;
    float br_scale = (float)ctx->brightness / 4.0f;

    for (int r = 0; r < GRID_ROWS; r++)
        for (int c = 0; c < GRID_COLS; c++)
            md->trail[r][c] *= 0.85f;

    for (int c = 0; c < GRID_COLS; c++) {
        md->drops[c] += md->speeds[c] * speed_mult;
        int row = (int)md->drops[c];
        if (row >= 0 && row < GRID_ROWS)
            md->trail[row][c] = 1.0f;
        if (row >= GRID_ROWS + 3) {
            md->drops[c] = -(float)(f87_effect_rand(&md->seed) % (GRID_ROWS * 2));
            md->speeds[c] = 0.1f + (float)(f87_effect_rand(&md->seed) % 50) / 100.0f;
        }
    }

    for (int k = 0; k < F87_KEY_COUNT; k++) {
        int row = f87_key_layout[k].row;
        int col = f87_key_layout[k].col;
        if (row >= GRID_ROWS || col >= GRID_COLS) continue;
        float v = md->trail[row][col];
        frame->keys[k][0] = (uint8_t)((float)ctx->base_color[0] * v * br_scale);
        frame->keys[k][1] = (uint8_t)((float)ctx->base_color[1] * v * br_scale);
        frame->keys[k][2] = (uint8_t)((float)ctx->base_color[2] * v * br_scale);
    }
}

static void matrix_destroy(f87_effect_ctx_t *ctx)
{
    free(ctx->effect_data);
    ctx->effect_data = NULL;
}

static const f87_sw_effect_t effect_matrix = {
    .name = "Matrix", .id = F87_SW_MATRIX,
    .needs_audio = false, .needs_input = false,
    .init = matrix_init, .render = matrix_render,
    .on_key = NULL, .destroy = matrix_destroy,
};

/* ===== PLASMA EFFECT ===== */

static int plasma_init(f87_effect_ctx_t *ctx)
{
    ctx->effect_data = NULL;
    return F87_OK;
}

static void plasma_render(f87_effect_ctx_t *ctx, f87_frame_t *frame,
                           const f87_audio_data_t *audio)
{
    (void)audio;
    float t = (float)ctx->frame_count * (0.02f + (float)ctx->speed * 0.02f);
    float br_scale = (float)ctx->brightness / 4.0f;

    for (int k = 0; k < F87_KEY_COUNT; k++) {
        float x = (float)f87_key_layout[k].col / 22.0f;
        float y = (float)f87_key_layout[k].row / 6.0f;

        float v1 = sinf(x * 10.0f + t);
        float v2 = sinf(y * 10.0f + t * 0.7f);
        float v3 = sinf((x + y) * 5.0f + t * 1.3f);
        float v4 = sinf(sqrtf(x * x + y * y) * 8.0f - t);

        float v = (v1 + v2 + v3 + v4 + 4.0f) / 8.0f;  /* 0 to 1 */

        float h = v * 6.0f;
        int hi = (int)h % 6;
        float f = h - (float)(int)h;
        float r, g, b;
        switch (hi) {
            case 0: r = 1; g = f; b = 0; break;
            case 1: r = 1 - f; g = 1; b = 0; break;
            case 2: r = 0; g = 1; b = f; break;
            case 3: r = 0; g = 1 - f; b = 1; break;
            case 4: r = f; g = 0; b = 1; break;
            default: r = 1; g = 0; b = 1 - f; break;
        }
        frame->keys[k][0] = (uint8_t)(r * 255.0f * br_scale);
        frame->keys[k][1] = (uint8_t)(g * 255.0f * br_scale);
        frame->keys[k][2] = (uint8_t)(b * 255.0f * br_scale);
    }
}

static void plasma_destroy(f87_effect_ctx_t *ctx) { (void)ctx; }

static const f87_sw_effect_t effect_plasma = {
    .name = "Plasma", .id = F87_SW_PLASMA,
    .needs_audio = false, .needs_input = false,
    .init = plasma_init, .render = plasma_render,
    .on_key = NULL, .destroy = plasma_destroy,
};

/* ===== HEATMAP EFFECT ===== */

typedef struct {
    float current_temp;
} heatmap_data_t;

static float read_cpu_temp(void)
{
    FILE *f = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (!f) return 0.5f;
    int millideg;
    if (fscanf(f, "%d", &millideg) != 1) millideg = 50000;
    fclose(f);
    float t = ((float)millideg / 1000.0f - 30.0f) / 60.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t;
}

static int heatmap_init(f87_effect_ctx_t *ctx)
{
    heatmap_data_t *hd = calloc(1, sizeof(heatmap_data_t));
    if (!hd) return F87_ERR_NOMEM;
    hd->current_temp = read_cpu_temp();
    ctx->effect_data = hd;
    return F87_OK;
}

static void heatmap_render(f87_effect_ctx_t *ctx, f87_frame_t *frame,
                            const f87_audio_data_t *audio)
{
    (void)audio;
    heatmap_data_t *hd = ctx->effect_data;
    if (ctx->frame_count % 30 == 0) {
        float target = read_cpu_temp();
        hd->current_temp += (target - hd->current_temp) * 0.1f;
    }
    float t = hd->current_temp;
    float br_scale = (float)ctx->brightness / 4.0f;
    float r, g, b;
    if (t < 0.33f) {
        float f = t / 0.33f;
        r = 0; g = f * 255; b = (1 - f) * 255;
    } else if (t < 0.66f) {
        float f = (t - 0.33f) / 0.33f;
        r = f * 255; g = 255; b = 0;
    } else {
        float f = (t - 0.66f) / 0.34f;
        r = 255; g = (1 - f) * 255; b = 0;
    }
    for (int k = 0; k < F87_KEY_COUNT; k++) {
        frame->keys[k][0] = (uint8_t)(r * br_scale);
        frame->keys[k][1] = (uint8_t)(g * br_scale);
        frame->keys[k][2] = (uint8_t)(b * br_scale);
    }
}

static void heatmap_destroy(f87_effect_ctx_t *ctx)
{
    free(ctx->effect_data);
    ctx->effect_data = NULL;
}

static const f87_sw_effect_t effect_heatmap = {
    .name = "Heatmap", .id = F87_SW_HEATMAP,
    .needs_audio = false, .needs_input = false,
    .init = heatmap_init, .render = heatmap_render,
    .on_key = NULL, .destroy = heatmap_destroy,
};

/* ===== RADAR EFFECT ===== */

typedef struct {
    float angle;
    float trail[F87_KEY_COUNT];
} radar_data_t;

static int radar_init(f87_effect_ctx_t *ctx)
{
    radar_data_t *rd = calloc(1, sizeof(radar_data_t));
    if (!rd) return F87_ERR_NOMEM;
    ctx->effect_data = rd;
    return F87_OK;
}

static void radar_render(f87_effect_ctx_t *ctx, f87_frame_t *frame,
                          const f87_audio_data_t *audio)
{
    (void)audio;
    radar_data_t *rd = ctx->effect_data;
    float br_scale = (float)ctx->brightness / 4.0f;
    float rot_speed = 0.05f + (float)ctx->speed * 0.03f;

    rd->angle += rot_speed;
    if (rd->angle > 2.0f * (float)M_PI) rd->angle -= 2.0f * (float)M_PI;

    for (int k = 0; k < F87_KEY_COUNT; k++)
        rd->trail[k] *= 0.92f;

    float cx = 10.0f, cy = 2.5f;
    for (int k = 0; k < F87_KEY_COUNT; k++) {
        float kx = (float)f87_key_layout[k].col - cx;
        float ky = (float)f87_key_layout[k].row - cy;
        float key_angle = atan2f(ky, kx);
        if (key_angle < 0) key_angle += 2.0f * (float)M_PI;

        float diff = fabsf(key_angle - rd->angle);
        if (diff > (float)M_PI) diff = 2.0f * (float)M_PI - diff;

        if (diff < 0.26f) {
            float intensity = 1.0f - diff / 0.26f;
            if (intensity > rd->trail[k])
                rd->trail[k] = intensity;
        }

        float v = rd->trail[k];
        frame->keys[k][0] = (uint8_t)((float)ctx->base_color[0] * v * br_scale);
        frame->keys[k][1] = (uint8_t)((float)ctx->base_color[1] * v * br_scale);
        frame->keys[k][2] = (uint8_t)((float)ctx->base_color[2] * v * br_scale);
    }
}

static void radar_destroy(f87_effect_ctx_t *ctx)
{
    free(ctx->effect_data);
    ctx->effect_data = NULL;
}

static const f87_sw_effect_t effect_radar = {
    .name = "Radar", .id = F87_SW_RADAR,
    .needs_audio = false, .needs_input = false,
    .init = radar_init, .render = radar_render,
    .on_key = NULL, .destroy = radar_destroy,
};

/* ===== LIGHTNING EFFECT ===== */

#define LIGHTNING_MAX_BOLTS 3
#define LIGHTNING_MAX_PATH 12

typedef struct {
    struct {
        int path[LIGHTNING_MAX_PATH];
        int length;
        int age;
    } bolts[LIGHTNING_MAX_BOLTS];
    float glow[F87_KEY_COUNT];
    uint32_t seed;
    int cooldown;
} lightning_data_t;

static int lightning_init(f87_effect_ctx_t *ctx)
{
    lightning_data_t *ld = calloc(1, sizeof(lightning_data_t));
    if (!ld) return F87_ERR_NOMEM;
    ld->seed = 99;
    ctx->effect_data = ld;
    return F87_OK;
}

static int find_neighbor(int key_id, uint32_t *seed)
{
    int row = f87_key_layout[key_id].row;
    int col = f87_key_layout[key_id].col;
    int dr = (int)(f87_effect_rand(seed) % 3) - 1;
    int dc = (int)(f87_effect_rand(seed) % 3) - 1;
    if (dr == 0 && dc == 0) dc = 1;
    int tr = row + dr;
    int tc = col + dc;
    int best = -1;
    int best_dist = 999;
    for (int k = 0; k < F87_KEY_COUNT; k++) {
        int d = abs(f87_key_layout[k].row - tr) + abs(f87_key_layout[k].col - tc);
        if (d < best_dist) {
            best_dist = d;
            best = k;
        }
    }
    return best;
}

static void lightning_render(f87_effect_ctx_t *ctx, f87_frame_t *frame,
                              const f87_audio_data_t *audio)
{
    (void)audio;
    lightning_data_t *ld = ctx->effect_data;
    float br_scale = (float)ctx->brightness / 4.0f;
    int spawn_rate = 10 - ctx->speed * 2;
    if (spawn_rate < 2) spawn_rate = 2;

    for (int k = 0; k < F87_KEY_COUNT; k++)
        ld->glow[k] *= 0.8f;

    for (int b = 0; b < LIGHTNING_MAX_BOLTS; b++) {
        if (ld->bolts[b].length > 0) {
            ld->bolts[b].age++;
            if (ld->bolts[b].age > 8)
                ld->bolts[b].length = 0;
        }
    }

    ld->cooldown--;
    if (ld->cooldown <= 0) {
        for (int b = 0; b < LIGHTNING_MAX_BOLTS; b++) {
            if (ld->bolts[b].length == 0) {
                int start = f87_effect_rand(&ld->seed) % F87_KEY_COUNT;
                ld->bolts[b].path[0] = start;
                ld->bolts[b].length = 1;
                ld->bolts[b].age = 0;
                int len = 4 + (int)(f87_effect_rand(&ld->seed) % 6);
                int cur = start;
                for (int i = 1; i < len && i < LIGHTNING_MAX_PATH; i++) {
                    cur = find_neighbor(cur, &ld->seed);
                    if (cur < 0) break;
                    ld->bolts[b].path[i] = cur;
                    ld->bolts[b].length++;
                }
                ld->cooldown = spawn_rate;
                break;
            }
        }
    }

    for (int b = 0; b < LIGHTNING_MAX_BOLTS; b++) {
        if (ld->bolts[b].length == 0) continue;
        float intensity = 1.0f - (float)ld->bolts[b].age / 8.0f;
        for (int i = 0; i < ld->bolts[b].length; i++) {
            int k = ld->bolts[b].path[i];
            if (intensity > ld->glow[k])
                ld->glow[k] = intensity;
        }
    }

    for (int k = 0; k < F87_KEY_COUNT; k++) {
        float v = ld->glow[k];
        float white = v > 0.5f ? (v - 0.5f) * 2.0f : 0.0f;
        float cr = white * 255.0f + (1 - white) * (float)ctx->base_color[0] * v;
        float cg = white * 255.0f + (1 - white) * (float)ctx->base_color[1] * v;
        float cb = white * 255.0f + (1 - white) * (float)ctx->base_color[2] * v;
        if (cr > 255) cr = 255;
        if (cg > 255) cg = 255;
        if (cb > 255) cb = 255;
        frame->keys[k][0] = (uint8_t)(cr * br_scale);
        frame->keys[k][1] = (uint8_t)(cg * br_scale);
        frame->keys[k][2] = (uint8_t)(cb * br_scale);
    }
}

static void lightning_destroy(f87_effect_ctx_t *ctx)
{
    free(ctx->effect_data);
    ctx->effect_data = NULL;
}

static const f87_sw_effect_t effect_lightning = {
    .name = "Lightning", .id = F87_SW_LIGHTNING,
    .needs_audio = false, .needs_input = false,
    .init = lightning_init, .render = lightning_render,
    .on_key = NULL, .destroy = lightning_destroy,
};

/* ===== EFFECT REGISTRY ===== */

static const f87_sw_effect_t *all_effects[] = {
    &effect_fire,
    &effect_matrix,
    &effect_plasma,
    &effect_heatmap,
    &effect_radar,
    &effect_lightning,
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
