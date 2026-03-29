#include "preview.h"
#include "protocol.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PREVIEW_FPS 15
#define KEY_COUNT F87_KEY_COUNT

struct f87_preview {
    F87KeyboardView *keyboard;
    guint timer;
    uint64_t frame;
    int effect_id;
    char category[16];
    uint8_t speed;    /* 0-4 */
    uint8_t color[3];
    uint32_t rng;     /* PRNG state */
    uint8_t buf[KEY_COUNT][3]; /* working buffer */
};

/* Simple PRNG */
static uint32_t rng_next(uint32_t *s)
{
    *s = *s * 1103515245u + 12345u;
    return (*s >> 16) & 0x7FFF;
}

/* HSV to RGB (0-255) */
static void hsv(float h, float s, float v, uint8_t *r, uint8_t *g, uint8_t *b)
{
    float c = v * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    float rf, gf, bf;
    if (h < 60)       { rf = c; gf = x; bf = 0; }
    else if (h < 120) { rf = x; gf = c; bf = 0; }
    else if (h < 180) { rf = 0; gf = c; bf = x; }
    else if (h < 240) { rf = 0; gf = x; bf = c; }
    else if (h < 300) { rf = x; gf = 0; bf = c; }
    else               { rf = c; gf = 0; bf = x; }
    *r = (uint8_t)((rf + m) * 255);
    *g = (uint8_t)((gf + m) * 255);
    *b = (uint8_t)((bf + m) * 255);
}

static float speed_factor(uint8_t speed)
{
    return 0.3f + speed * 0.4f; /* 0.3 to 1.9 */
}

/* ===== Effect renders ===== */

static void render_off(f87_preview_t *p)
{
    memset(p->buf, 0, sizeof(p->buf));
}

static void render_static(f87_preview_t *p)
{
    for (int i = 0; i < KEY_COUNT; i++)
        memcpy(p->buf[i], p->color, 3);
}

static void render_breathing(f87_preview_t *p)
{
    float t = (float)p->frame * speed_factor(p->speed) * 0.08f;
    float br = (sinf(t) + 1.0f) * 0.5f;
    for (int i = 0; i < KEY_COUNT; i++) {
        p->buf[i][0] = (uint8_t)(p->color[0] * br);
        p->buf[i][1] = (uint8_t)(p->color[1] * br);
        p->buf[i][2] = (uint8_t)(p->color[2] * br);
    }
}

static void render_wave(f87_preview_t *p)
{
    float t = (float)p->frame * speed_factor(p->speed) * 2.0f;
    for (int i = 0; i < KEY_COUNT; i++) {
        float hue = fmodf(f87_key_layout[i].col * 20.0f + t, 360.0f);
        hsv(hue, 1.0f, 1.0f, &p->buf[i][0], &p->buf[i][1], &p->buf[i][2]);
    }
}

static void render_rain(f87_preview_t *p)
{
    /* Fade existing */
    for (int i = 0; i < KEY_COUNT; i++) {
        p->buf[i][0] = p->buf[i][0] > 20 ? p->buf[i][0] - 20 : 0;
        p->buf[i][1] = p->buf[i][1] > 20 ? p->buf[i][1] - 20 : 0;
        p->buf[i][2] = p->buf[i][2] > 20 ? p->buf[i][2] - 20 : 0;
    }
    /* Drop new rain */
    int drops = 1 + p->speed / 2;
    for (int d = 0; d < drops; d++) {
        int idx = (int)(rng_next(&p->rng) % KEY_COUNT);
        memcpy(p->buf[idx], p->color, 3);
    }
}

static void render_starlight(f87_preview_t *p)
{
    /* Fade */
    for (int i = 0; i < KEY_COUNT; i++) {
        p->buf[i][0] = p->buf[i][0] > 15 ? p->buf[i][0] - 15 : 0;
        p->buf[i][1] = p->buf[i][1] > 15 ? p->buf[i][1] - 15 : 0;
        p->buf[i][2] = p->buf[i][2] > 15 ? p->buf[i][2] - 15 : 0;
    }
    int twinkles = 2 + p->speed;
    for (int t = 0; t < twinkles; t++) {
        int idx = (int)(rng_next(&p->rng) % KEY_COUNT);
        float hue = (float)(rng_next(&p->rng) % 360);
        hsv(hue, 0.6f, 1.0f, &p->buf[idx][0], &p->buf[idx][1], &p->buf[idx][2]);
    }
}

