#include "preview.h"
#include "protocol.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define PREVIEW_FPS 15
#define KEY_COUNT F87_KEY_COUNT
#define GRID_ROWS 6
#define GRID_COLS 22

struct f87_preview {
    F87KeyboardView *keyboard;
    guint timer;
    uint64_t frame;
    int effect_id;
    char category[16];
    uint8_t speed;
    uint8_t color[3];
    int colorful;
    uint32_t rng;
    uint8_t buf[KEY_COUNT][3];
    void *state; /* effect-specific state */
};

static uint32_t rng_next(uint32_t *s)
{
    *s = *s * 1103515245u + 12345u;
    return (*s >> 16) & 0x7FFF;
}

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

static float speed_mult(uint8_t speed)
{
    return 0.5f + (float)speed * 0.3f;
}

/* ===== Effect state structs ===== */

typedef struct {
    float heat[GRID_ROWS][GRID_COLS];
} fire_state_t;

typedef struct {
    float drops[GRID_COLS];
    float speeds[GRID_COLS];
    float trail[GRID_ROWS][GRID_COLS];
} matrix_state_t;

typedef struct {
    float angle;
    float trail[KEY_COUNT];
} radar_state_t;

typedef struct {
    struct { int path[12]; int length; int age; } bolts[3];
    float glow[KEY_COUNT];
    int cooldown;
} lightning_state_t;

#define REACTIVE_MAX 8
typedef struct {
    struct { int key_id; float radius; float strength; float hue; } items[REACTIVE_MAX];
    int idx;
} reactive_state_t;

typedef struct {
    float heat[KEY_COUNT];
} typewriter_state_t;

typedef struct {
    uint8_t grid[GRID_ROWS][GRID_COLS];
    uint8_t next[GRID_ROWS][GRID_COLS];
    float bright[GRID_ROWS][GRID_COLS];
    int step;
} life_state_t;

typedef struct {
    float heat[KEY_COUNT];
    float max_heat;
} keyheat_state_t;

/* ===== Renders ===== */

static void render_off(f87_preview_t *p)
{
    (void)p;
    memset(p->buf, 0, sizeof(p->buf));
}

static void render_static(f87_preview_t *p)
{
    for (int i = 0; i < KEY_COUNT; i++) memcpy(p->buf[i], p->color, 3);
}

static void render_breathing(f87_preview_t *p)
{
    float t = (float)p->frame * speed_mult(p->speed) * 0.08f;
    float br = (sinf(t) + 1.0f) * 0.5f;
    for (int i = 0; i < KEY_COUNT; i++) {
        p->buf[i][0] = (uint8_t)(p->color[0] * br);
        p->buf[i][1] = (uint8_t)(p->color[1] * br);
        p->buf[i][2] = (uint8_t)(p->color[2] * br);
    }
}

static void render_wave(f87_preview_t *p)
{
    /* Hardware Wave: all keys cycle through rainbow together */
    float hue = fmodf((float)p->frame * speed_mult(p->speed) * 3.0f, 360.0f);
    uint8_t r, g, b;
    hsv(hue, 1.0f, 1.0f, &r, &g, &b);
    for (int i = 0; i < KEY_COUNT; i++) {
        p->buf[i][0] = r;
        p->buf[i][1] = g;
        p->buf[i][2] = b;
    }
}

static void render_starlight(f87_preview_t *p)
{
    for (int i = 0; i < KEY_COUNT; i++) {
        p->buf[i][0] = p->buf[i][0] > 15 ? p->buf[i][0] - 15 : 0;
        p->buf[i][1] = p->buf[i][1] > 15 ? p->buf[i][1] - 15 : 0;
        p->buf[i][2] = p->buf[i][2] > 15 ? p->buf[i][2] - 15 : 0;
    }
    int n = 2 + p->speed;
    for (int t = 0; t < n; t++) {
        int idx = (int)(rng_next(&p->rng) % KEY_COUNT);
        memcpy(p->buf[idx], p->color, 3);
    }
}

