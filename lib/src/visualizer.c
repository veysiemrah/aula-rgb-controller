#ifdef F87_HAS_AUDIO

#include "animate_internal.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ===== SPECTRUM VISUALIZER ===== */

typedef struct {
    float smooth_bands[F87_AUDIO_BANDS];
    float peak_bands[F87_AUDIO_BANDS];  /* Auto-gain per band */
} spectrum_viz_data_t;

static int spectrum_viz_init(f87_effect_ctx_t *ctx)
{
    spectrum_viz_data_t *sd = calloc(1, sizeof(spectrum_viz_data_t));
    if (!sd) return F87_ERR_NOMEM;
    ctx->effect_data = sd;
    return F87_OK;
}

static void spectrum_viz_render(f87_effect_ctx_t *ctx, f87_frame_t *frame,
                                 const f87_audio_data_t *audio)
{
    spectrum_viz_data_t *sd = ctx->effect_data;
    float br_scale = (float)ctx->brightness / 4.0f;
    float smooth = 0.3f + (float)(4 - ctx->speed) * 0.1f;

    if (audio) {
        for (int b = 0; b < F87_AUDIO_BANDS; b++) {
            sd->smooth_bands[b] += (audio->bands[b] - sd->smooth_bands[b]) * smooth;
            /* Auto-gain per band */
            if (sd->smooth_bands[b] > sd->peak_bands[b])
                sd->peak_bands[b] = sd->smooth_bands[b];
            sd->peak_bands[b] *= 0.999f;
            if (sd->peak_bands[b] < 0.01f) sd->peak_bands[b] = 0.01f;
        }
    } else {
        for (int b = 0; b < F87_AUDIO_BANDS; b++)
            sd->smooth_bands[b] *= 0.95f;
    }

    for (int k = 0; k < F87_KEY_COUNT; k++) {
        int row = f87_key_layout[k].row;
        int col = f87_key_layout[k].col;
        if (row >= F87_AUDIO_BANDS) continue;

        int band = F87_AUDIO_BANDS - 1 - row;
        /* Normalize band level against its own peak */
        float level = sd->smooth_bands[band] / sd->peak_bands[band];
        if (level > 1.0f) level = 1.0f;
        float max_col = level * 22.0f;

        if ((float)col < max_col) {
            float intensity = 1.0f - (float)col / (max_col + 1.0f);
            float r = 0, g = 0, b = 0;
            switch (band) {
                case 0: r = 1.0f; break;
                case 1: r = 1.0f; g = 0.3f; break;
                case 2: r = 0.8f; g = 0.8f; break;
                case 3: g = 1.0f; break;
                case 4: g = 0.5f; b = 1.0f; break;
                case 5: b = 1.0f; break;
            }
            frame->keys[k][0] = (uint8_t)(r * intensity * 255.0f * br_scale);
            frame->keys[k][1] = (uint8_t)(g * intensity * 255.0f * br_scale);
            frame->keys[k][2] = (uint8_t)(b * intensity * 255.0f * br_scale);
        }
    }
}

static void spectrum_viz_destroy(f87_effect_ctx_t *ctx)
{
    free(ctx->effect_data);
    ctx->effect_data = NULL;
}

static const f87_sw_effect_t viz_spectrum = {
    .name = "Spectrum", .id = F87_MU_SPECTRUM,
    .needs_audio = true, .needs_input = false,
    .init = spectrum_viz_init, .render = spectrum_viz_render,
    .on_key = NULL, .destroy = spectrum_viz_destroy,
};

/* ===== BEAT PULSE VISUALIZER ===== */

typedef struct {
    float flash;
    float hue_offset;
    float peak_energy;    /* Auto-gain: tracks recent peak */
} beat_viz_data_t;

static int beat_viz_init(f87_effect_ctx_t *ctx)
{
    beat_viz_data_t *bd = calloc(1, sizeof(beat_viz_data_t));
    if (!bd) return F87_ERR_NOMEM;
    ctx->effect_data = bd;
    return F87_OK;
}

static void beat_viz_render(f87_effect_ctx_t *ctx, f87_frame_t *frame,
                             const f87_audio_data_t *audio)
{
    beat_viz_data_t *bd = ctx->effect_data;
    float br_scale = (float)ctx->brightness / 4.0f;
    float decay = 0.82f;

    bd->flash *= decay;

    /* Auto-gain: track peak energy, normalize against it */
    if (audio) {
        if (audio->energy > bd->peak_energy)
            bd->peak_energy = audio->energy;
        /* Slowly decay peak so it adapts to volume changes */
        bd->peak_energy *= 0.999f;
        if (bd->peak_energy < 0.01f) bd->peak_energy = 0.01f;

        /* Normalize energy to 0-1 range based on recent peak */
        float normalized = audio->energy / bd->peak_energy;
        if (normalized > 1.0f) normalized = 1.0f;

        if (audio->beat) {
            bd->flash = 1.0f;
            bd->hue_offset += 0.4f;
        } else if (normalized > bd->flash) {
            bd->flash = normalized;
        }
    }

    if (bd->flash < 0.02f) return;

    /* Color: rotate hue on each beat */
    float h = bd->hue_offset;
    float r = (sinf(h) * 0.5f + 0.5f);
    float g = (sinf(h + 2.094f) * 0.5f + 0.5f);
    float b = (sinf(h + 4.189f) * 0.5f + 0.5f);

    float boosted = bd->flash * bd->flash;
    uint8_t cr = (uint8_t)(r * 255.0f * boosted * br_scale);
    uint8_t cg = (uint8_t)(g * 255.0f * boosted * br_scale);
    uint8_t cb = (uint8_t)(b * 255.0f * boosted * br_scale);

    for (int k = 0; k < F87_KEY_COUNT; k++) {
        frame->keys[k][0] = cr;
        frame->keys[k][1] = cg;
        frame->keys[k][2] = cb;
    }
}

