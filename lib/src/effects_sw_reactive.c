#include "animate_internal.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define GRID_ROWS 6
#define GRID_COLS 22

/* ===== EXPLODE EFFECT ===== */

#define EXPLODE_MAX 8

typedef struct {
    struct {
        int key_id;
        float radius;
        float strength;
        float hue;
    } explosions[EXPLODE_MAX];
    int exp_idx;
    uint32_t seed;
} explode_data_t;

static int explode_init(f87_effect_ctx_t *ctx)
{
    explode_data_t *ed = calloc(1, sizeof(explode_data_t));
    if (!ed) return F87_ERR_NOMEM;
    ed->seed = 777;
    ctx->effect_data = ed;
    return F87_OK;
}

static void explode_on_key(f87_effect_ctx_t *ctx, int key_id)
{
    explode_data_t *ed = ctx->effect_data;
    int idx = ed->exp_idx % EXPLODE_MAX;
    ed->explosions[idx].key_id = key_id;
    ed->explosions[idx].radius = 0.1f;
    ed->explosions[idx].strength = 1.0f;
    ed->explosions[idx].hue = (float)(f87_effect_rand(&ed->seed) % 360) / 360.0f;
    ed->exp_idx++;
}

static void explode_render(f87_effect_ctx_t *ctx, f87_frame_t *frame,
                            const f87_audio_data_t *audio)
{
    (void)audio;
    explode_data_t *ed = ctx->effect_data;
    float br_scale = (float)ctx->brightness / 4.0f;
    float expand = 0.3f;
    /* speed controls max spread radius: 0=2 keys, 1=3, 2=5, 3=7, 4=10 */
    float max_radius = 2.0f + (float)ctx->speed * 2.0f;

    for (int e = 0; e < EXPLODE_MAX; e++) {
        if (ed->explosions[e].strength <= 0) continue;
        ed->explosions[e].radius += expand;
        /* Stop expanding and fade faster when max radius reached */
        if (ed->explosions[e].radius > max_radius)
            ed->explosions[e].strength *= 0.8f;
        else
            ed->explosions[e].strength *= 0.92f;
        if (ed->explosions[e].strength < 0.01f)
            ed->explosions[e].strength = 0;
    }

    for (int k = 0; k < F87_KEY_COUNT; k++) {
        float max_v = 0;
        float best_hue = 0;

        for (int e = 0; e < EXPLODE_MAX; e++) {
            if (ed->explosions[e].strength <= 0) continue;
            int src = ed->explosions[e].key_id;
            float dx = (float)(f87_key_layout[k].col - f87_key_layout[src].col);
            float dy = (float)(f87_key_layout[k].row - f87_key_layout[src].row) * 2.0f;
            float dist = sqrtf(dx * dx + dy * dy);
            /* Skip keys beyond max radius */
            if (dist > max_radius) continue;
            float ring_dist = fabsf(dist - ed->explosions[e].radius);
            if (ring_dist < 1.5f) {
                float v = (1.0f - ring_dist / 1.5f) * ed->explosions[e].strength;
                if (v > max_v) {
                    max_v = v;
                    best_hue = ed->explosions[e].hue;
                }
            }
        }

        if (max_v > 0) {
            float h = best_hue * 6.0f;
            int hi = (int)h % 6;
            float f = h - (float)(int)h;
            float r = 0, g = 0, b = 0;
            switch (hi) {
                case 0: r = 1; g = f; b = 0; break;
                case 1: r = 1 - f; g = 1; b = 0; break;
                case 2: r = 0; g = 1; b = f; break;
                case 3: r = 0; g = 1 - f; b = 1; break;
                case 4: r = f; g = 0; b = 1; break;
                default: r = 1; g = 0; b = 1 - f; break;
            }
            frame->keys[k][0] = (uint8_t)(r * max_v * 255.0f * br_scale);
            frame->keys[k][1] = (uint8_t)(g * max_v * 255.0f * br_scale);
            frame->keys[k][2] = (uint8_t)(b * max_v * 255.0f * br_scale);
        }
    }
}

static void explode_destroy(f87_effect_ctx_t *ctx)
{
    free(ctx->effect_data);
    ctx->effect_data = NULL;
}

static const f87_sw_effect_t effect_explode = {
    .name = "Explode", .id = F87_SW_EXPLODE,
    .needs_audio = false, .needs_input = true,
    .init = explode_init, .render = explode_render,
    .on_key = explode_on_key, .destroy = explode_destroy,
};

/* ===== RIPPLE EFFECT ===== */

#define RIPPLE_MAX 12

typedef struct {
    struct { int key_id; float radius; float strength; } waves[RIPPLE_MAX];
    int wave_idx;
} ripple_data_t;

static int ripple_init(f87_effect_ctx_t *ctx)
{
    ripple_data_t *rd = calloc(1, sizeof(ripple_data_t));
    if (!rd) return F87_ERR_NOMEM;
    ctx->effect_data = rd;
    return F87_OK;
}