static void render_rain(f87_preview_t *p)
{
    for (int i = 0; i < KEY_COUNT; i++) {
        p->buf[i][0] = p->buf[i][0] > 20 ? p->buf[i][0] - 20 : 0;
        p->buf[i][1] = p->buf[i][1] > 20 ? p->buf[i][1] - 20 : 0;
        p->buf[i][2] = p->buf[i][2] > 20 ? p->buf[i][2] - 20 : 0;
    }
    int d = 1 + p->speed / 2;
    for (int i = 0; i < d; i++) {
        int idx = (int)(rng_next(&p->rng) % KEY_COUNT);
        memcpy(p->buf[idx], p->color, 3);
    }
}

static void render_snake(f87_preview_t *p)
{
    memset(p->buf, 0, sizeof(p->buf));
    int len = 6;
    int head = (int)((float)p->frame * speed_mult(p->speed) * 0.5f) % KEY_COUNT;
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
    float t = (float)p->frame * speed_mult(p->speed) * 0.3f;
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
    float t = (float)p->frame * speed_mult(p->speed) * 0.2f;
    float cx = 9.0f, cy = 3.0f;
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
    float t = (float)p->frame * speed_mult(p->speed) * 0.15f;
    for (int i = 0; i < KEY_COUNT; i++) {
        float row = (float)f87_key_layout[i].row;
        float hue = fmodf(row * 60.0f + t * 40.0f, 360.0f);
        hsv(hue, 1.0f, 1.0f, &p->buf[i][0], &p->buf[i][1], &p->buf[i][2]);
    }
}

static void render_center_ripple(f87_preview_t *p)
{
    float t = (float)p->frame * speed_mult(p->speed) * 0.2f;
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
        p->buf[i][0] = r; p->buf[i][1] = g; p->buf[i][2] = b;
    }
}

/* --- Fire: Doom fire --- */
static void render_fire(f87_preview_t *p)
{
    fire_state_t *s = p->state;
    float decay_base = 0.15f - (float)p->speed * 0.02f;
    if (decay_base < 0.05f) decay_base = 0.05f;

    for (int c = 0; c < GRID_COLS; c++) s->heat[GRID_ROWS - 1][c] = 1.0f;
    for (int r = 0; r < GRID_ROWS - 1; r++) {
        for (int c = 0; c < GRID_COLS; c++) {
            int sc = c + (int)(rng_next(&p->rng) % 3) - 1;
            if (sc < 0) sc = 0;
            if (sc >= GRID_COLS) sc = GRID_COLS - 1;
            float d = decay_base * (float)(rng_next(&p->rng) % 100) / 100.0f;
            s->heat[r][c] = s->heat[r + 1][sc] - d;
            if (s->heat[r][c] < 0) s->heat[r][c] = 0;
        }
    }
    for (int i = 0; i < KEY_COUNT; i++) {
        int row = f87_key_layout[i].row, col = f87_key_layout[i].col;
        if (row >= GRID_ROWS || col >= GRID_COLS) { memset(p->buf[i], 0, 3); continue; }
        float h = s->heat[row][col];
        if (h < 0.33f) {
            float t = h / 0.33f;
            p->buf[i][0] = (uint8_t)(p->color[0] * t); p->buf[i][1] = 0; p->buf[i][2] = 0;
        } else if (h < 0.66f) {
            float t = (h - 0.33f) / 0.33f;
            p->buf[i][0] = p->color[0]; p->buf[i][1] = (uint8_t)(p->color[1] * t); p->buf[i][2] = 0;
        } else {
            float t = (h - 0.66f) / 0.34f;
            p->buf[i][0] = p->color[0]; p->buf[i][1] = p->color[1]; p->buf[i][2] = (uint8_t)(255 * t);
        }
    }
}