static void beat_viz_destroy(f87_effect_ctx_t *ctx)
{
    free(ctx->effect_data);
    ctx->effect_data = NULL;
}

static const f87_sw_effect_t viz_beat = {
    .name = "Beat", .id = F87_MU_BEAT,
    .needs_audio = true, .needs_input = false,
    .init = beat_viz_init, .render = beat_viz_render,
    .on_key = NULL, .destroy = beat_viz_destroy,
};

/* ===== ENERGY WAVE VISUALIZER ===== */

typedef struct {
    float waves[8];
    float wave_str[8];
    int wave_idx;
    int cooldown;
} energy_viz_data_t;

static int energy_viz_init(f87_effect_ctx_t *ctx)
{
    energy_viz_data_t *ed = calloc(1, sizeof(energy_viz_data_t));
    if (!ed) return F87_ERR_NOMEM;
    ctx->effect_data = ed;
    return F87_OK;
}

static void energy_viz_render(f87_effect_ctx_t *ctx, f87_frame_t *frame,
                               const f87_audio_data_t *audio)
{
    energy_viz_data_t *ed = ctx->effect_data;
    float br_scale = (float)ctx->brightness / 4.0f;
    float expand_speed = 0.3f + (float)ctx->speed * 0.15f;
    float cx = 10.0f, cy = 2.5f;

    ed->cooldown--;
    if (audio && ed->cooldown <= 0 &&
        (audio->beat || audio->energy > 0.4f)) {
        ed->waves[ed->wave_idx % 8] = 0.1f;
        ed->wave_str[ed->wave_idx % 8] = audio->energy;
        ed->wave_idx++;
        ed->cooldown = 3;
    }

    for (int w = 0; w < 8; w++) {
        if (ed->waves[w] > 0) {
            ed->waves[w] += expand_speed;
            ed->wave_str[w] *= 0.93f;
            if (ed->wave_str[w] < 0.01f)
                ed->waves[w] = 0;
        }
    }

    for (int k = 0; k < F87_KEY_COUNT; k++) {
        float kx = (float)f87_key_layout[k].col - cx;
        float ky = ((float)f87_key_layout[k].row - cy) * 3.0f;
        float dist = sqrtf(kx * kx + ky * ky);

        float v = 0;
        for (int w = 0; w < 8; w++) {
            if (ed->waves[w] <= 0) continue;
            float diff = fabsf(dist - ed->waves[w]);
            if (diff < 1.5f) {
                float ring = (1.0f - diff / 1.5f) * ed->wave_str[w];
                if (ring > v) v = ring;
            }
        }

        frame->keys[k][0] = (uint8_t)((float)ctx->base_color[0] * v * br_scale);
        frame->keys[k][1] = (uint8_t)((float)ctx->base_color[1] * v * br_scale);
        frame->keys[k][2] = (uint8_t)((float)ctx->base_color[2] * v * br_scale);
    }
}

static void energy_viz_destroy(f87_effect_ctx_t *ctx)
{
    free(ctx->effect_data);
    ctx->effect_data = NULL;
}

static const f87_sw_effect_t viz_energy = {
    .name = "Energy", .id = F87_MU_ENERGY,
    .needs_audio = true, .needs_input = false,
    .init = energy_viz_init, .render = energy_viz_render,
    .on_key = NULL, .destroy = energy_viz_destroy,
};

/* ===== VU METER VISUALIZER ===== */

typedef struct {
    float smooth_level;
    float peak;
    int peak_hold;
    float peak_energy;  /* Auto-gain */
} vu_viz_data_t;

static int vu_viz_init(f87_effect_ctx_t *ctx)
{
    vu_viz_data_t *vd = calloc(1, sizeof(vu_viz_data_t));
    if (!vd) return F87_ERR_NOMEM;
    ctx->effect_data = vd;
    return F87_OK;
}