static void ripple_on_key(f87_effect_ctx_t *ctx, int key_id)
{
    ripple_data_t *rd = ctx->effect_data;
    int idx = rd->wave_idx % RIPPLE_MAX;
    rd->waves[idx].key_id = key_id;
    rd->waves[idx].radius = 0.1f;
    rd->waves[idx].strength = 1.0f;
    rd->wave_idx++;
}

static void ripple_render(f87_effect_ctx_t *ctx, f87_frame_t *frame,
                           const f87_audio_data_t *audio)
{
    (void)audio;
    ripple_data_t *rd = ctx->effect_data;
    float br_scale = (float)ctx->brightness / 4.0f;
    float expand = 0.3f + (float)ctx->speed * 0.15f;

    for (int w = 0; w < RIPPLE_MAX; w++) {
        if (rd->waves[w].strength <= 0) continue;
        rd->waves[w].radius += expand;
        rd->waves[w].strength *= 0.92f;
        if (rd->waves[w].strength < 0.01f)
            rd->waves[w].strength = 0;
    }

    for (int k = 0; k < F87_KEY_COUNT; k++) {
        float total = 0;
        for (int w = 0; w < RIPPLE_MAX; w++) {
            if (rd->waves[w].strength <= 0) continue;
            int src = rd->waves[w].key_id;
            float dx = (float)(f87_key_layout[k].col - f87_key_layout[src].col);
            float dy = (float)(f87_key_layout[k].row - f87_key_layout[src].row) * 2.0f;
            float dist = sqrtf(dx * dx + dy * dy);
            /* Multiple concentric rings — smooth cosine ripple */
            if (dist < rd->waves[w].radius + 2.0f) {
                float phase = dist * 1.5f - rd->waves[w].radius * 1.5f;
                float wave = cosf(phase) * 0.5f + 0.5f;
                float falloff = 1.0f - dist / (rd->waves[w].radius + 3.0f);
                if (falloff < 0) falloff = 0;
                total += wave * falloff * rd->waves[w].strength;
            }
        }
        if (total > 1.0f) total = 1.0f;
        frame->keys[k][0] = (uint8_t)((float)ctx->base_color[0] * total * br_scale);
        frame->keys[k][1] = (uint8_t)((float)ctx->base_color[1] * total * br_scale);
        frame->keys[k][2] = (uint8_t)((float)ctx->base_color[2] * total * br_scale);
    }
}

static void ripple_destroy(f87_effect_ctx_t *ctx)
{
    free(ctx->effect_data);
    ctx->effect_data = NULL;
}

static const f87_sw_effect_t effect_ripple = {
    .name = "Ripple SW", .id = F87_SW_RIPPLE,
    .needs_audio = false, .needs_input = true,
    .init = ripple_init, .render = ripple_render,
    .on_key = ripple_on_key, .destroy = ripple_destroy,
};

/* ===== TYPEWRITER EFFECT ===== */

typedef struct {
    float heat[F87_KEY_COUNT];
} typewriter_data_t;

static int typewriter_init(f87_effect_ctx_t *ctx)
{
    typewriter_data_t *td = calloc(1, sizeof(typewriter_data_t));
    if (!td) return F87_ERR_NOMEM;
    ctx->effect_data = td;
    return F87_OK;
}

static void typewriter_on_key(f87_effect_ctx_t *ctx, int key_id)
{
    typewriter_data_t *td = ctx->effect_data;
    td->heat[key_id] = 1.0f;
}

static void typewriter_render(f87_effect_ctx_t *ctx, f87_frame_t *frame,
                               const f87_audio_data_t *audio)
{
    (void)audio;
    typewriter_data_t *td = ctx->effect_data;
    float br_scale = (float)ctx->brightness / 4.0f;
    float decay = 0.96f - (float)ctx->speed * 0.01f;

    for (int k = 0; k < F87_KEY_COUNT; k++) {
        td->heat[k] *= decay;
        float h = td->heat[k];
        if (h < 0.01f) continue;

        float r, g, b;
        if (h > 0.7f) {
            r = 255; g = 255; b = (h - 0.7f) / 0.3f * 200.0f;
        } else if (h > 0.3f) {
            r = 255; g = (h - 0.3f) / 0.4f * 200.0f; b = 0;
        } else {
            r = h / 0.3f * 255.0f; g = 0; b = 0;
        }
        frame->keys[k][0] = (uint8_t)(r * br_scale);
        frame->keys[k][1] = (uint8_t)(g * br_scale);
        frame->keys[k][2] = (uint8_t)(b * br_scale);
    }
}

static void typewriter_destroy(f87_effect_ctx_t *ctx)
{
    free(ctx->effect_data);
    ctx->effect_data = NULL;
}