/* --- Matrix: falling drops with trails --- */
static void render_matrix(f87_preview_t *p)
{
    matrix_state_t *s = p->state;
    float sm = speed_mult(p->speed);
    for (int r = 0; r < GRID_ROWS; r++)
        for (int c = 0; c < GRID_COLS; c++)
            s->trail[r][c] *= 0.85f;
    for (int c = 0; c < GRID_COLS; c++) {
        s->drops[c] += s->speeds[c] * sm;
        int row = (int)s->drops[c];
        if (row >= 0 && row < GRID_ROWS) s->trail[row][c] = 1.0f;
        if (row >= GRID_ROWS + 3) {
            s->drops[c] = -(float)(rng_next(&p->rng) % (GRID_ROWS * 2));
            s->speeds[c] = 0.1f + (float)(rng_next(&p->rng) % 50) / 100.0f;
        }
    }
    for (int i = 0; i < KEY_COUNT; i++) {
        int row = f87_key_layout[i].row, col = f87_key_layout[i].col;
        if (row >= GRID_ROWS || col >= GRID_COLS) { memset(p->buf[i], 0, 3); continue; }
        float v = s->trail[row][col];
        p->buf[i][0] = (uint8_t)(p->color[0] * v);
        p->buf[i][1] = (uint8_t)(p->color[1] * v);
        p->buf[i][2] = (uint8_t)(p->color[2] * v);
    }
}

/* --- Plasma --- */
static void render_plasma(f87_preview_t *p)
{
    float t = (float)p->frame * (0.02f + (float)p->speed * 0.02f);
    for (int i = 0; i < KEY_COUNT; i++) {
        float x = (float)f87_key_layout[i].col / 22.0f;
        float y = (float)f87_key_layout[i].row / 6.0f;
        float v = sinf(x * 10 + t) + sinf(y * 10 + t * 0.7f) +
                  sinf((x + y) * 5 + t * 1.3f) + sinf(sqrtf(x*x + y*y) * 8 - t);
        float hue = fmodf((v + 4.0f) * 45.0f, 360.0f);
        hsv(hue, 0.8f, 0.9f, &p->buf[i][0], &p->buf[i][1], &p->buf[i][2]);
    }
}

/* --- Radar: rotating sweep --- */
static void render_radar(f87_preview_t *p)
{
    radar_state_t *s = p->state;
    float rot = 0.05f + (float)p->speed * 0.03f;
    s->angle += rot;
    if (s->angle > 2.0f * (float)M_PI) s->angle -= 2.0f * (float)M_PI;
    for (int k = 0; k < KEY_COUNT; k++) s->trail[k] *= 0.92f;
    float cx = 10.0f, cy = 2.5f;
    for (int k = 0; k < KEY_COUNT; k++) {
        float kx = (float)f87_key_layout[k].col - cx;
        float ky = (float)f87_key_layout[k].row - cy;
        float a = atan2f(ky, kx); if (a < 0) a += 2.0f * (float)M_PI;
        float diff = fabsf(a - s->angle);
        if (diff > (float)M_PI) diff = 2.0f * (float)M_PI - diff;
        if (diff < 0.26f) {
            float intensity = 1.0f - diff / 0.26f;
            if (intensity > s->trail[k]) s->trail[k] = intensity;
        }
        float v = s->trail[k];
        p->buf[k][0] = (uint8_t)(p->color[0] * v);
        p->buf[k][1] = (uint8_t)(p->color[1] * v);
        p->buf[k][2] = (uint8_t)(p->color[2] * v);
    }
}

/* --- Lightning: random bolts --- */
static int find_neighbor_preview(int key_id, uint32_t *seed)
{
    int row = f87_key_layout[key_id].row, col = f87_key_layout[key_id].col;
    int dr = (int)(rng_next(seed) % 3) - 1, dc = (int)(rng_next(seed) % 3) - 1;
    if (dr == 0 && dc == 0) dc = 1;
    int tr = row + dr, tc = col + dc, best = -1, bd = 999;
    for (int k = 0; k < KEY_COUNT; k++) {
        int d = abs(f87_key_layout[k].row - tr) + abs(f87_key_layout[k].col - tc);
        if (d < bd) { bd = d; best = k; }
    }
    return best;
}