static void vu_viz_render(f87_effect_ctx_t *ctx, f87_frame_t *frame,
                           const f87_audio_data_t *audio)
{
    vu_viz_data_t *vd = ctx->effect_data;
    float br_scale = (float)ctx->brightness / 4.0f;
    float smooth = 0.3f + (float)(4 - ctx->speed) * 0.1f;

    float target = audio ? audio->energy : 0.0f;

    /* Auto-gain: normalize against recent peak */
    if (target > vd->peak_energy)
        vd->peak_energy = target;
    vd->peak_energy *= 0.999f;
    if (vd->peak_energy < 0.01f) vd->peak_energy = 0.01f;
    target = target / vd->peak_energy;
    if (target > 1.0f) target = 1.0f;

    vd->smooth_level += (target - vd->smooth_level) * smooth;

    if (vd->smooth_level > vd->peak) {
        vd->peak = vd->smooth_level;
        vd->peak_hold = 15;
    } else if (vd->peak_hold > 0) {
        vd->peak_hold--;
    } else {
        vd->peak *= 0.97f;
    }

    float level = vd->smooth_level;
    int max_col = 21;

    for (int k = 0; k < F87_KEY_COUNT; k++) {
        int col = f87_key_layout[k].col;
        float pos = (float)col / (float)max_col;

        if (pos <= level) {
            /* Color zones based on column position, not level */
            float r, g, b;
            if (pos < 0.2f) {
                r = 0; g = 255; b = 0;               /* Green */
            } else if (pos < 0.7f) {
                r = 255; g = 255; b = 0;             /* Yellow */
            } else {
                r = 255; g = 0; b = 0;               /* Red */
            }
            frame->keys[k][0] = (uint8_t)(r * br_scale);
            frame->keys[k][1] = (uint8_t)(g * br_scale);
            frame->keys[k][2] = (uint8_t)(b * br_scale);
        } else if (fabsf(pos - vd->peak) < 0.06f && vd->peak > 0.05f) {
            /* Peak indicator: red */
            frame->keys[k][0] = (uint8_t)(255 * br_scale);
            frame->keys[k][1] = 0;
            frame->keys[k][2] = 0;
        }
    }
}

static void vu_viz_destroy(f87_effect_ctx_t *ctx)
{
    free(ctx->effect_data);
    ctx->effect_data = NULL;
}

static const f87_sw_effect_t viz_vu = {
    .name = "VU", .id = F87_MU_VU,
    .needs_audio = true, .needs_input = false,
    .init = vu_viz_init, .render = vu_viz_render,
    .on_key = NULL, .destroy = vu_viz_destroy,
};

/* ===== FREQ MAP VISUALIZER ===== */

typedef struct {
    float smooth[F87_AUDIO_BANDS];
} freqmap_viz_data_t;

static int freqmap_viz_init(f87_effect_ctx_t *ctx)
{
    freqmap_viz_data_t *fd = calloc(1, sizeof(freqmap_viz_data_t));
    if (!fd) return F87_ERR_NOMEM;
    ctx->effect_data = fd;
    return F87_OK;
}

static void freqmap_viz_render(f87_effect_ctx_t *ctx, f87_frame_t *frame,
                                const f87_audio_data_t *audio)
{
    freqmap_viz_data_t *fd = ctx->effect_data;
    float br_scale = (float)ctx->brightness / 4.0f;
    float smooth = 0.3f + (float)(4 - ctx->speed) * 0.1f;

    if (audio) {
        for (int b = 0; b < F87_AUDIO_BANDS; b++)
            fd->smooth[b] += (audio->bands[b] - fd->smooth[b]) * smooth;
    } else {
        for (int b = 0; b < F87_AUDIO_BANDS; b++)
            fd->smooth[b] *= 0.95f;
    }

    for (int k = 0; k < F87_KEY_COUNT; k++) {
        int row = f87_key_layout[k].row;
        if (row >= F87_AUDIO_BANDS) continue;

        int band = F87_AUDIO_BANDS - 1 - row;
        float v = fd->smooth[band];

        frame->keys[k][0] = (uint8_t)((float)ctx->base_color[0] * v * br_scale);
        frame->keys[k][1] = (uint8_t)((float)ctx->base_color[1] * v * br_scale);
        frame->keys[k][2] = (uint8_t)((float)ctx->base_color[2] * v * br_scale);
    }
}

static void freqmap_viz_destroy(f87_effect_ctx_t *ctx)
{
    free(ctx->effect_data);
    ctx->effect_data = NULL;
}

static const f87_sw_effect_t viz_freqmap = {
    .name = "FreqMap", .id = F87_MU_FREQ_MAP,
    .needs_audio = true, .needs_input = false,
    .init = freqmap_viz_init, .render = freqmap_viz_render,
    .on_key = NULL, .destroy = freqmap_viz_destroy,
};

/* ===== VISUALIZER REGISTRY ===== */

static const f87_sw_effect_t *viz_effects[] = {
    &viz_spectrum,
    &viz_beat,
    &viz_energy,
    &viz_vu,
    &viz_freqmap,
    NULL
};

const f87_sw_effect_t *f87_viz_find_effect(f87_sw_effect_id id)
{
    for (int i = 0; viz_effects[i] != NULL; i++) {
        if (viz_effects[i]->id == id)
            return viz_effects[i];
    }
    return NULL;
}

#endif /* F87_HAS_AUDIO */