static void render_snake(f87_preview_t *p)
{
    memset(p->buf, 0, sizeof(p->buf));
    int len = 6;
    int head = (int)((float)p->frame * speed_factor(p->speed) * 0.5f) % KEY_COUNT;
    for (int i = 0; i < len; i++) {
        int idx = (head - i + KEY_COUNT) % KEY_COUNT;
        float br = 1.0f - (float)i / (float)len;
        p->buf[idx][0] = (uint8_t)(p->color[0] * br);
        p->buf[idx][1] = (uint8_t)(p->color[1] * br);
        p->buf[idx][2] = (uint8_t)(p->color[2] * br);
    }
}

static void render_marquee(f87_preview_t *p)
{
    float t = (float)p->frame * speed_factor(p->speed) * 0.3f;
    for (int i = 0; i < KEY_COUNT; i++) {
        float col = (float)f87_key_layout[i].col;
        float wave = (sinf(col * 0.5f - t) + 1.0f) * 0.5f;
        p->buf[i][0] = (uint8_t)(p->color[0] * wave);
        p->buf[i][1] = (uint8_t)(p->color[1] * wave);
        p->buf[i][2] = (uint8_t)(p->color[2] * wave);
    }
}

static void render_circle(f87_preview_t *p)
{
    float t = (float)p->frame * speed_factor(p->speed) * 0.2f;
    float cx = 9.0f, cy = 3.0f; /* center of TKL */
    for (int i = 0; i < KEY_COUNT; i++) {
        float dx = (float)f87_key_layout[i].col - cx;
        float dy = (float)f87_key_layout[i].row - cy;
        float dist = sqrtf(dx * dx + dy * dy);
        float hue = fmodf(dist * 30.0f - t * 20.0f, 360.0f);
        if (hue < 0) hue += 360.0f;
        hsv(hue, 1.0f, 1.0f, &p->buf[i][0], &p->buf[i][1], &p->buf[i][2]);
    }
}

static void render_raindown(f87_preview_t *p)
{
    float t = (float)p->frame * speed_factor(p->speed) * 0.15f;
    for (int i = 0; i < KEY_COUNT; i++) {
        float row = (float)f87_key_layout[i].row;
        float hue = fmodf(row * 60.0f + t * 40.0f, 360.0f);
        hsv(hue, 1.0f, 1.0f, &p->buf[i][0], &p->buf[i][1], &p->buf[i][2]);
    }
}

static void render_center_ripple(f87_preview_t *p)
{
    float t = (float)p->frame * speed_factor(p->speed) * 0.2f;
    float cx = 9.0f, cy = 3.0f;
    for (int i = 0; i < KEY_COUNT; i++) {
        float dx = (float)f87_key_layout[i].col - cx;
        float dy = (float)f87_key_layout[i].row - cy;
        float dist = sqrtf(dx * dx + dy * dy);
        float wave = (sinf(dist * 1.2f - t) + 1.0f) * 0.5f;
        float hue = fmodf(dist * 25.0f - t * 15.0f, 360.0f);
        if (hue < 0) hue += 360.0f;
        uint8_t r, g, b;
        hsv(hue, 1.0f, wave, &r, &g, &b);
        p->buf[i][0] = r;
        p->buf[i][1] = g;
        p->buf[i][2] = b;
    }
}

/* SW effects */
/* Doom fire algorithm — same as the real effect in effects_sw.c */
#define FIRE_ROWS 6
#define FIRE_COLS 22