static void render_lightning(f87_preview_t *p)
{
    lightning_state_t *s = p->state;
    for (int k = 0; k < KEY_COUNT; k++) s->glow[k] *= 0.8f;
    for (int b = 0; b < 3; b++) {
        if (s->bolts[b].length > 0) { s->bolts[b].age++; if (s->bolts[b].age > 8) s->bolts[b].length = 0; }
    }
    s->cooldown--;
    int spawn_rate = 10 - p->speed * 2; if (spawn_rate < 2) spawn_rate = 2;
    if (s->cooldown <= 0) {
        for (int b = 0; b < 3; b++) {
            if (s->bolts[b].length == 0) {
                int start = rng_next(&p->rng) % KEY_COUNT;
                s->bolts[b].path[0] = start; s->bolts[b].length = 1; s->bolts[b].age = 0;
                int len = 4 + (int)(rng_next(&p->rng) % 6), cur = start;
                for (int i = 1; i < len && i < 12; i++) {
                    cur = find_neighbor_preview(cur, &p->rng);
                    if (cur < 0) break;
                    s->bolts[b].path[i] = cur; s->bolts[b].length++;
                }
                s->cooldown = spawn_rate; break;
            }
        }
    }
    for (int b = 0; b < 3; b++) {
        if (s->bolts[b].length == 0) continue;
        float intensity = 1.0f - (float)s->bolts[b].age / 8.0f;
        for (int i = 0; i < s->bolts[b].length; i++) {
            int k = s->bolts[b].path[i];
            if (intensity > s->glow[k]) s->glow[k] = intensity;
        }
    }
    for (int k = 0; k < KEY_COUNT; k++) {
        float v = s->glow[k];
        float white = v > 0.5f ? (v - 0.5f) * 2.0f : 0.0f;
        float cr = white * 255 + (1 - white) * p->color[0] * v;
        float cg = white * 255 + (1 - white) * p->color[1] * v;
        float cb = white * 255 + (1 - white) * p->color[2] * v;
        if (cr > 255) cr = 255;
        if (cg > 255) cg = 255;
        if (cb > 255) cb = 255;
        p->buf[k][0] = (uint8_t)cr; p->buf[k][1] = (uint8_t)cg; p->buf[k][2] = (uint8_t)cb;
    }
}

/* --- Reactive: simulated keypress -> expanding rings --- */
static void inject_reactive_key(f87_preview_t *p, int key_id)
{
    reactive_state_t *s = p->state;
    if (!s) return;
    int idx = s->idx % REACTIVE_MAX;
    s->items[idx].key_id = key_id;
    s->items[idx].radius = 0.1f;
    s->items[idx].strength = 1.0f;
    s->items[idx].hue = (float)(rng_next(&p->rng) % 360);
    s->idx++;
}

static void render_spectrum_hw(f87_preview_t *p)
{
    /* Spectrum: horizontal line spread on same row only */
    reactive_state_t *s = p->state;

    for (int e = 0; e < REACTIVE_MAX; e++) {
        if (s->items[e].strength <= 0) continue;
        s->items[e].radius += 0.4f + (float)p->speed * 0.15f;
        s->items[e].strength *= 0.90f;
        if (s->items[e].strength < 0.01f) s->items[e].strength = 0;
    }

    memset(p->buf, 0, sizeof(p->buf));
    for (int k = 0; k < KEY_COUNT; k++) {
        float max_v = 0;
        float best_hue = 0;
        float best_dist = 0;
        for (int e = 0; e < REACTIVE_MAX; e++) {
            if (s->items[e].strength <= 0) continue;
            int src = s->items[e].key_id;
            if (f87_key_layout[k].row != f87_key_layout[src].row) continue;
            float dx = fabsf((float)(f87_key_layout[k].col - f87_key_layout[src].col));
            if (dx <= s->items[e].radius) {
                float v = (1.0f - dx / (s->items[e].radius + 1.0f)) * s->items[e].strength;
                if (v > max_v) { max_v = v; best_hue = s->items[e].hue; best_dist = dx; }
            }
        }
        if (max_v > 0) {
            if (p->colorful) {
                /* Color shifts with distance from source — rainbow spread */
                float dist_hue = fmodf(best_hue + best_dist * 25.0f, 360.0f);
                uint8_t cr, cg, cb;
                hsv(dist_hue, 1.0f, max_v, &cr, &cg, &cb);
                p->buf[k][0] = cr; p->buf[k][1] = cg; p->buf[k][2] = cb;
            } else {
                p->buf[k][0] = (uint8_t)(p->color[0] * max_v);
                p->buf[k][1] = (uint8_t)(p->color[1] * max_v);
                p->buf[k][2] = (uint8_t)(p->color[2] * max_v);
            }
        }
    }
}