static const f87_sw_effect_t effect_typewriter = {
    .name = "Typewriter", .id = F87_SW_TYPEWRITER,
    .needs_audio = false, .needs_input = true,
    .init = typewriter_init, .render = typewriter_render,
    .on_key = typewriter_on_key, .destroy = typewriter_destroy,
};

/* ===== GAME OF LIFE EFFECT ===== */

typedef struct {
    uint8_t grid[GRID_ROWS][GRID_COLS];
    uint8_t next[GRID_ROWS][GRID_COLS];
    float brightness_map[GRID_ROWS][GRID_COLS];
    int step_counter;
} life_data_t;

static int life_init(f87_effect_ctx_t *ctx)
{
    life_data_t *ld = calloc(1, sizeof(life_data_t));
    if (!ld) return F87_ERR_NOMEM;
    ctx->effect_data = ld;
    return F87_OK;
}

static void life_on_key(f87_effect_ctx_t *ctx, int key_id)
{
    life_data_t *ld = ctx->effect_data;
    int row = f87_key_layout[key_id].row;
    int col = f87_key_layout[key_id].col;
    if (row >= GRID_ROWS || col >= GRID_COLS) return;

    /* Seed a small cluster around the pressed key for survival */
    for (int dr = -1; dr <= 1; dr++) {
        for (int dc = -1; dc <= 1; dc++) {
            int r = row + dr, c = col + dc;
            if (r >= 0 && r < GRID_ROWS && c >= 0 && c < GRID_COLS) {
                ld->grid[r][c] = 1;
                ld->brightness_map[r][c] = 1.0f;
            }
        }
    }
}

static int count_neighbors(life_data_t *ld, int r, int c)
{
    int count = 0;
    for (int dr = -1; dr <= 1; dr++) {
        for (int dc = -1; dc <= 1; dc++) {
            if (dr == 0 && dc == 0) continue;
            int nr = r + dr, nc = c + dc;
            if (nr >= 0 && nr < GRID_ROWS && nc >= 0 && nc < GRID_COLS)
                count += ld->grid[nr][nc];
        }
    }
    return count;
}

static void life_render(f87_effect_ctx_t *ctx, f87_frame_t *frame,
                         const f87_audio_data_t *audio)
{
    (void)audio;
    life_data_t *ld = ctx->effect_data;
    float br_scale = (float)ctx->brightness / 4.0f;
    int step_interval = 15 - ctx->speed * 3;
    if (step_interval < 3) step_interval = 3;

    ld->step_counter++;
    if (ld->step_counter >= step_interval) {
        ld->step_counter = 0;
        for (int r = 0; r < GRID_ROWS; r++) {
            for (int c = 0; c < GRID_COLS; c++) {
                int n = count_neighbors(ld, r, c);
                if (ld->grid[r][c])
                    ld->next[r][c] = (n == 2 || n == 3) ? 1 : 0;
                else
                    ld->next[r][c] = (n == 3) ? 1 : 0;
            }
        }
        memcpy(ld->grid, ld->next, sizeof(ld->grid));
    }

    for (int r = 0; r < GRID_ROWS; r++) {
        for (int c = 0; c < GRID_COLS; c++) {
            float target = ld->grid[r][c] ? 1.0f : 0.0f;
            ld->brightness_map[r][c] += (target - ld->brightness_map[r][c]) * 0.3f;
        }
    }

    for (int k = 0; k < F87_KEY_COUNT; k++) {
        int row = f87_key_layout[k].row;
        int col = f87_key_layout[k].col;
        if (row >= GRID_ROWS || col >= GRID_COLS) continue;
        float v = ld->brightness_map[row][col];
        frame->keys[k][0] = (uint8_t)((float)ctx->base_color[0] * v * br_scale);
        frame->keys[k][1] = (uint8_t)((float)ctx->base_color[1] * v * br_scale);
        frame->keys[k][2] = (uint8_t)((float)ctx->base_color[2] * v * br_scale);
    }
}

static void life_destroy(f87_effect_ctx_t *ctx)
{
    free(ctx->effect_data);
    ctx->effect_data = NULL;
}

static const f87_sw_effect_t effect_life = {
    .name = "Life", .id = F87_SW_LIFE,
    .needs_audio = false, .needs_input = true,
    .init = life_init, .render = life_render,
    .on_key = life_on_key, .destroy = life_destroy,
};

/* ===== REACTIVE REGISTRY ===== */

static const f87_sw_effect_t *reactive_effects[] = {
    &effect_explode,
    &effect_ripple,
    &effect_typewriter,
    &effect_life,
    NULL
};

const f87_sw_effect_t *f87_reactive_find_effect(f87_sw_effect_id id)
{
    for (int i = 0; reactive_effects[i] != NULL; i++) {
        if (reactive_effects[i]->id == id)
            return reactive_effects[i];
    }
    return NULL;
}