static void render_fire(f87_preview_t *p)
{
    /* Use buf as heat storage via static — preview is single-instance */
    static float heat[FIRE_ROWS][FIRE_COLS];
    static int initialized = 0;
    if (!initialized) { memset(heat, 0, sizeof(heat)); initialized = 1; }

    float decay_base = 0.15f - (float)p->speed * 0.02f;
    if (decay_base < 0.05f) decay_base = 0.05f;

    /* Bottom row = max heat */
    for (int c = 0; c < FIRE_COLS; c++)
        heat[FIRE_ROWS - 1][c] = 1.0f;

    /* Propagate upward */
    for (int r = 0; r < FIRE_ROWS - 1; r++) {
        for (int c = 0; c < FIRE_COLS; c++) {
            int src_c = c + (int)(rng_next(&p->rng) % 3) - 1;
            if (src_c < 0) src_c = 0;
            if (src_c >= FIRE_COLS) src_c = FIRE_COLS - 1;

            float decay = decay_base * (float)(rng_next(&p->rng) % 100) / 100.0f;
            heat[r][c] = heat[r + 1][src_c] - decay;
            if (heat[r][c] < 0.0f) heat[r][c] = 0.0f;
        }
    }

    /* Map heat to keys */
    for (int i = 0; i < KEY_COUNT; i++) {
        int row = f87_key_layout[i].row;
        int col = f87_key_layout[i].col;
        if (row >= FIRE_ROWS || col >= FIRE_COLS) {
            p->buf[i][0] = p->buf[i][1] = p->buf[i][2] = 0;
            continue;
        }

        float h = heat[row][col];
        /* Fire gradient: black -> color -> orange -> yellow */
        if (h < 0.33f) {
            float t = h / 0.33f;
            p->buf[i][0] = (uint8_t)(p->color[0] * t);
            p->buf[i][1] = 0;
            p->buf[i][2] = 0;
        } else if (h < 0.66f) {
            float t = (h - 0.33f) / 0.33f;
            p->buf[i][0] = p->color[0];
            p->buf[i][1] = (uint8_t)(p->color[1] * t);
            p->buf[i][2] = 0;
        } else {
            float t = (h - 0.66f) / 0.34f;
            p->buf[i][0] = p->color[0];
            p->buf[i][1] = p->color[1];
            p->buf[i][2] = (uint8_t)(255 * t);
        }
    }
}

static void render_matrix(f87_preview_t *p)
{
    /* Fade */
    for (int i = 0; i < KEY_COUNT; i++) {
        p->buf[i][1] = p->buf[i][1] > 25 ? p->buf[i][1] - 25 : 0;
        p->buf[i][0] = 0;
        p->buf[i][2] = 0;
    }
    int drops = 1 + p->speed;
    for (int d = 0; d < drops; d++) {
        int idx = (int)(rng_next(&p->rng) % KEY_COUNT);
        p->buf[idx][1] = 255;
    }
}

static void render_plasma(f87_preview_t *p)
{
    float t = (float)p->frame * speed_factor(p->speed) * 0.08f;
    for (int i = 0; i < KEY_COUNT; i++) {
        float col = (float)f87_key_layout[i].col;
        float row = (float)f87_key_layout[i].row;
        float v = sinf(col * 0.5f + t) + sinf(row * 0.7f + t * 1.3f) +
                  sinf((col + row) * 0.3f - t * 0.7f);
        float hue = fmodf((v + 3.0f) * 60.0f, 360.0f);
        hsv(hue, 0.8f, 0.9f, &p->buf[i][0], &p->buf[i][1], &p->buf[i][2]);
    }
}

static void render_spectrum_music(f87_preview_t *p)
{
    /* Simulated VU bars per column */
    float t = (float)p->frame * speed_factor(p->speed) * 0.12f;
    for (int i = 0; i < KEY_COUNT; i++) {
        float col = (float)f87_key_layout[i].col;
        float row = (float)f87_key_layout[i].row;
        float level = (sinf(col * 0.4f + t) + 1.0f) * 0.5f;
        float row_norm = 1.0f - row / 6.0f;
        if (row_norm < level) {
            float hue = col * 20.0f;
            hsv(hue, 1.0f, 1.0f, &p->buf[i][0], &p->buf[i][1], &p->buf[i][2]);
        } else {
            p->buf[i][0] = p->buf[i][1] = p->buf[i][2] = 0;
        }
    }
}

static void render_generic_color(f87_preview_t *p)
{
    /* Pulsing base color — fallback for effects without specific preview */
    float t = (float)p->frame * speed_factor(p->speed) * 0.1f;
    float br = 0.5f + 0.5f * sinf(t);
    for (int i = 0; i < KEY_COUNT; i++) {
        p->buf[i][0] = (uint8_t)(p->color[0] * br);
        p->buf[i][1] = (uint8_t)(p->color[1] * br);
        p->buf[i][2] = (uint8_t)(p->color[2] * br);
    }
}