static void render_explode(f87_preview_t *p)
{
    reactive_state_t *s = p->state;

    float max_r = 2.0f + (float)p->speed * 2.0f;
    for (int e = 0; e < REACTIVE_MAX; e++) {
        if (s->items[e].strength <= 0) continue;
        s->items[e].radius += 0.3f;
        s->items[e].strength *= (s->items[e].radius > max_r) ? 0.8f : 0.92f;
        if (s->items[e].strength < 0.01f) s->items[e].strength = 0;
    }
    memset(p->buf, 0, sizeof(p->buf));
    for (int k = 0; k < KEY_COUNT; k++) {
        float max_v = 0; float best_hue = 0;
        for (int e = 0; e < REACTIVE_MAX; e++) {
            if (s->items[e].strength <= 0) continue;
            int src = s->items[e].key_id;
            float dx = (float)(f87_key_layout[k].col - f87_key_layout[src].col);
            float dy = (float)(f87_key_layout[k].row - f87_key_layout[src].row) * 2.0f;
            float dist = sqrtf(dx*dx + dy*dy);
            if (dist > max_r) continue;
            float ring = fabsf(dist - s->items[e].radius);
            if (ring < 1.5f) {
                float v = (1.0f - ring / 1.5f) * s->items[e].strength;
                if (v > max_v) { max_v = v; best_hue = s->items[e].hue; }
            }
        }
        if (max_v > 0) {
            hsv(best_hue, 1.0f, max_v, &p->buf[k][0], &p->buf[k][1], &p->buf[k][2]);
        }
    }
}

static void render_ripple_sw(f87_preview_t *p)
{
    reactive_state_t *s = p->state;

    float expand = 0.3f + (float)p->speed * 0.15f;
    for (int w = 0; w < REACTIVE_MAX; w++) {
        if (s->items[w].strength <= 0) continue;
        s->items[w].radius += expand;
        s->items[w].strength *= 0.92f;
        if (s->items[w].strength < 0.01f) s->items[w].strength = 0;
    }
    memset(p->buf, 0, sizeof(p->buf));
    for (int k = 0; k < KEY_COUNT; k++) {
        float total = 0;
        for (int w = 0; w < REACTIVE_MAX; w++) {
            if (s->items[w].strength <= 0) continue;
            int src = s->items[w].key_id;
            float dx = (float)(f87_key_layout[k].col - f87_key_layout[src].col);
            float dy = (float)(f87_key_layout[k].row - f87_key_layout[src].row) * 2.0f;
            float dist = sqrtf(dx*dx + dy*dy);
            if (dist < s->items[w].radius + 2.0f) {
                float phase = dist * 1.5f - s->items[w].radius * 1.5f;
                float wave = cosf(phase) * 0.5f + 0.5f;
                float falloff = 1.0f - dist / (s->items[w].radius + 3.0f);
                if (falloff < 0) falloff = 0;
                total += wave * falloff * s->items[w].strength;
            }
        }
        if (total > 1.0f) total = 1.0f;
        p->buf[k][0] = (uint8_t)(p->color[0] * total);
        p->buf[k][1] = (uint8_t)(p->color[1] * total);
        p->buf[k][2] = (uint8_t)(p->color[2] * total);
    }
}

static void render_typewriter(f87_preview_t *p)
{
    typewriter_state_t *s = p->state;
    float decay = 0.96f - (float)p->speed * 0.01f;
    for (int k = 0; k < KEY_COUNT; k++) {
        s->heat[k] *= decay;
        float h = s->heat[k];
        if (h < 0.01f) { memset(p->buf[k], 0, 3); continue; }
        /* white -> color -> dark gradient */
        if (h > 0.7f) {
            p->buf[k][0] = 255; p->buf[k][1] = 255; p->buf[k][2] = (uint8_t)((h - 0.7f) / 0.3f * 200);
        } else if (h > 0.3f) {
            p->buf[k][0] = 255; p->buf[k][1] = (uint8_t)((h - 0.3f) / 0.4f * 200); p->buf[k][2] = 0;
        } else {
            p->buf[k][0] = (uint8_t)(h / 0.3f * 255); p->buf[k][1] = 0; p->buf[k][2] = 0;
        }
    }
}

static void render_life(f87_preview_t *p)
{
    life_state_t *s = p->state;
    int interval = 15 - p->speed * 3; if (interval < 3) interval = 3;

    if (0) {
        int k = 0; /* Life cells are seeded by f87_preview_on_key */
        int row = f87_key_layout[k].row, col = f87_key_layout[k].col;
        for (int dr = -1; dr <= 1; dr++)
            for (int dc = -1; dc <= 1; dc++) {
                int r = row + dr, c = col + dc;
                if (r >= 0 && r < GRID_ROWS && c >= 0 && c < GRID_COLS) {
                    s->grid[r][c] = 1; s->bright[r][c] = 1.0f;
                }
            }
    }

    s->step++;
    if (s->step >= interval) {
        s->step = 0;
        for (int r = 0; r < GRID_ROWS; r++)
            for (int c = 0; c < GRID_COLS; c++) {
                int n = 0;
                for (int dr = -1; dr <= 1; dr++)
                    for (int dc = -1; dc <= 1; dc++) {
                        if (dr == 0 && dc == 0) continue;
                        int nr = r + dr, nc = c + dc;
                        if (nr >= 0 && nr < GRID_ROWS && nc >= 0 && nc < GRID_COLS)
                            n += s->grid[nr][nc];
                    }
                s->next[r][c] = s->grid[r][c] ? (n == 2 || n == 3) : (n == 3);
            }
        memcpy(s->grid, s->next, sizeof(s->grid));
    }
    for (int r = 0; r < GRID_ROWS; r++)
        for (int c = 0; c < GRID_COLS; c++) {
            float target = s->grid[r][c] ? 1.0f : 0.0f;
            s->bright[r][c] += (target - s->bright[r][c]) * 0.3f;
        }
    for (int i = 0; i < KEY_COUNT; i++) {
        int row = f87_key_layout[i].row, col = f87_key_layout[i].col;
        if (row >= GRID_ROWS || col >= GRID_COLS) { memset(p->buf[i], 0, 3); continue; }
        float v = s->bright[row][col];
        p->buf[i][0] = (uint8_t)(p->color[0] * v);
        p->buf[i][1] = (uint8_t)(p->color[1] * v);
        p->buf[i][2] = (uint8_t)(p->color[2] * v);
    }
}