/* ===== Dispatch ===== */

static void render_frame(f87_preview_t *p)
{
    switch (p->effect_id) {
    case 0:  render_off(p); break;
    case 1:  render_static(p); break;
    case 2:  render_breathing(p); break;
    case 3:  render_wave(p); break;
    case 4:  render_rain(p); break;          /* Spectrum keypress ~ rain */
    case 5:  render_rain(p); break;
    case 7:  render_center_ripple(p); break; /* Ripple ~ center ripple */
    case 8:  render_starlight(p); break;
    case 10: render_snake(p); break;
    case 11: render_plasma(p); break;        /* Aurora ~ plasma */
    case 12: render_starlight(p); break;     /* Reactive ~ random flash */
    case 13: render_marquee(p); break;
    case 15: render_circle(p); break;
    case 16: render_raindown(p); break;
    case 17: render_center_ripple(p); break;
    /* SW effects */
    case 100: render_fire(p); break;
    case 101: render_matrix(p); break;
    case 102: render_plasma(p); break;
    case 104: render_circle(p); break;       /* Radar ~ circle */
    case 105: render_starlight(p); break;    /* Lightning ~ random flash */
    case 110: render_rain(p); break;         /* Explode ~ rain */
    case 111: render_center_ripple(p); break;/* Ripple SW */
    case 112: render_rain(p); break;         /* Typewriter ~ rain */
    case 113: render_matrix(p); break;       /* Life ~ matrix */
    case 114: render_fire(p); break;         /* KeyHeat ~ fire */
    /* Music */
    case 200: render_spectrum_music(p); break;
    case 201: render_breathing(p); break;    /* Beat ~ breathing */
    case 202: render_breathing(p); break;    /* Energy */
    case 203: render_spectrum_music(p); break;/* VU */
    case 204: render_wave(p); break;         /* FreqMap ~ wave */
    /* Sensor */
    case 106: render_fire(p); break;         /* Sensor ~ heatmap */
    default:  render_generic_color(p); break;
    }
}

static gboolean on_preview_tick(gpointer data)
{
    f87_preview_t *p = data;
    p->frame++;
    render_frame(p);

    /* Copy buffer to keyboard view */
    for (int i = 0; i < KEY_COUNT; i++)
        f87_keyboard_view_set_key(p->keyboard, i,
                                   p->buf[i][0], p->buf[i][1], p->buf[i][2]);

    return G_SOURCE_CONTINUE;
}

/* ===== Public API ===== */

f87_preview_t *f87_preview_new(F87KeyboardView *keyboard)
{
    f87_preview_t *p = g_new0(f87_preview_t, 1);
    p->keyboard = keyboard;
    p->rng = 42;
    return p;
}

void f87_preview_destroy(f87_preview_t *prev)
{
    if (!prev) return;
    f87_preview_stop(prev);
    g_free(prev);
}

void f87_preview_start(f87_preview_t *prev, int effect_id, const char *category,
                        uint8_t speed, uint8_t r, uint8_t g, uint8_t b)
{
    f87_preview_stop(prev);

    prev->effect_id = effect_id;
    strncpy(prev->category, category ? category : "", sizeof(prev->category) - 1);
    prev->speed = speed;
    prev->color[0] = r;
    prev->color[1] = g;
    prev->color[2] = b;
    prev->frame = 0;
    memset(prev->buf, 0, sizeof(prev->buf));

    /* Don't animate custom paint mode or off */
    if (effect_id == 18 || effect_id == 0)
        return;

    prev->timer = g_timeout_add(1000 / PREVIEW_FPS, on_preview_tick, prev);
}

void f87_preview_set_speed(f87_preview_t *prev, uint8_t speed)
{
    prev->speed = speed;
}

void f87_preview_set_color(f87_preview_t *prev, uint8_t r, uint8_t g, uint8_t b)
{
    prev->color[0] = r;
    prev->color[1] = g;
    prev->color[2] = b;
}

void f87_preview_stop(f87_preview_t *prev)
{
    if (prev->timer) {
        g_source_remove(prev->timer);
        prev->timer = 0;
    }
}