static void render_keyheat(f87_preview_t *p)
{
    keyheat_state_t *s = p->state;
    /* KeyHeat accumulates from f87_preview_on_key — no auto-sim */
    if (0) {
        int k = 0;
        s->heat[k] += 1.0f;
        if (s->heat[k] > s->max_heat) s->max_heat = s->heat[k];
    }
    float scale = 1.0f - (float)p->speed * 0.2f; if (scale < 0.2f) scale = 0.2f;
    int t1 = (int)(5*scale); if(t1<1) t1=1;
    int t2 = (int)(15*scale); if(t2<2) t2=2;
    int t3 = (int)(30*scale); if(t3<3) t3=3;
    int t4 = (int)(60*scale); if(t4<4) t4=4;
    int t5 = (int)(100*scale); if(t5<5) t5=5;
    memset(p->buf, 0, sizeof(p->buf));
    for (int k = 0; k < KEY_COUNT; k++) {
        int pr = (int)s->heat[k]; if (pr < 1) continue;
        float r, g, b;
        if (pr < t1) { float t = (float)pr/t1; r=0; g=0; b=t*200; }
        else if (pr < t2) { float t = (float)(pr-t1)/(t2-t1); r=0; g=(0.4f+t*0.6f)*255; b=(1-t)*200; }
        else if (pr < t3) { float t = (float)(pr-t2)/(t3-t2); r=t*255; g=255; b=0; }
        else if (pr < t4) { float t = (float)(pr-t3)/(t4-t3); r=255; g=(1-t*0.6f)*255; b=0; }
        else if (pr < t5) { float t = (float)(pr-t4)/(t5-t4); r=255; g=(0.4f-t*0.4f)*255; b=0; }
        else { r=255; g=0; b=0; }
        p->buf[k][0] = (uint8_t)r; p->buf[k][1] = (uint8_t)g; p->buf[k][2] = (uint8_t)b;
    }
}

static void render_spectrum_music(f87_preview_t *p)
{
    float t = (float)p->frame * speed_mult(p->speed) * 0.12f;
    for (int i = 0; i < KEY_COUNT; i++) {
        float col = (float)f87_key_layout[i].col;
        float row = (float)f87_key_layout[i].row;
        float level = (sinf(col * 0.4f + t) + 1.0f) * 0.5f;
        float row_norm = 1.0f - row / 6.0f;
        if (row_norm < level) {
            float hue = col * 20.0f;
            hsv(hue, 1.0f, 1.0f, &p->buf[i][0], &p->buf[i][1], &p->buf[i][2]);
        } else {
            memset(p->buf[i], 0, 3);
        }
    }
}

/* ===== Dispatch ===== */

static void free_state(f87_preview_t *p)
{
    if (p->state) { g_free(p->state); p->state = NULL; }
}

static void alloc_state(f87_preview_t *p)
{
    free_state(p);
    switch (p->effect_id) {
    case 100: p->state = g_new0(fire_state_t, 1); break;
    case 101: {
        matrix_state_t *s = g_new0(matrix_state_t, 1);
        for (int c = 0; c < GRID_COLS; c++) {
            s->drops[c] = -(float)(rng_next(&p->rng) % (GRID_ROWS * 3));
            s->speeds[c] = 0.1f + (float)(rng_next(&p->rng) % 50) / 100.0f;
        }
        p->state = s; break;
    }
    case 104: p->state = g_new0(radar_state_t, 1); break;
    case 105: p->state = g_new0(lightning_state_t, 1); break;
    case 4: case 7: case 110: case 111: p->state = g_new0(reactive_state_t, 1); break;
    case 12: case 112: p->state = g_new0(typewriter_state_t, 1); break;
    case 113: p->state = g_new0(life_state_t, 1); break;
    case 114: { keyheat_state_t *s = g_new0(keyheat_state_t, 1); s->max_heat = 1; p->state = s; break; }
    default: break;
    }
}

static void render_frame(f87_preview_t *p)
{
    switch (p->effect_id) {
    case 0:  render_off(p); break;
    case 1:  render_static(p); break;
    case 2:  render_breathing(p); break;
    case 3:  render_wave(p); break;
    case 4:  render_spectrum_hw(p); break;
    case 5:  render_rain(p); break;
    case 7:  render_ripple_sw(p); break;  /* HW ripple ~ SW ripple visually */
    case 8:  render_starlight(p); break;
    case 10: render_snake(p); break;
    case 11: render_plasma(p); break;
    case 12: render_typewriter(p); break; /* HW reactive ~ typewriter glow */
    case 13: render_marquee(p); break;
    case 15: render_circle(p); break;
    case 16: render_raindown(p); break;
    case 17: render_center_ripple(p); break;
    case 100: render_fire(p); break;
    case 101: render_matrix(p); break;
    case 102: render_plasma(p); break;
    case 104: render_radar(p); break;
    case 105: render_lightning(p); break;
    case 110: render_explode(p); break;
    case 111: render_ripple_sw(p); break;
    case 112: render_typewriter(p); break;
    case 113: render_life(p); break;
    case 114: render_keyheat(p); break;
    case 200: render_spectrum_music(p); break;
    case 201: render_breathing(p); break;
    case 202: render_breathing(p); break;
    case 203: render_spectrum_music(p); break;
    case 204: render_wave(p); break;
    case 106: render_fire(p); break;
    default:  render_breathing(p); break;
    }
}

static gboolean on_preview_tick(gpointer data)
{
    f87_preview_t *p = data;
    p->frame++;
    render_frame(p);
    f87_keyboard_view_set_all_keys(p->keyboard, p->buf, KEY_COUNT);
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
    free_state(prev);
    g_free(prev);
}

void f87_preview_start(f87_preview_t *prev, int effect_id, const char *category,
                        uint8_t speed, uint8_t r, uint8_t g, uint8_t b)
{
    f87_preview_stop(prev);
    free_state(prev);

    prev->effect_id = effect_id;
    strncpy(prev->category, category ? category : "", sizeof(prev->category) - 1);
    prev->speed = speed;
    prev->color[0] = r; prev->color[1] = g; prev->color[2] = b;
    prev->frame = 0;
    memset(prev->buf, 0, sizeof(prev->buf));

    if (effect_id == 18 || effect_id == 0) return;

    alloc_state(prev);
    prev->timer = g_timeout_add(1000 / PREVIEW_FPS, on_preview_tick, prev);
}

void f87_preview_set_speed(f87_preview_t *prev, uint8_t speed) { prev->speed = speed; }
void f87_preview_set_color(f87_preview_t *prev, uint8_t r, uint8_t g, uint8_t b)
{
    prev->color[0] = r; prev->color[1] = g; prev->color[2] = b;
}

void f87_preview_set_colorful(f87_preview_t *prev, int colorful)
{
    prev->colorful = colorful;
}

void f87_preview_stop(f87_preview_t *prev)
{
    if (prev->timer) { g_source_remove(prev->timer); prev->timer = 0; }
}

/* Check if effect is reactive (needs keypress) */
void f87_preview_on_key(f87_preview_t *prev, int key_id)
{
    if (!prev || key_id < 0 || key_id >= KEY_COUNT) return;

    switch (prev->effect_id) {
    case 4:   /* Spectrum HW */
    case 7:   /* Ripple HW */
    case 110: /* Explode */
    case 111: /* Ripple SW */
        inject_reactive_key(prev, key_id);
        break;
    case 12:  /* Reactive HW — single key glow */
    case 112: { /* Typewriter */
        typewriter_state_t *s = prev->state;
        if (s) s->heat[key_id] = 1.0f;
        break;
    }
    case 113: { /* Life */
        life_state_t *s = prev->state;
        if (s) {
            int row = f87_key_layout[key_id].row;
            int col = f87_key_layout[key_id].col;
            for (int dr = -1; dr <= 1; dr++)
                for (int dc = -1; dc <= 1; dc++) {
                    int r = row + dr, c = col + dc;
                    if (r >= 0 && r < GRID_ROWS && c >= 0 && c < GRID_COLS) {
                        s->grid[r][c] = 1;
                        s->bright[r][c] = 1.0f;
                    }
                }
        }
        break;
    }
    case 114: { /* KeyHeat */
        keyheat_state_t *s = prev->state;
        if (s) {
            s->heat[key_id] += 1.0f;
            if (s->heat[key_id] > s->max_heat)
                s->max_heat = s->heat[key_id];
        }
        break;
    }
    default:
        break;
    }
}

int f87_preview_is_reactive(int effect_id)
{
    return effect_id == 4 || effect_id == 7 || effect_id == 12 ||
           effect_id == 110 || effect_id == 111 || effect_id == 112 ||
           effect_id == 113 || effect_id == 114;
}
