# GUI Kapsamlı Yeniden Tasarım + i18n Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Efektlerin aldığı parametrelere göre kontrol panelini uyarlamak, gettext i18n eklemek, ve GUI kalitesini artıran iyileştirmeler yapmak.

**Architecture:** Metadata-driven kontrol paneli — her efekt için statik flag tablosu hangi widget'ların gösterileceğini belirler. gettext ile İngilizce/Türkçe çeviri, `.po` dosyaları. Mevcut split layout korunur (sol: parametreler, sağ: renk seçici) ama efekte göre koşullu.

**Tech Stack:** C11, GTK4, libadwaita, Cairo, GNU gettext, CMake

**Spec:** `docs/superpowers/specs/2026-03-30-gui-controls-i18n-design.md`

---

## Dosya Haritası

| Dosya | Durum | Sorumluluk |
|-------|-------|-----------|
| `gui/src/i18n.h` | Yeni | gettext makroları (`_()`, `N_()`), init prototipi |
| `gui/src/i18n.c` | Yeni | Locale init, domain binding |
| `gui/src/effect_meta.h` | Yeni | `f87_param_flags` enum, `effect_meta_t` struct, lookup fonksiyonu |
| `gui/src/effect_meta.c` | Yeni | Statik metadata tablosu, `effect_meta_lookup()` |
| `gui/src/controls.h` | Değişiklik | Yeni public API: `f87_controls_get_color()`, `f87_controls_send()`, `f87_controls_stop()` |
| `gui/src/controls.c` | Büyük refactor | Birleşik builder, `create_color_picker()`, CSS fix, SV cache, paint butonları, `_()` |
| `gui/src/sidebar.c` | Değişiklik | Tooltip'ler, kategori `_()` |
| `gui/src/window.c` | Değişiklik | Rescan butonu, shortcut'lar, renk tutarlılığı, `_()` |
| `gui/src/keyboard_view.c` | Değişiklik | Responsive, drag-to-paint |
| `gui/src/app_state.c` | Değişiklik | `_()` ile status stringleri |
| `gui/src/main.c` | Değişiklik | `f87_i18n_init()` çağrısı |
| `gui/CMakeLists.txt` | Değişiklik | Yeni dosyalar, Intl, LOCALEDIR, mo derleme |
| `po/POTFILES.in` | Yeni | Çevrilecek dosya listesi |
| `po/tr.po` | Yeni | Türkçe çeviri |

---

### Task 1: i18n Altyapısı (gettext)

**Files:**
- Create: `gui/src/i18n.h`
- Create: `gui/src/i18n.c`
- Modify: `gui/src/main.c`
- Modify: `gui/CMakeLists.txt`
- Create: `po/POTFILES.in`

- [ ] **Step 1: `gui/src/i18n.h` oluştur**

```c
#ifndef F87_I18N_H
#define F87_I18N_H

#include <libintl.h>
#include <locale.h>

#define _(str) gettext(str)
#define N_(str) str

void f87_i18n_init(void);

#endif /* F87_I18N_H */
```

- [ ] **Step 2: `gui/src/i18n.c` oluştur**

```c
#include "i18n.h"

#ifndef LOCALEDIR
#define LOCALEDIR "/usr/local/share/locale"
#endif

#define GETTEXT_PACKAGE "f87control"

void f87_i18n_init(void)
{
    setlocale(LC_ALL, "");
    bindtextdomain(GETTEXT_PACKAGE, LOCALEDIR);
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain(GETTEXT_PACKAGE);
}
```

- [ ] **Step 3: `gui/src/main.c`'ye init çağrısı ekle**

`main()` fonksiyonunun başına, `f87_log_init`'ten sonra:

```c
#include "i18n.h"

int main(int argc, char *argv[])
{
    f87_i18n_init();
    f87_log_init(F87_LOG_STDERR);
    /* ... mevcut kod ... */
}
```

- [ ] **Step 4: `gui/CMakeLists.txt`'yi güncelle — i18n.c ekle, LOCALEDIR tanımla**

`add_executable` bloğuna `src/i18n.c` ekle:

```cmake
add_executable(f87control
    src/main.c
    src/i18n.c
    src/window.c
    src/sidebar.c
    src/app_state.c
    src/controls.c
    src/keyboard_view.c
    src/preview.c
    ${GRESOURCE_C}
)
```

`target_compile_options`'tan sonra LOCALEDIR tanımı ekle:

```cmake
target_compile_definitions(f87control PRIVATE
    LOCALEDIR="${CMAKE_INSTALL_PREFIX}/share/locale"
)
```

- [ ] **Step 5: `po/POTFILES.in` oluştur**

```
gui/src/main.c
gui/src/i18n.c
gui/src/window.c
gui/src/sidebar.c
gui/src/controls.c
gui/src/app_state.c
gui/src/keyboard_view.c
```

- [ ] **Step 6: Build doğrula**

```bash
cd build && cmake .. -DBUILD_GUI=ON && make f87control 2>&1 | tail -5
```

Expected: Hatasız derleme. `f87_i18n_init()` çağrılır ama henüz `.mo` dosyası olmadığı için tüm stringler orijinal (İngilizce) kalır.

- [ ] **Step 7: Commit**

```bash
git add gui/src/i18n.h gui/src/i18n.c gui/src/main.c gui/CMakeLists.txt po/POTFILES.in
git commit -m "feat(gui): add gettext i18n infrastructure"
```

---

### Task 2: Efekt Metadata Tablosu

**Files:**
- Create: `gui/src/effect_meta.h`
- Create: `gui/src/effect_meta.c`
- Modify: `gui/CMakeLists.txt`

- [ ] **Step 1: `gui/src/effect_meta.h` oluştur**

```c
#ifndef F87_EFFECT_META_H
#define F87_EFFECT_META_H

#include <stdint.h>

typedef enum {
    F87_PARAM_BRIGHTNESS = (1 << 0),
    F87_PARAM_SPEED      = (1 << 1),
    F87_PARAM_COLOR      = (1 << 2),
    F87_PARAM_COLORFUL   = (1 << 3),
    F87_PARAM_AUDIO      = (1 << 4),
    F87_PARAM_PROFILE    = (1 << 5),
    F87_PARAM_PAINT      = (1 << 6),
} f87_param_flags;

typedef struct {
    int effect_id;
    uint32_t flags;
    const char *tag;  /* "rainbow", NULL, etc. — not translated */
} effect_meta_t;

/* Returns metadata for given effect_id. Never returns NULL — unknown IDs
   return a default entry with only BRIGHTNESS flag. */
const effect_meta_t *effect_meta_lookup(int effect_id);

#endif /* F87_EFFECT_META_H */
```

- [ ] **Step 2: `gui/src/effect_meta.c` oluştur**

```c
#include "effect_meta.h"

#define B  F87_PARAM_BRIGHTNESS
#define S  F87_PARAM_SPEED
#define C  F87_PARAM_COLOR
#define CF F87_PARAM_COLORFUL
#define A  F87_PARAM_AUDIO
#define P  F87_PARAM_PROFILE
#define PT F87_PARAM_PAINT

static const effect_meta_t meta_table[] = {
    /* HW effects */
    {  0, 0,                          NULL },       /* Off */
    {  1, B | C,                      NULL },       /* Static */
    {  2, B | S | C | CF,             NULL },       /* Breathing */
    {  3, B | S,                      "rainbow" },  /* Wave */
    {  4, B | S | C | CF,             NULL },       /* Spectrum */
    {  5, B | S | C | CF,             NULL },       /* Rain */
    {  7, B | S | C | CF,             NULL },       /* Ripple */
    {  8, B | S | C | CF,             NULL },       /* Starlight */
    { 10, B | S | C | CF,             NULL },       /* Snake */
    { 11, B | S | C | CF,             NULL },       /* Aurora */
    { 12, B | S | C | CF,             NULL },       /* Reactive */
    { 13, B | S | C | CF,             NULL },       /* Marquee */
    { 15, B | S,                      "rainbow" },  /* Circle */
    { 16, B | S,                      "rainbow" },  /* Rain Down */
    { 17, B | S,                      "rainbow" },  /* Center Ripple */
    { 18, B | C | PT,                 NULL },       /* Custom */

    /* SW effects */
    {100, B | S | C,                  NULL },       /* Fire */
    {101, B | S | C,                  NULL },       /* Matrix */
    {102, B | S,                      "rainbow" },  /* Plasma */
    {103, B,                          NULL },       /* Heatmap */
    {104, B | S | C,                  NULL },       /* Radar */
    {105, B | S | C,                  NULL },       /* Lightning */
    {110, B | S,                      "rainbow" },  /* Explode */
    {111, B | S | C,                  NULL },       /* Ripple SW */
    {112, B | S,                      NULL },       /* Typewriter */
    {113, B | S | C,                  NULL },       /* Life */
    {114, B | S,                      NULL },       /* KeyHeat */

    /* Music effects */
    {200, B | S | A,                  NULL },       /* Spectrum */
    {201, B | A,                      NULL },       /* Beat */
    {202, B | S | C | A,             NULL },       /* Energy */
    {203, B | S | A,                  NULL },       /* VU */
    {204, B | S | A,                  NULL },       /* FreqMap */

    /* Sensor */
    {106, B | P,                      NULL },       /* developer */
    {107, B | P,                      NULL },       /* gamer */
    {108, B | P,                      NULL },       /* system */
};

#undef B
#undef S
#undef C
#undef CF
#undef A
#undef P
#undef PT

static const effect_meta_t default_meta = { -1, F87_PARAM_BRIGHTNESS, NULL };

#define META_COUNT (sizeof(meta_table) / sizeof(meta_table[0]))

const effect_meta_t *effect_meta_lookup(int effect_id)
{
    for (unsigned i = 0; i < META_COUNT; i++) {
        if (meta_table[i].effect_id == effect_id)
            return &meta_table[i];
    }
    return &default_meta;
}
```

- [ ] **Step 3: `gui/CMakeLists.txt`'ye `effect_meta.c` ekle**

`add_executable` bloğuna `src/effect_meta.c` ekle:

```cmake
add_executable(f87control
    src/main.c
    src/i18n.c
    src/effect_meta.c
    src/window.c
    src/sidebar.c
    src/app_state.c
    src/controls.c
    src/keyboard_view.c
    src/preview.c
    ${GRESOURCE_C}
)
```

- [ ] **Step 4: Build doğrula**

```bash
cd build && cmake .. -DBUILD_GUI=ON && make f87control 2>&1 | tail -5
```

Expected: Hatasız derleme. `effect_meta.o` yeni obje olarak linklenir.

- [ ] **Step 5: Commit**

```bash
git add gui/src/effect_meta.h gui/src/effect_meta.c gui/CMakeLists.txt
git commit -m "feat(gui): add effect parameter metadata table"
```

---

### Task 3: controls.c Refactor — Birleşik Builder + Renk Seçici Ayrıştırma + CSS Fix

Bu en büyük task — `controls.c`'nin tamamını yeniden yazıyor.

**Files:**
- Modify: `gui/src/controls.h`
- Rewrite: `gui/src/controls.c`

- [ ] **Step 1: `gui/src/controls.h`'yi güncelle — yeni public API**

```c
#ifndef F87_CONTROLS_H
#define F87_CONTROLS_H

#include <adwaita.h>
#include "app_state.h"
#include "keyboard_view.h"

typedef struct _F87Controls F87Controls;

typedef void (*F87ControlsStatusCallback)(const char *text, gpointer user_data);

F87Controls *f87_controls_new(f87_app_state_t *state,
                               F87ControlsStatusCallback status_cb,
                               gpointer user_data);

void f87_controls_set_effect(F87Controls *ctrl, const char *category,
                              const char *effect_name, int effect_id);

void f87_controls_set_keyboard(F87Controls *ctrl, F87KeyboardView *keyboard);

GtkWidget *f87_controls_get_widget(F87Controls *ctrl);

/* Get current selected color (3-byte RGB array) */
const uint8_t *f87_controls_get_color(F87Controls *ctrl);

/* Trigger send/stop from external shortcut */
void f87_controls_send(F87Controls *ctrl);
void f87_controls_stop(F87Controls *ctrl);

#endif /* F87_CONTROLS_H */
```

- [ ] **Step 2: `gui/src/controls.c` yeniden yaz**

Tam dosya. Mevcut `controls.c`'nin tamamı bu ile değiştirilir. Önemli değişiklikler:
- `#include "effect_meta.h"` ve `#include "i18n.h"` eklendi
- `create_color_picker()` tek fonksiyon olarak çıkarıldı (tekrar yok)
- `ensure_preset_css()` ile preset CSS bir kez yükleniyor
- `sync_color_from_hsv()` tek `swatch_provider` tutuyor (sızıntı düzeltmesi)
- `build_controls_for_effect()` tek birleşik builder — flag tablosuna bakıyor
- SV gradient hue cache (`sv_cache` + `sv_cache_hue`)
- Paint mode: fill/clear butonları
- Tüm kullanıcı görünür stringler `_()` ile

```c
#include "controls.h"
#include "effect_meta.h"
#include "preview.h"
#include "i18n.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

/* Preset color palette */
static const uint8_t preset_colors[][3] = {
    {255,   0,   0}, {255,  80,   0}, {255, 165,   0}, {255, 255,   0},
    {  0, 255,   0}, {  0, 255, 128}, {  0, 255, 255}, {  0, 128, 255},
    {  0,   0, 255}, {128,   0, 255}, {255,   0, 255}, {255,   0, 128},
};
#define NUM_PRESETS 12

struct _F87Controls {
    f87_app_state_t *state;
    F87ControlsStatusCallback status_cb;
    gpointer user_data;

    GtkBox *container;
    GtkBox *params_box;

    char category[16];
    char effect_name[64];
    int effect_id;

    GtkScale *brightness_scale;
    GtkScale *speed_scale;
    GtkScale *gain_scale;
    GtkSwitch *auto_gain_switch;
    GtkSwitch *colorful_switch;
    GtkDropDown *source_dropdown;
    GtkDropDown *sensor_profile_dropdown;
    GtkButton *send_button;
    GtkButton *stop_button;

    /* Color picker state */
    float hue;
    float sat;
    float val;
    uint8_t selected_color[3];
    GtkDrawingArea *sv_area;
    GtkScale *hue_scale;
    GtkEntry *hex_entry;
    GtkWidget *preview_swatch;
    GtkCssProvider *swatch_provider;
    gboolean sv_dragging;

    /* SV gradient cache */
    cairo_surface_t *sv_cache;
    float sv_cache_hue;
    int sv_cache_w;
    int sv_cache_h;

    guint loading_timer;

    F87KeyboardView *keyboard;
    f87_preview_t *preview;
};

static void update_status(F87Controls *ctrl, const char *text)
{
    if (ctrl->status_cb)
        ctrl->status_cb(text, ctrl->user_data);
}

/* ===== HSV <-> RGB ===== */

static void hsv_to_rgb(float h, float s, float v, uint8_t *r, uint8_t *g, uint8_t *b)
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

static void rgb_to_hsv(uint8_t r, uint8_t g, uint8_t b, float *h, float *s, float *v)
{
    float rf = r / 255.0f, gf = g / 255.0f, bf = b / 255.0f;
    float mx = fmaxf(rf, fmaxf(gf, bf));
    float mn = fminf(rf, fminf(gf, bf));
    float d = mx - mn;

    *v = mx;
    *s = (mx > 0) ? d / mx : 0;

    if (d < 0.001f) { *h = 0; return; }
    if (mx == rf)      *h = 60.0f * fmodf((gf - bf) / d, 6.0f);
    else if (mx == gf) *h = 60.0f * ((bf - rf) / d + 2.0f);
    else               *h = 60.0f * ((rf - gf) / d + 4.0f);
    if (*h < 0) *h += 360.0f;
}

static void sync_color_from_hsv(F87Controls *ctrl)
{
    hsv_to_rgb(ctrl->hue, ctrl->sat, ctrl->val,
               &ctrl->selected_color[0], &ctrl->selected_color[1], &ctrl->selected_color[2]);

    if (ctrl->hex_entry) {
        char hex[8];
        snprintf(hex, sizeof(hex), "%02X%02X%02X",
                 ctrl->selected_color[0], ctrl->selected_color[1], ctrl->selected_color[2]);
        gtk_editable_set_text(GTK_EDITABLE(ctrl->hex_entry), hex);
    }

    /* Reuse single CSS provider — no leak */
    if (ctrl->preview_swatch) {
        char css[128];
        snprintf(css, sizeof(css), ".color-preview { background: #%02x%02x%02x; }",
                 ctrl->selected_color[0], ctrl->selected_color[1], ctrl->selected_color[2]);
        if (!ctrl->swatch_provider) {
            ctrl->swatch_provider = gtk_css_provider_new();
            gtk_style_context_add_provider_for_display(
                gtk_widget_get_display(ctrl->preview_swatch),
                GTK_STYLE_PROVIDER(ctrl->swatch_provider),
                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        }
        gtk_css_provider_load_from_string(ctrl->swatch_provider, css);
    }

    if (ctrl->sv_area)
        gtk_widget_queue_draw(GTK_WIDGET(ctrl->sv_area));

    if (ctrl->preview)
        f87_preview_set_color(ctrl->preview,
                               ctrl->selected_color[0],
                               ctrl->selected_color[1],
                               ctrl->selected_color[2]);
}

/* ===== SV GRADIENT AREA ===== */

#define SV_SIZE 120

static void sv_draw(GtkDrawingArea *area, cairo_t *cr,
                     int width, int height, gpointer data)
{
    (void)area;
    F87Controls *ctrl = data;

    /* Rebuild cache only when hue or size changes */
    if (!ctrl->sv_cache || ctrl->sv_cache_hue != ctrl->hue ||
        ctrl->sv_cache_w != width || ctrl->sv_cache_h != height) {

        if (ctrl->sv_cache)
            cairo_surface_destroy(ctrl->sv_cache);

        ctrl->sv_cache = cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, height);
        uint32_t *pixels = (uint32_t *)cairo_image_surface_get_data(ctrl->sv_cache);
        int stride = cairo_image_surface_get_stride(ctrl->sv_cache) / 4;

        for (int y = 0; y < height; y++) {
            float v = 1.0f - (float)y / (float)(height - 1);
            for (int x = 0; x < width; x++) {
                float s = (float)x / (float)(width - 1);
                uint8_t r, g, b;
                hsv_to_rgb(ctrl->hue, s, v, &r, &g, &b);
                pixels[y * stride + x] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            }
        }
        cairo_surface_mark_dirty(ctrl->sv_cache);
        ctrl->sv_cache_hue = ctrl->hue;
        ctrl->sv_cache_w = width;
        ctrl->sv_cache_h = height;
    }

    cairo_set_source_surface(cr, ctrl->sv_cache, 0, 0);
    cairo_paint(cr);

    /* Selector circle */
    double cx = ctrl->sat * (width - 1);
    double cy = (1.0 - ctrl->val) * (height - 1);
    cairo_set_line_width(cr, 2.0);
    cairo_arc(cr, cx, cy, 6, 0, 2 * G_PI);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_stroke_preserve(cr);
    cairo_set_line_width(cr, 1.0);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_stroke(cr);
}

static void sv_update_from_pos(F87Controls *ctrl, double x, double y, int w, int h)
{
    ctrl->sat = CLAMP(x / (w - 1), 0.0f, 1.0f);
    ctrl->val = CLAMP(1.0f - y / (h - 1), 0.0f, 1.0f);
    sync_color_from_hsv(ctrl);
}

static void sv_pressed(GtkGestureClick *gesture, int n_press, double x, double y, gpointer data)
{
    (void)gesture; (void)n_press;
    F87Controls *ctrl = data;
    ctrl->sv_dragging = TRUE;
    int w = gtk_widget_get_width(GTK_WIDGET(ctrl->sv_area));
    int h = gtk_widget_get_height(GTK_WIDGET(ctrl->sv_area));
    sv_update_from_pos(ctrl, x, y, w, h);
}

static void sv_released(GtkGestureClick *gesture, int n_press, double x, double y, gpointer data)
{
    (void)gesture; (void)n_press; (void)x; (void)y;
    ((F87Controls *)data)->sv_dragging = FALSE;
}

static void sv_motion(GtkEventControllerMotion *motion, double x, double y, gpointer data)
{
    (void)motion;
    F87Controls *ctrl = data;
    if (!ctrl->sv_dragging) return;
    int w = gtk_widget_get_width(GTK_WIDGET(ctrl->sv_area));
    int h = gtk_widget_get_height(GTK_WIDGET(ctrl->sv_area));
    sv_update_from_pos(ctrl, x, y, w, h);
}

static void on_hue_changed(GtkRange *range, gpointer data)
{
    F87Controls *ctrl = data;
    ctrl->hue = (float)gtk_range_get_value(range);
    sync_color_from_hsv(ctrl);
}

static void on_hex_activate(GtkEntry *entry, gpointer data)
{
    F87Controls *ctrl = data;
    const char *text = gtk_editable_get_text(GTK_EDITABLE(entry));
    unsigned int r, g, b;
    if (sscanf(text, "%2x%2x%2x", &r, &g, &b) == 3) {
        ctrl->selected_color[0] = (uint8_t)r;
        ctrl->selected_color[1] = (uint8_t)g;
        ctrl->selected_color[2] = (uint8_t)b;
        rgb_to_hsv(r, g, b, &ctrl->hue, &ctrl->sat, &ctrl->val);
        if (ctrl->hue_scale)
            gtk_range_set_value(GTK_RANGE(ctrl->hue_scale), ctrl->hue);
        if (ctrl->sv_area)
            gtk_widget_queue_draw(GTK_WIDGET(ctrl->sv_area));
    }
}

static void on_preset_clicked(GtkButton *btn, gpointer data)
{
    F87Controls *ctrl = data;
    int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "color-idx"));
    ctrl->selected_color[0] = preset_colors[idx][0];
    ctrl->selected_color[1] = preset_colors[idx][1];
    ctrl->selected_color[2] = preset_colors[idx][2];
    rgb_to_hsv(preset_colors[idx][0], preset_colors[idx][1], preset_colors[idx][2],
               &ctrl->hue, &ctrl->sat, &ctrl->val);
    if (ctrl->hue_scale)
        gtk_range_set_value(GTK_RANGE(ctrl->hue_scale), ctrl->hue);
    sync_color_from_hsv(ctrl);
}

/* ===== SLIDER HELPER ===== */

static GtkWidget *create_slider(const char *label_text, double min, double max,
                                 double value, double step, GtkScale **out)
{
    GtkBox *box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4));
    GtkLabel *label = GTK_LABEL(gtk_label_new(label_text));
    gtk_widget_set_size_request(GTK_WIDGET(label), 70, -1);
    gtk_label_set_xalign(label, 0);
    gtk_widget_set_opacity(GTK_WIDGET(label), 0.7);
    gtk_box_append(box, GTK_WIDGET(label));

    GtkScale *scale = GTK_SCALE(gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
                                                          min, max, step));
    gtk_range_set_value(GTK_RANGE(scale), value);
    gtk_scale_set_draw_value(scale, TRUE);
    gtk_widget_set_hexpand(GTK_WIDGET(scale), TRUE);
    gtk_box_append(box, GTK_WIDGET(scale));

    gtk_widget_set_hexpand(GTK_WIDGET(box), TRUE);
    *out = scale;
    return GTK_WIDGET(box);
}

static void on_speed_changed(GtkRange *range, gpointer data)
{
    F87Controls *ctrl = data;
    if (ctrl->preview)
        f87_preview_set_speed(ctrl->preview, (uint8_t)gtk_range_get_value(range));
}

/* ===== PAINT CALLBACKS ===== */

static void on_key_painted(int key_id, gpointer user_data)
{
    F87Controls *ctrl = user_data;
    if (!ctrl->keyboard) return;
    f87_keyboard_view_set_key(ctrl->keyboard, key_id,
                               ctrl->selected_color[0],
                               ctrl->selected_color[1],
                               ctrl->selected_color[2]);
}

static void on_fill_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    F87Controls *ctrl = data;
    if (ctrl->keyboard)
        f87_keyboard_view_set_color(ctrl->keyboard,
                                     ctrl->selected_color[0],
                                     ctrl->selected_color[1],
                                     ctrl->selected_color[2]);
}

static void on_clear_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    F87Controls *ctrl = data;
    if (ctrl->keyboard)
        f87_keyboard_view_clear(ctrl->keyboard);
}

/* ===== PRESET CSS — loaded once ===== */

static gboolean preset_css_loaded = FALSE;

static void ensure_preset_css(void)
{
    if (preset_css_loaded) return;

    GString *pcss = g_string_new("");
    for (int i = 0; i < NUM_PRESETS; i++) {
        g_string_append_printf(pcss,
            ".preset-%d { background: #%02x%02x%02x; min-width: 14px; "
            "min-height: 14px; padding: 0; border-radius: 2px; }\n",
            i, preset_colors[i][0], preset_colors[i][1], preset_colors[i][2]);
    }
    GtkCssProvider *pprov = gtk_css_provider_new();
    gtk_css_provider_load_from_string(pprov, pcss->str);
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(), GTK_STYLE_PROVIDER(pprov),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(pprov);
    g_string_free(pcss, TRUE);

    preset_css_loaded = TRUE;
}

/* ===== COLOR PICKER (reusable) ===== */

static GtkWidget *create_color_picker(F87Controls *ctrl)
{
    GtkBox *right = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 4));

    /* SV gradient area */
    ctrl->sv_area = GTK_DRAWING_AREA(gtk_drawing_area_new());
    gtk_drawing_area_set_content_width(ctrl->sv_area, SV_SIZE);
    gtk_drawing_area_set_content_height(ctrl->sv_area, SV_SIZE);
    gtk_widget_set_size_request(GTK_WIDGET(ctrl->sv_area), SV_SIZE, SV_SIZE);
    gtk_drawing_area_set_draw_func(ctrl->sv_area, sv_draw, ctrl, NULL);
    gtk_widget_set_cursor_from_name(GTK_WIDGET(ctrl->sv_area), "crosshair");

    GtkGesture *click = gtk_gesture_click_new();
    g_signal_connect(click, "pressed", G_CALLBACK(sv_pressed), ctrl);
    g_signal_connect(click, "released", G_CALLBACK(sv_released), ctrl);
    gtk_widget_add_controller(GTK_WIDGET(ctrl->sv_area), GTK_EVENT_CONTROLLER(click));

    GtkEventController *motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion", G_CALLBACK(sv_motion), ctrl);
    gtk_widget_add_controller(GTK_WIDGET(ctrl->sv_area), motion);

    gtk_box_append(right, GTK_WIDGET(ctrl->sv_area));

    /* Hue slider */
    ctrl->hue_scale = GTK_SCALE(gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
                                                           0, 359, 1));
    gtk_range_set_value(GTK_RANGE(ctrl->hue_scale), ctrl->hue);
    gtk_scale_set_draw_value(ctrl->hue_scale, FALSE);
    gtk_widget_add_css_class(GTK_WIDGET(ctrl->hue_scale), "hue-slider");
    g_signal_connect(ctrl->hue_scale, "value-changed", G_CALLBACK(on_hue_changed), ctrl);
    gtk_box_append(right, GTK_WIDGET(ctrl->hue_scale));

    /* Hex input + preview swatch */
    GtkBox *hex_row = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4));
    GtkLabel *hash = GTK_LABEL(gtk_label_new("#"));
    gtk_widget_set_opacity(GTK_WIDGET(hash), 0.7);
    gtk_box_append(hex_row, GTK_WIDGET(hash));

    ctrl->hex_entry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_max_length(ctrl->hex_entry, 6);
    gtk_widget_set_size_request(GTK_WIDGET(ctrl->hex_entry), 70, -1);
    char hex[8];
    snprintf(hex, sizeof(hex), "%02X%02X%02X",
             ctrl->selected_color[0], ctrl->selected_color[1], ctrl->selected_color[2]);
    gtk_editable_set_text(GTK_EDITABLE(ctrl->hex_entry), hex);
    g_signal_connect(ctrl->hex_entry, "activate", G_CALLBACK(on_hex_activate), ctrl);
    gtk_box_append(hex_row, GTK_WIDGET(ctrl->hex_entry));

    ctrl->preview_swatch = gtk_drawing_area_new();
    gtk_widget_set_size_request(ctrl->preview_swatch, 24, 24);
    gtk_widget_add_css_class(ctrl->preview_swatch, "color-preview");
    gtk_box_append(hex_row, ctrl->preview_swatch);

    gtk_box_append(right, GTK_WIDGET(hex_row));

    /* Preset swatches */
    ensure_preset_css();
    GtkFlowBox *flow = GTK_FLOW_BOX(gtk_flow_box_new());
    gtk_flow_box_set_max_children_per_line(flow, 12);
    gtk_flow_box_set_selection_mode(flow, GTK_SELECTION_NONE);
    gtk_flow_box_set_row_spacing(flow, 1);
    gtk_flow_box_set_column_spacing(flow, 1);

    for (int i = 0; i < NUM_PRESETS; i++) {
        GtkButton *btn = GTK_BUTTON(gtk_button_new());
        char cls[32];
        snprintf(cls, sizeof(cls), "preset-%d", i);
        gtk_widget_add_css_class(GTK_WIDGET(btn), cls);
        g_object_set_data(G_OBJECT(btn), "color-idx", GINT_TO_POINTER(i));
        g_signal_connect(btn, "clicked", G_CALLBACK(on_preset_clicked), ctrl);
        gtk_flow_box_append(flow, GTK_WIDGET(btn));
    }
    gtk_box_append(right, GTK_WIDGET(flow));

    sync_color_from_hsv(ctrl);

    return GTK_WIDGET(right);
}

/* ===== SEND / STOP ===== */

static gboolean on_loading_done(gpointer data)
{
    F87Controls *ctrl = data;
    ctrl->loading_timer = 0;
    gtk_button_set_label(ctrl->send_button, _("Save"));
    gtk_widget_set_sensitive(GTK_WIDGET(ctrl->send_button), TRUE);
    gtk_widget_remove_css_class(GTK_WIDGET(ctrl->send_button), "loading");
    return G_SOURCE_REMOVE;
}

static void do_send(F87Controls *ctrl)
{
    uint8_t brightness = (uint8_t)gtk_range_get_value(GTK_RANGE(ctrl->brightness_scale));
    uint8_t speed = ctrl->speed_scale ?
                    (uint8_t)gtk_range_get_value(GTK_RANGE(ctrl->speed_scale)) : 2;

    const effect_meta_t *meta = effect_meta_lookup(ctrl->effect_id);
    int rc = -1;

    if (meta->flags & F87_PARAM_PAINT) {
        if (ctrl->keyboard) {
            const uint8_t (*colors)[3] = f87_keyboard_view_get_colors(ctrl->keyboard);
            rc = f87_app_state_apply_custom(ctrl->state, colors, 88);
        }
    } else if (strcmp(ctrl->category, "hw") == 0) {
        uint8_t colorful = ctrl->colorful_switch ?
                           gtk_switch_get_active(ctrl->colorful_switch) : 0;
        rc = f87_app_state_start_hw(ctrl->state, ctrl->effect_id,
                                     brightness, speed, colorful,
                                     ctrl->selected_color[0],
                                     ctrl->selected_color[1],
                                     ctrl->selected_color[2]);
    } else {
        f87_anim_config_t config = {0};
        config.color[0] = ctrl->selected_color[0];
        config.color[1] = ctrl->selected_color[1];
        config.color[2] = ctrl->selected_color[2];
        config.brightness = brightness;
        config.speed = speed;

        if (meta->flags & F87_PARAM_AUDIO) {
            guint src_idx = ctrl->source_dropdown ?
                            gtk_drop_down_get_selected(ctrl->source_dropdown) : 0;
            config.audio_source = (f87_audio_source_t)src_idx;

            if (ctrl->auto_gain_switch && gtk_switch_get_active(ctrl->auto_gain_switch))
                config.gain = 0;
            else if (ctrl->gain_scale)
                config.gain = (float)gtk_range_get_value(GTK_RANGE(ctrl->gain_scale));
        }

        if (meta->flags & F87_PARAM_PROFILE) {
            if (ctrl->sensor_profile_dropdown) {
                guint idx = gtk_drop_down_get_selected(ctrl->sensor_profile_dropdown);
                const char *profiles[] = {"developer", "gamer", "system"};
                if (idx < 3)
                    config.sensor_profile = profiles[idx];
            }
        }

        rc = f87_app_state_start_sw(ctrl->state, ctrl->effect_id, &config);
    }

    update_status(ctrl, ctrl->state->status_text);

    if (rc == 0) {
        gtk_button_set_label(ctrl->send_button, _("Saving..."));
        gtk_widget_set_sensitive(GTK_WIDGET(ctrl->send_button), FALSE);
        gtk_widget_add_css_class(GTK_WIDGET(ctrl->send_button), "loading");

        if (ctrl->loading_timer)
            g_source_remove(ctrl->loading_timer);
        ctrl->loading_timer = g_timeout_add(2000, on_loading_done, ctrl);
    }
}

static void on_send_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    do_send((F87Controls *)data);
}

static void do_stop(F87Controls *ctrl)
{
    f87_app_state_stop(ctrl->state);
    update_status(ctrl, ctrl->state->status_text);
}

static void on_stop_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    do_stop((F87Controls *)data);
}

/* ===== CLEAR + BUILD ===== */

static void clear_params(F87Controls *ctrl)
{
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(GTK_WIDGET(ctrl->params_box))))
        gtk_box_remove(ctrl->params_box, child);

    ctrl->speed_scale = NULL;
    ctrl->gain_scale = NULL;
    ctrl->auto_gain_switch = NULL;
    ctrl->colorful_switch = NULL;
    ctrl->source_dropdown = NULL;
    ctrl->sensor_profile_dropdown = NULL;
    ctrl->sv_area = NULL;
    ctrl->hue_scale = NULL;
    ctrl->hex_entry = NULL;
    ctrl->preview_swatch = NULL;

    /* Invalidate SV cache when picker is rebuilt */
    if (ctrl->sv_cache) {
        cairo_surface_destroy(ctrl->sv_cache);
        ctrl->sv_cache = NULL;
    }
}

static void build_controls_for_effect(F87Controls *ctrl)
{
    const effect_meta_t *meta = effect_meta_lookup(ctrl->effect_id);
    uint32_t flags = meta->flags;

    /* Title row */
    GtkBox *title_row = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6));
    GtkLabel *title_label = GTK_LABEL(gtk_label_new(ctrl->effect_name));
    gtk_label_set_xalign(title_label, 0);
    gtk_widget_add_css_class(GTK_WIDGET(title_label), "title-4");
    gtk_box_append(title_row, GTK_WIDGET(title_label));

    if (meta->tag) {
        GtkLabel *tag_label = GTK_LABEL(gtk_label_new(meta->tag));
        gtk_widget_set_opacity(GTK_WIDGET(tag_label), 0.4);
        gtk_widget_add_css_class(GTK_WIDGET(tag_label), "caption");
        gtk_widget_set_valign(GTK_WIDGET(tag_label), GTK_ALIGN_CENTER);
        gtk_box_append(title_row, GTK_WIDGET(tag_label));
    }

    if (f87_preview_is_reactive(ctrl->effect_id)) {
        GtkLabel *reactive_label = GTK_LABEL(gtk_label_new(_("key-press reactive")));
        gtk_widget_set_opacity(GTK_WIDGET(reactive_label), 0.5);
        gtk_widget_add_css_class(GTK_WIDGET(reactive_label), "caption");
        gtk_widget_set_valign(GTK_WIDGET(reactive_label), GTK_ALIGN_CENTER);
        gtk_box_append(title_row, GTK_WIDGET(reactive_label));
    }
    gtk_box_append(ctrl->params_box, GTK_WIDGET(title_row));

    /* No params for Off */
    if (flags == 0) return;

    /* Split layout: left params | divider | right color picker */
    GtkBox *split = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8));

    /* LEFT: sliders, switches, dropdowns */
    GtkBox *left = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 4));
    gtk_widget_set_hexpand(GTK_WIDGET(left), TRUE);

    if (flags & F87_PARAM_BRIGHTNESS)
        gtk_box_append(left, create_slider(_("Brightness"), 1, 4, 3, 1, &ctrl->brightness_scale));

    if (flags & F87_PARAM_SPEED) {
        gtk_box_append(left, create_slider(_("Speed"), 0, 4, 2, 1, &ctrl->speed_scale));
        g_signal_connect(ctrl->speed_scale, "value-changed", G_CALLBACK(on_speed_changed), ctrl);
    }

    if (flags & F87_PARAM_COLORFUL) {
        GtkBox *cf_box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4));
        GtkLabel *cf_label = GTK_LABEL(gtk_label_new(_("Colorful mode")));
        gtk_widget_set_size_request(GTK_WIDGET(cf_label), 70, -1);
        gtk_widget_set_opacity(GTK_WIDGET(cf_label), 0.7);
        gtk_box_append(cf_box, GTK_WIDGET(cf_label));
        ctrl->colorful_switch = GTK_SWITCH(gtk_switch_new());
        gtk_widget_set_margin_start(GTK_WIDGET(ctrl->colorful_switch), 4);
        gtk_box_append(cf_box, GTK_WIDGET(ctrl->colorful_switch));
        GtkLabel *cf_hint = GTK_LABEL(gtk_label_new(_("mixed colors")));
        gtk_widget_set_opacity(GTK_WIDGET(cf_hint), 0.35);
        gtk_widget_add_css_class(GTK_WIDGET(cf_hint), "caption");
        gtk_box_append(cf_box, GTK_WIDGET(cf_hint));
        gtk_box_append(left, GTK_WIDGET(cf_box));
    }

    if (flags & F87_PARAM_AUDIO) {
        GtkBox *src_box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4));
        GtkLabel *src_label = GTK_LABEL(gtk_label_new(_("Source")));
        gtk_widget_set_size_request(GTK_WIDGET(src_label), 70, -1);
        gtk_widget_set_opacity(GTK_WIDGET(src_label), 0.7);
        gtk_box_append(src_box, GTK_WIDGET(src_label));

        const char *sources[] = {N_("System Audio"), N_("Microphone"), NULL};
        /* gettext can't translate string arrays in-place for GtkDropDown,
           so we build a translated copy */
        const char *tr_sources[] = {_(sources[0]), _(sources[1]), NULL};
        ctrl->source_dropdown = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(tr_sources));
        gtk_box_append(src_box, GTK_WIDGET(ctrl->source_dropdown));
        gtk_box_append(left, GTK_WIDGET(src_box));

        GtkBox *gain_box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4));
        GtkLabel *gain_label = GTK_LABEL(gtk_label_new(_("Auto Gain")));
        gtk_widget_set_size_request(GTK_WIDGET(gain_label), 70, -1);
        gtk_widget_set_opacity(GTK_WIDGET(gain_label), 0.7);
        gtk_box_append(gain_box, GTK_WIDGET(gain_label));
        ctrl->auto_gain_switch = GTK_SWITCH(gtk_switch_new());
        gtk_switch_set_active(ctrl->auto_gain_switch, TRUE);
        gtk_box_append(gain_box, GTK_WIDGET(ctrl->auto_gain_switch));
        gtk_box_append(left, GTK_WIDGET(gain_box));

        gtk_box_append(left, create_slider(_("Gain"), 1, 10, 3, 1, &ctrl->gain_scale));
    }

    if (flags & F87_PARAM_PROFILE) {
        GtkBox *prof_box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4));
        GtkLabel *prof_label = GTK_LABEL(gtk_label_new(_("Profile")));
        gtk_widget_set_size_request(GTK_WIDGET(prof_label), 70, -1);
        gtk_widget_set_opacity(GTK_WIDGET(prof_label), 0.7);
        gtk_box_append(prof_box, GTK_WIDGET(prof_label));

        const char *profiles[] = {"Developer", "Gamer", "System", NULL};
        ctrl->sensor_profile_dropdown = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(profiles));
        gtk_box_append(prof_box, GTK_WIDGET(ctrl->sensor_profile_dropdown));
        gtk_box_append(left, GTK_WIDGET(prof_box));
    }

    if (flags & F87_PARAM_PAINT) {
        GtkLabel *hint = GTK_LABEL(gtk_label_new(_("Select color, paint by clicking keys")));
        gtk_label_set_xalign(hint, 0);
        gtk_widget_set_opacity(GTK_WIDGET(hint), 0.5);
        gtk_widget_add_css_class(GTK_WIDGET(hint), "caption");
        gtk_box_append(left, GTK_WIDGET(hint));

        GtkBox *paint_btns = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4));
        GtkButton *fill_btn = GTK_BUTTON(gtk_button_new_with_label(_("Fill All")));
        g_signal_connect(fill_btn, "clicked", G_CALLBACK(on_fill_clicked), ctrl);
        GtkButton *clear_btn = GTK_BUTTON(gtk_button_new_with_label(_("Clear")));
        g_signal_connect(clear_btn, "clicked", G_CALLBACK(on_clear_clicked), ctrl);
        gtk_box_append(paint_btns, GTK_WIDGET(fill_btn));
        gtk_box_append(paint_btns, GTK_WIDGET(clear_btn));
        gtk_box_append(left, GTK_WIDGET(paint_btns));
    }

    gtk_box_append(split, GTK_WIDGET(left));

    /* RIGHT: color picker (only if HAS_COLOR) */
    if (flags & F87_PARAM_COLOR) {
        /* Vertical divider */
        GtkSeparator *divider = GTK_SEPARATOR(gtk_separator_new(GTK_ORIENTATION_VERTICAL));
        gtk_box_append(split, GTK_WIDGET(divider));

        GtkWidget *picker = create_color_picker(ctrl);
        gtk_box_append(split, picker);
    }

    gtk_box_append(ctrl->params_box, GTK_WIDGET(split));

    /* Enable paint mode */
    if ((flags & F87_PARAM_PAINT) && ctrl->keyboard) {
        f87_keyboard_view_clear(ctrl->keyboard);
        f87_keyboard_view_set_paint_mode(ctrl->keyboard, TRUE, on_key_painted, ctrl);
    }
}

/* ===== PUBLIC API ===== */

void f87_controls_set_effect(F87Controls *ctrl, const char *category,
                              const char *effect_name, int effect_id)
{
    strncpy(ctrl->category, category, sizeof(ctrl->category) - 1);
    strncpy(ctrl->effect_name, effect_name, sizeof(ctrl->effect_name) - 1);
    ctrl->effect_id = effect_id;

    if (ctrl->keyboard)
        f87_keyboard_view_set_paint_mode(ctrl->keyboard, FALSE, NULL, NULL);

    clear_params(ctrl);
    build_controls_for_effect(ctrl);

    if (ctrl->stop_button) {
        gboolean show_stop = strcmp(category, "hw") != 0;
        gtk_widget_set_visible(GTK_WIDGET(ctrl->stop_button), show_stop);
    }

    gtk_button_set_label(ctrl->send_button, _("Save"));
    gtk_widget_set_sensitive(GTK_WIDGET(ctrl->send_button), TRUE);

    /* Start preview (not for paint mode) */
    if (ctrl->preview) {
        const effect_meta_t *meta = effect_meta_lookup(effect_id);
        if (!(meta->flags & F87_PARAM_PAINT)) {
            uint8_t spd = ctrl->speed_scale ?
                           (uint8_t)gtk_range_get_value(GTK_RANGE(ctrl->speed_scale)) : 2;
            f87_preview_start(ctrl->preview, effect_id, category, spd,
                               ctrl->selected_color[0],
                               ctrl->selected_color[1],
                               ctrl->selected_color[2]);
        }
    }
}

void f87_controls_set_keyboard(F87Controls *ctrl, F87KeyboardView *keyboard)
{
    ctrl->keyboard = keyboard;
    if (ctrl->preview)
        f87_preview_destroy(ctrl->preview);
    ctrl->preview = f87_preview_new(keyboard);
}

GtkWidget *f87_controls_get_widget(F87Controls *ctrl)
{
    return GTK_WIDGET(ctrl->container);
}

const uint8_t *f87_controls_get_color(F87Controls *ctrl)
{
    return ctrl->selected_color;
}

void f87_controls_send(F87Controls *ctrl)
{
    do_send(ctrl);
}

void f87_controls_stop(F87Controls *ctrl)
{
    do_stop(ctrl);
}

F87Controls *f87_controls_new(f87_app_state_t *state,
                               F87ControlsStatusCallback status_cb,
                               gpointer user_data)
{
    F87Controls *ctrl = g_new0(F87Controls, 1);
    ctrl->state = state;
    ctrl->status_cb = status_cb;
    ctrl->user_data = user_data;
    ctrl->selected_color[0] = 255;
    ctrl->selected_color[1] = 80;
    ctrl->selected_color[2] = 0;
    rgb_to_hsv(255, 80, 0, &ctrl->hue, &ctrl->sat, &ctrl->val);

    ctrl->container = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 4));
    gtk_widget_add_css_class(GTK_WIDGET(ctrl->container), "controls-panel");

    GtkScrolledWindow *scroll = GTK_SCROLLED_WINDOW(gtk_scrolled_window_new());
    gtk_scrolled_window_set_policy(scroll, GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(GTK_WIDGET(scroll), TRUE);

    ctrl->params_box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 4));
    GtkLabel *placeholder = GTK_LABEL(gtk_label_new(_("Select effect")));
    gtk_widget_set_opacity(GTK_WIDGET(placeholder), 0.5);
    gtk_box_append(ctrl->params_box, GTK_WIDGET(placeholder));

    gtk_scrolled_window_set_child(scroll, GTK_WIDGET(ctrl->params_box));
    gtk_box_append(ctrl->container, GTK_WIDGET(scroll));

    GtkBox *btn_box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8));
    gtk_widget_set_margin_top(GTK_WIDGET(btn_box), 4);

    ctrl->send_button = GTK_BUTTON(gtk_button_new_with_label(_("Save")));
    gtk_widget_add_css_class(GTK_WIDGET(ctrl->send_button), "action-button");
    gtk_widget_set_hexpand(GTK_WIDGET(ctrl->send_button), TRUE);
    g_signal_connect(ctrl->send_button, "clicked", G_CALLBACK(on_send_clicked), ctrl);
    gtk_box_append(btn_box, GTK_WIDGET(ctrl->send_button));

    ctrl->stop_button = GTK_BUTTON(gtk_button_new_with_label(_("Stop")));
    gtk_widget_add_css_class(GTK_WIDGET(ctrl->stop_button), "stop-button");
    g_signal_connect(ctrl->stop_button, "clicked", G_CALLBACK(on_stop_clicked), ctrl);
    gtk_box_append(btn_box, GTK_WIDGET(ctrl->stop_button));

    gtk_box_append(ctrl->container, GTK_WIDGET(btn_box));

    return ctrl;
}
```

- [ ] **Step 3: Build doğrula**

```bash
cd build && cmake .. -DBUILD_GUI=ON && make f87control 2>&1 | tail -10
```

Expected: Hatasız derleme.

- [ ] **Step 4: Commit**

```bash
git add gui/src/controls.h gui/src/controls.c
git commit -m "refactor(gui): metadata-driven control panel, single color picker, CSS fix, SV cache"
```

---

### Task 4: window.c — Rescan Butonu + Shortcut'lar + Renk Tutarlılığı + i18n

**Files:**
- Modify: `gui/src/window.c`

- [ ] **Step 1: `gui/src/window.c`'yi güncelle**

Tam dosya yeniden yazımı:

```c
#include "window.h"
#include "sidebar.h"
#include "app_state.h"
#include "controls.h"
#include "keyboard_view.h"
#include "effect_meta.h"
#include "i18n.h"
#include <stdio.h>

struct _F87Window {
    AdwApplicationWindow parent;
    GtkPaned *paned;
    GtkBox *sidebar_box;
    GtkBox *main_box;
    GtkLabel *status_label;
    GtkButton *rescan_button;
    f87_app_state_t app_state;
    F87Controls *controls;
    F87KeyboardView *keyboard;
    guint rescan_timer;
};

G_DEFINE_TYPE(F87Window, f87_window, ADW_TYPE_APPLICATION_WINDOW)

static void on_status_update(const char *text, gpointer user_data)
{
    F87Window *self = user_data;
    gtk_label_set_text(self->status_label, text);

    gtk_widget_remove_css_class(GTK_WIDGET(self->status_label), "status-error");
    gtk_widget_remove_css_class(GTK_WIDGET(self->status_label), "status-warn");
    gtk_widget_remove_css_class(GTK_WIDGET(self->status_label), "status-ok");

    if (self->app_state.status == F87_GUI_ERROR)
        gtk_widget_add_css_class(GTK_WIDGET(self->status_label), "status-error");
    else if (self->app_state.status == F87_GUI_RUNNING)
        gtk_widget_add_css_class(GTK_WIDGET(self->status_label), "status-ok");
    else if (self->app_state.status_level == F87_LOG_WARN)
        gtk_widget_add_css_class(GTK_WIDGET(self->status_label), "status-warn");
}

static void on_effect_selected(const char *category, const char *effect_name,
                                int effect_id, gpointer user_data)
{
    F87Window *self = user_data;
    f87_controls_set_effect(self->controls, category, effect_name, effect_id);

    /* Apply controls' selected color to keyboard preview for consistency */
    const effect_meta_t *meta = effect_meta_lookup(effect_id);
    const uint8_t *c = f87_controls_get_color(self->controls);

    if (meta->flags & F87_PARAM_COLOR) {
        f87_keyboard_view_set_color(self->keyboard, c[0], c[1], c[2]);
    } else if (meta->flags & F87_PARAM_PAINT) {
        /* Paint mode — keyboard cleared by controls */
    } else if (strcmp(category, "sensor") == 0) {
        f87_keyboard_view_clear(self->keyboard);
        for (int i = 1; i <= 4; i++)
            f87_keyboard_view_set_key(self->keyboard, i, 0, 200, 0);
        for (int i = 5; i <= 8; i++)
            f87_keyboard_view_set_key(self->keyboard, i, 200, 200, 0);
        for (int i = 9; i <= 12; i++)
            f87_keyboard_view_set_key(self->keyboard, i, 0, 100, 255);
    }

    char buf[256];
    snprintf(buf, sizeof(buf), _("Selected: %s"), effect_name);
    gtk_label_set_text(self->status_label, buf);
}

/* Rescan button */
static gboolean on_rescan_cooldown(gpointer data)
{
    F87Window *self = data;
    self->rescan_timer = 0;
    gtk_widget_set_sensitive(GTK_WIDGET(self->rescan_button), TRUE);
    return G_SOURCE_REMOVE;
}

static void on_rescan_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    F87Window *self = data;
    f87_app_state_rescan(&self->app_state);
    on_status_update(self->app_state.status_text, self);

    /* 2s cooldown to prevent USB rapid commands */
    gtk_widget_set_sensitive(GTK_WIDGET(self->rescan_button), FALSE);
    if (self->rescan_timer)
        g_source_remove(self->rescan_timer);
    self->rescan_timer = g_timeout_add(2000, on_rescan_cooldown, self);
}

/* Keyboard shortcuts */
static gboolean on_save_shortcut(GtkWidget *widget, GVariant *args, gpointer data)
{
    (void)widget; (void)args;
    F87Window *self = data;
    f87_controls_send(self->controls);
    return TRUE;
}

static gboolean on_stop_shortcut(GtkWidget *widget, GVariant *args, gpointer data)
{
    (void)widget; (void)args;
    F87Window *self = data;
    f87_controls_stop(self->controls);
    on_status_update(self->app_state.status_text, self);
    return TRUE;
}

static void f87_window_init(F87Window *self)
{
    AdwHeaderBar *header = ADW_HEADER_BAR(adw_header_bar_new());

    /* Rescan button in header */
    self->rescan_button = GTK_BUTTON(gtk_button_new_from_icon_name("view-refresh-symbolic"));
    gtk_widget_set_tooltip_text(GTK_WIDGET(self->rescan_button), _("Rescan for keyboard"));
    adw_header_bar_pack_end(header, GTK_WIDGET(self->rescan_button));
    g_signal_connect(self->rescan_button, "clicked", G_CALLBACK(on_rescan_clicked), self);

    AdwToolbarView *toolbar_view = ADW_TOOLBAR_VIEW(adw_toolbar_view_new());
    adw_toolbar_view_add_top_bar(toolbar_view, GTK_WIDGET(header));

    /* Main layout: horizontal paned (sidebar | main) */
    self->paned = GTK_PANED(gtk_paned_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_paned_set_position(self->paned, 220);
    gtk_paned_set_shrink_start_child(self->paned, FALSE);

    /* Sidebar */
    GtkScrolledWindow *sidebar_scroll = GTK_SCROLLED_WINDOW(gtk_scrolled_window_new());
    gtk_scrolled_window_set_policy(sidebar_scroll, GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    self->sidebar_box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
    gtk_widget_add_css_class(GTK_WIDGET(self->sidebar_box), "sidebar");

    GtkWidget *sidebar_content = f87_sidebar_create(on_effect_selected, self);
    gtk_box_append(self->sidebar_box, sidebar_content);

    gtk_scrolled_window_set_child(sidebar_scroll, GTK_WIDGET(self->sidebar_box));
    gtk_paned_set_start_child(self->paned, GTK_WIDGET(sidebar_scroll));

    /* Main area */
    GtkBox *right_box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 8));
    gtk_widget_set_margin_start(GTK_WIDGET(right_box), 12);
    gtk_widget_set_margin_end(GTK_WIDGET(right_box), 12);
    gtk_widget_set_margin_top(GTK_WIDGET(right_box), 12);
    gtk_widget_set_margin_bottom(GTK_WIDGET(right_box), 8);

    self->keyboard = f87_keyboard_view_new();
    gtk_box_append(right_box, GTK_WIDGET(self->keyboard));

    self->controls = f87_controls_new(&self->app_state, on_status_update, self);
    f87_controls_set_keyboard(self->controls, self->keyboard);
    gtk_box_append(right_box, f87_controls_get_widget(self->controls));

    /* Status bar */
    self->status_label = GTK_LABEL(gtk_label_new(_("Waiting")));
    gtk_widget_add_css_class(GTK_WIDGET(self->status_label), "status-bar");
    gtk_label_set_xalign(self->status_label, 0);
    gtk_box_append(right_box, GTK_WIDGET(self->status_label));

    self->main_box = right_box;
    gtk_paned_set_end_child(self->paned, GTK_WIDGET(right_box));

    adw_toolbar_view_set_content(toolbar_view, GTK_WIDGET(self->paned));
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(self),
                                       GTK_WIDGET(toolbar_view));

    /* Keyboard shortcuts */
    GtkShortcutController *sc = gtk_shortcut_controller_new();
    gtk_shortcut_controller_set_scope(sc, GTK_SHORTCUT_SCOPE_GLOBAL);
    gtk_shortcut_controller_add_shortcut(sc, gtk_shortcut_new(
        gtk_shortcut_trigger_parse_string("<Control>s"),
        gtk_callback_action_new(on_save_shortcut, self, NULL)));
    gtk_shortcut_controller_add_shortcut(sc, gtk_shortcut_new(
        gtk_shortcut_trigger_parse_string("Escape"),
        gtk_callback_action_new(on_stop_shortcut, self, NULL)));
    gtk_widget_add_controller(GTK_WIDGET(self), GTK_EVENT_CONTROLLER(sc));

    /* Initialize device connection */
    f87_app_state_init(&self->app_state);
    gtk_label_set_text(self->status_label, self->app_state.status_text);
    if (self->app_state.status == F87_GUI_ERROR)
        gtk_widget_add_css_class(GTK_WIDGET(self->status_label), "status-error");
}

static void f87_window_dispose(GObject *obj)
{
    F87Window *self = F87_WINDOW(obj);
    if (self->rescan_timer) {
        g_source_remove(self->rescan_timer);
        self->rescan_timer = 0;
    }
    f87_app_state_destroy(&self->app_state);
    G_OBJECT_CLASS(f87_window_parent_class)->dispose(obj);
}

static void f87_window_class_init(F87WindowClass *klass)
{
    G_OBJECT_CLASS(klass)->dispose = f87_window_dispose;
}

F87Window *f87_window_new(AdwApplication *app)
{
    return g_object_new(F87_TYPE_WINDOW,
                        "application", app,
                        "title", "F87Control",
                        "default-width", 1000,
                        "default-height", 550,
                        NULL);
}
```

- [ ] **Step 2: Build doğrula**

```bash
cd build && make f87control 2>&1 | tail -10
```

Expected: Hatasız derleme.

- [ ] **Step 3: Commit**

```bash
git add gui/src/window.c
git commit -m "feat(gui): rescan button, keyboard shortcuts, color consistency, i18n"
```

---

### Task 5: sidebar.c — Tooltip'ler + Kategori i18n

**Files:**
- Modify: `gui/src/sidebar.c`

- [ ] **Step 1: `gui/src/sidebar.c`'yi güncelle**

```c
#include "sidebar.h"
#include "i18n.h"

typedef struct {
    F87SidebarCallback callback;
    gpointer user_data;
} SidebarData;

typedef struct {
    const char *name;
    int id;
    const char *tooltip;
} EffectEntry;

static void on_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer data)
{
    (void)box;
    SidebarData *sd = data;
    if (!sd->callback) return;

    const char *category = g_object_get_data(G_OBJECT(row), "category");
    const char *name = g_object_get_data(G_OBJECT(row), "effect-name");
    int id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "effect-id"));

    if (name && category)
        sd->callback(category, name, id, sd->user_data);
}

static GtkListBoxRow *make_effect_row(const char *category, const char *name,
                                       int id, const char *tooltip)
{
    GtkListBoxRow *row = GTK_LIST_BOX_ROW(gtk_list_box_row_new());
    GtkLabel *label = GTK_LABEL(gtk_label_new(name));
    gtk_label_set_xalign(label, 0);
    gtk_list_box_row_set_child(row, GTK_WIDGET(label));

    g_object_set_data(G_OBJECT(row), "category", (gpointer)category);
    g_object_set_data(G_OBJECT(row), "effect-name", (gpointer)name);
    g_object_set_data(G_OBJECT(row), "effect-id", GINT_TO_POINTER(id));

    if (tooltip)
        gtk_widget_set_tooltip_text(GTK_WIDGET(row), _(tooltip));

    return row;
}

static GtkWidget *make_expander_category(const char *title, const char *category,
                                          const EffectEntry *entries, SidebarData *sd)
{
    GtkListBox *list = GTK_LIST_BOX(gtk_list_box_new());
    gtk_widget_add_css_class(GTK_WIDGET(list), "sidebar");
    g_signal_connect(list, "row-activated", G_CALLBACK(on_row_activated), sd);

    for (int i = 0; entries[i].name; i++)
        gtk_list_box_append(list, GTK_WIDGET(
            make_effect_row(category, entries[i].name, entries[i].id, entries[i].tooltip)));

    GtkExpander *expander = GTK_EXPANDER(gtk_expander_new(title));
    gtk_expander_set_expanded(expander, FALSE);
    gtk_expander_set_child(expander, GTK_WIDGET(list));
    gtk_widget_add_css_class(GTK_WIDGET(expander), "category-expander");

    return GTK_WIDGET(expander);
}

GtkWidget *f87_sidebar_create(F87SidebarCallback callback, gpointer user_data)
{
    SidebarData *sd = g_new0(SidebarData, 1);
    sd->callback = callback;
    sd->user_data = user_data;

    GtkBox *box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
    g_object_set_data_full(G_OBJECT(box), "sidebar-data", sd, g_free);

    static const EffectEntry hw[] = {
        {"Off",            0, N_("Turn off all LEDs")},
        {"Static",         1, N_("Single solid color")},
        {"Breathing",      2, N_("Pulsing color fade")},
        {"Wave",           3, N_("Rainbow wave across keyboard")},
        {"Spectrum",       4, N_("Color spread from key presses")},
        {"Rain",           5, N_("Falling rain drops")},
        {"Ripple",         7, N_("Ripple waves from key presses")},
        {"Starlight",      8, N_("Random twinkling stars")},
        {"Snake",         10, N_("Snake trail moving across keys")},
        {"Aurora",        11, N_("Northern lights shimmer")},
        {"Reactive",      12, N_("Single key lights up on press")},
        {"Marquee",       13, N_("Scrolling light marquee")},
        {"Circle",        15, N_("Circular rainbow pattern")},
        {"Rain Down",     16, N_("Top to bottom wave")},
        {"Center Ripple", 17, N_("Ripple from center outward")},
        {"Custom",        18, N_("Paint each key individually")},
        {NULL, 0, NULL}
    };
    gtk_box_append(box, make_expander_category(_("Hardware Effects"), "hw", hw, sd));

    static const EffectEntry sw[] = {
        {"Fire",       100, N_("Doom fire algorithm")},
        {"Matrix",     101, N_("Matrix digital rain")},
        {"Plasma",     102, N_("Colorful plasma waves")},
        {"Radar",      104, N_("Rotating radar sweep")},
        {"Lightning",  105, N_("Random lightning bolts")},
        {"Explode",    110, N_("Colorful explosions on key press")},
        {"Ripple SW",  111, N_("Software ripple waves on key press")},
        {"Typewriter", 112, N_("Heat trail on key press")},
        {"Life",       113, N_("Conway's Game of Life on key press")},
        {"KeyHeat",    114, N_("Cumulative key usage heatmap")},
        {NULL, 0, NULL}
    };
    gtk_box_append(box, make_expander_category(_("Software Effects"), "sw", sw, sd));

    static const EffectEntry mu[] = {
        {"Spectrum", 200, N_("Audio spectrum analyzer bars")},
        {"Beat",     201, N_("Flash on beat detection")},
        {"Energy",   202, N_("Expanding energy waves from audio")},
        {"VU Meter", 203, N_("Classic VU meter display")},
        {"FreqMap",  204, N_("Frequency band mapping")},
        {NULL, 0, NULL}
    };
    gtk_box_append(box, make_expander_category(_("Music"), "music", mu, sd));

    static const EffectEntry se[] = {
        {"developer", 106, N_("Developer sensor profile")},
        {"gamer",     107, N_("Gamer sensor profile")},
        {"system",    108, N_("System monitor profile")},
        {NULL, 0, NULL}
    };
    GtkListBox *se_list = GTK_LIST_BOX(gtk_list_box_new());
    gtk_widget_add_css_class(GTK_WIDGET(se_list), "sidebar");
    g_signal_connect(se_list, "row-activated", G_CALLBACK(on_row_activated), sd);
    for (int i = 0; se[i].name; i++) {
        GtkListBoxRow *row = make_effect_row("sensor", se[i].name, se[i].id, se[i].tooltip);
        g_object_set_data(G_OBJECT(row), "sensor-profile", (gpointer)se[i].name);
        gtk_list_box_append(se_list, GTK_WIDGET(row));
    }

    GtkExpander *se_expander = GTK_EXPANDER(gtk_expander_new(_("Sensor")));
    gtk_expander_set_expanded(se_expander, FALSE);
    gtk_expander_set_child(se_expander, GTK_WIDGET(se_list));
    gtk_widget_add_css_class(GTK_WIDGET(se_expander), "category-expander");
    gtk_box_append(box, GTK_WIDGET(se_expander));

    return GTK_WIDGET(box);
}
```

- [ ] **Step 2: Build doğrula**

```bash
cd build && make f87control 2>&1 | tail -5
```

- [ ] **Step 3: Commit**

```bash
git add gui/src/sidebar.c
git commit -m "feat(gui): sidebar effect tooltips and i18n category titles"
```

---

### Task 6: app_state.c — Status Stringleri i18n

**Files:**
- Modify: `gui/src/app_state.c`

- [ ] **Step 1: `gui/src/app_state.c`'deki tüm hardcoded Türkçe stringleri `_()` ile İngilizce'ye çevir**

Değiştirilecek stringler (her biri `snprintf` içinde):

| Satır | Eski | Yeni |
|-------|------|------|
| 10 | `"Baslatiliyor..."` | `_("Starting...")` |
| 15 | `"Daemon'a baglanilamadi"` | `_("Daemon connection failed")` |
| 23 | `"Daemon durumu alinamadi"` | `_("Could not get daemon status")` |
| 35 | `"Bagli (daemon)"` | `_("Connected (daemon)")` |
| 40 | `"Klavye bulunamadi"` | `_("Keyboard not found")` |
| 63 | `"Tarama basarisiz"` | `_("Scan failed")` |
| 72 | `"Bagli (daemon)"` | `_("Connected (daemon)")` |
| 78 | `"Klavye bulunamadi"` | `_("Keyboard not found")` |
| 111 | `"Klavye baglantisi koptu — yeniden baglanamadi"` | `_("Connection lost — could not reconnect")` |
| 120 | `"%s calisiyor"` | `_("%s running")` |
| 180 | `"%s calisiyor"` | `_("%s running")` |
| 194 | `"Durdurma hatasi"` | `_("Stop failed")` |
| 200 | `"Bekleniyor"` | `_("Waiting")` |
| 217 | `"Per-key renkler gonderilemedi"` | `_("Per-key colors failed")` |
| 225 | `"Custom calisiyor"` | `_("Custom running")` |

Dosyanın başına ekle:
```c
#include "i18n.h"
```

Her `snprintf` içindeki string'i tablodaki gibi değiştir. Örnek:

```c
/* Önce: */
snprintf(state->status_text, sizeof(state->status_text), "Baslatiliyor...");
/* Sonra: */
snprintf(state->status_text, sizeof(state->status_text), "%s", _("Starting..."));
```

Not: `_()` dönüşü `const char*` olduğu için `%s` format specifier kullan.

- [ ] **Step 2: Build doğrula**

```bash
cd build && make f87control 2>&1 | tail -5
```

- [ ] **Step 3: Commit**

```bash
git add gui/src/app_state.c
git commit -m "feat(gui): i18n for all app_state status messages"
```

---

### Task 7: keyboard_view.c — Responsive + Drag-to-Paint

**Files:**
- Modify: `gui/src/keyboard_view.c`

- [ ] **Step 1: Struct'a drag-to-paint alanları ekle**

`struct _F87KeyboardView` içine:

```c
struct _F87KeyboardView {
    GtkDrawingArea parent;
    uint8_t colors[F87_KEY_COUNT][3];

    /* Paint mode */
    gboolean paint_mode;
    F87KeyPaintCallback paint_cb;
    gpointer paint_data;
    gboolean paint_dragging;
    int last_painted_key;
};
```

- [ ] **Step 2: Init fonksiyonunu güncelle — responsive + motion handler**

```c
static void on_paint_motion(GtkEventControllerMotion *motion,
                             double x, double y, gpointer data)
{
    (void)motion;
    F87KeyboardView *self = data;
    if (!self->paint_mode || !self->paint_dragging) return;

    int w = gtk_widget_get_width(GTK_WIDGET(self));
    int h = gtk_widget_get_height(GTK_WIDGET(self));
    int key_id = hit_test((int)x, (int)y, w, h);
    if (key_id >= 0 && key_id != self->last_painted_key) {
        self->last_painted_key = key_id;
        if (self->paint_cb)
            self->paint_cb(key_id, self->paint_data);
    }
}
```

`on_key_clicked` fonksiyonunu güncelle — drag başlangıcı:

```c
static void on_key_pressed(GtkGestureClick *gesture, int n_press, double x, double y,
                            gpointer user_data)
{
    (void)gesture; (void)n_press;
    F87KeyboardView *self = user_data;
    if (!self->paint_mode) return;

    self->paint_dragging = TRUE;
    self->last_painted_key = -1;

    int w = gtk_widget_get_width(GTK_WIDGET(self));
    int h = gtk_widget_get_height(GTK_WIDGET(self));
    int key_id = hit_test((int)x, (int)y, w, h);
    if (key_id >= 0) {
        self->last_painted_key = key_id;
        if (self->paint_cb)
            self->paint_cb(key_id, self->paint_data);
    }
}

static void on_key_released(GtkGestureClick *gesture, int n_press, double x, double y,
                             gpointer user_data)
{
    (void)gesture; (void)n_press; (void)x; (void)y;
    F87KeyboardView *self = user_data;
    self->paint_dragging = FALSE;
    self->last_painted_key = -1;
}
```

`f87_keyboard_view_init` içinde:

```c
static void f87_keyboard_view_init(F87KeyboardView *self)
{
    memset(self->colors, 0, sizeof(self->colors));
    self->last_painted_key = -1;
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(self), draw_func, NULL, NULL);

    /* Responsive: expand horizontally, keep aspect ratio via content size */
    gtk_widget_set_size_request(GTK_WIDGET(self), 500, -1);
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(self), 690);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(self), 255);
    gtk_widget_set_halign(GTK_WIDGET(self), GTK_ALIGN_FILL);
    gtk_widget_set_valign(GTK_WIDGET(self), GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(GTK_WIDGET(self), TRUE);
    gtk_widget_set_vexpand(GTK_WIDGET(self), FALSE);

    /* Click handler for paint mode */
    GtkGesture *click = gtk_gesture_click_new();
    g_signal_connect(click, "pressed", G_CALLBACK(on_key_pressed), self);
    g_signal_connect(click, "released", G_CALLBACK(on_key_released), self);
    gtk_widget_add_controller(GTK_WIDGET(self), GTK_EVENT_CONTROLLER(click));

    /* Motion handler for drag-to-paint */
    GtkEventController *motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion", G_CALLBACK(on_paint_motion), self);
    gtk_widget_add_controller(GTK_WIDGET(self), motion);
}
```

- [ ] **Step 3: Build doğrula**

```bash
cd build && make f87control 2>&1 | tail -5
```

- [ ] **Step 4: Commit**

```bash
git add gui/src/keyboard_view.c
git commit -m "feat(gui): responsive keyboard view and drag-to-paint"
```

---

### Task 8: Türkçe Çeviri Dosyası (.po) + CMake mo Derleme

**Files:**
- Create: `po/tr.po`
- Modify: `gui/CMakeLists.txt`

- [ ] **Step 1: pot dosyası üret**

```bash
cd /home/emrah/Projects/F87Control && xgettext --keyword=_ --keyword=N_ --from-code=UTF-8 --add-comments -o po/f87control.pot gui/src/*.c 2>&1
```

- [ ] **Step 2: `po/tr.po` oluştur — pot'tan kopyala ve çevirileri doldur**

```bash
cd /home/emrah/Projects/F87Control && msginit --input=po/f87control.pot --locale=tr_TR.UTF-8 --output=po/tr.po --no-translator 2>&1 || cp po/f87control.pot po/tr.po
```

Sonra `po/tr.po` dosyasındaki `msgstr` alanlarını Türkçe çevirilerle doldur. Her `msgid`/`msgstr` çifti:

```po
msgid "Brightness"
msgstr "Parlaklık"

msgid "Speed"
msgstr "Hız"

msgid "Colorful mode"
msgstr "Renkli mod"

msgid "mixed colors"
msgstr "karışık renkler"

msgid "Source"
msgstr "Kaynak"

msgid "System Audio"
msgstr "Sistem Sesi"

msgid "Microphone"
msgstr "Mikrofon"

msgid "Auto Gain"
msgstr "Otomatik Kazanç"

msgid "Gain"
msgstr "Kazanç"

msgid "Profile"
msgstr "Profil"

msgid "Save"
msgstr "Kaydet"

msgid "Stop"
msgstr "Durdur"

msgid "Saving..."
msgstr "Kaydediliyor..."

msgid "Waiting"
msgstr "Bekleniyor"

msgid "Starting..."
msgstr "Başlatılıyor..."

msgid "Connected (daemon)"
msgstr "Bağlı (daemon)"

msgid "Keyboard not found"
msgstr "Klavye bulunamadı"

msgid "Scan failed"
msgstr "Tarama başarısız"

msgid "Connection lost — could not reconnect"
msgstr "Klavye bağlantısı koptu — yeniden bağlanamadı"

msgid "Stop failed"
msgstr "Durdurma hatası"

msgid "Per-key colors failed"
msgstr "Per-key renkler gönderilemedi"

msgid "Custom running"
msgstr "Custom çalışıyor"

msgid "%s running"
msgstr "%s çalışıyor"

msgid "Selected: %s"
msgstr "Seçilen: %s"

msgid "Select effect"
msgstr "Efekt seçiniz"

msgid "Select color, paint by clicking keys"
msgstr "Renk seç, tuşlarını tıklayarak boya"

msgid "key-press reactive"
msgstr "tuş basmaya duyarlı"

msgid "Fill All"
msgstr "Tümünü Boya"

msgid "Clear"
msgstr "Temizle"

msgid "Rescan for keyboard"
msgstr "Klavyeyi yeniden tara"

msgid "Daemon connection failed"
msgstr "Daemon'a bağlanılamadı"

msgid "Could not get daemon status"
msgstr "Daemon durumu alınamadı"

msgid "Hardware Effects"
msgstr "Donanım Efektleri"

msgid "Software Effects"
msgstr "Yazılımsal Efektler"

msgid "Music"
msgstr "Müzik"

msgid "Sensor"
msgstr "Sensör"

msgid "Turn off all LEDs"
msgstr "Tüm LED'leri kapat"

msgid "Single solid color"
msgstr "Tek düz renk"

msgid "Pulsing color fade"
msgstr "Nefes alan renk"

msgid "Rainbow wave across keyboard"
msgstr "Klavye boyunca gökkuşağı dalgası"

msgid "Color spread from key presses"
msgstr "Tuş basımından renk yayılması"

msgid "Falling rain drops"
msgstr "Düşen yağmur damlaları"

msgid "Ripple waves from key presses"
msgstr "Tuş basımından dalga"

msgid "Random twinkling stars"
msgstr "Rastgele parlayan yıldızlar"

msgid "Snake trail moving across keys"
msgstr "Tuşlar üzerinde yılan izi"

msgid "Northern lights shimmer"
msgstr "Kuzey ışıkları parıltısı"

msgid "Single key lights up on press"
msgstr "Basılan tuş yanar"

msgid "Scrolling light marquee"
msgstr "Kayan ışık şeridi"

msgid "Circular rainbow pattern"
msgstr "Dairesel gökkuşağı deseni"

msgid "Top to bottom wave"
msgstr "Yukarıdan aşağı dalga"

msgid "Ripple from center outward"
msgstr "Merkezden dışa dalga"

msgid "Paint each key individually"
msgstr "Her tuşu ayrı ayrı boya"

msgid "Doom fire algorithm"
msgstr "Doom ateş algoritması"

msgid "Matrix digital rain"
msgstr "Matrix dijital yağmur"

msgid "Colorful plasma waves"
msgstr "Renkli plazma dalgaları"

msgid "Rotating radar sweep"
msgstr "Dönen radar taraması"

msgid "Random lightning bolts"
msgstr "Rastgele şimşek çakması"

msgid "Colorful explosions on key press"
msgstr "Tuş basımında renkli patlama"

msgid "Software ripple waves on key press"
msgstr "Tuş basımında yazılımsal dalga"

msgid "Heat trail on key press"
msgstr "Tuş basımında ısı izi"

msgid "Conway's Game of Life on key press"
msgstr "Tuş basımıyla Conway'in Yaşam Oyunu"

msgid "Cumulative key usage heatmap"
msgstr "Kümülatif tuş kullanımı ısı haritası"

msgid "Audio spectrum analyzer bars"
msgstr "Ses spektrum analiz çubukları"

msgid "Flash on beat detection"
msgstr "Ritim algılamada flaş"

msgid "Expanding energy waves from audio"
msgstr "Sesten yayılan enerji dalgaları"

msgid "Classic VU meter display"
msgstr "Klasik VU metre göstergesi"

msgid "Frequency band mapping"
msgstr "Frekans bandı haritalama"

msgid "Developer sensor profile"
msgstr "Geliştirici sensör profili"

msgid "Gamer sensor profile"
msgstr "Oyuncu sensör profili"

msgid "System monitor profile"
msgstr "Sistem izleme profili"
```

- [ ] **Step 3: CMake'e mo derleme kuralı ekle**

`gui/CMakeLists.txt`'nin sonuna:

```cmake
# Gettext .po → .mo compilation
find_program(MSGFMT msgfmt)
if(MSGFMT)
    set(LANGUAGES tr)
    foreach(LANG ${LANGUAGES})
        set(PO_FILE ${CMAKE_SOURCE_DIR}/po/${LANG}.po)
        set(MO_DIR ${CMAKE_BINARY_DIR}/locale/${LANG}/LC_MESSAGES)
        set(MO_FILE ${MO_DIR}/f87control.mo)
        file(MAKE_DIRECTORY ${MO_DIR})
        add_custom_command(
            OUTPUT ${MO_FILE}
            COMMAND ${MSGFMT} -o ${MO_FILE} ${PO_FILE}
            DEPENDS ${PO_FILE}
            COMMENT "Compiling ${LANG} translation"
        )
        list(APPEND MO_FILES ${MO_FILE})
        install(FILES ${MO_FILE}
                DESTINATION ${CMAKE_INSTALL_DATADIR}/locale/${LANG}/LC_MESSAGES)
    endforeach()
    add_custom_target(translations ALL DEPENDS ${MO_FILES})
endif()
```

- [ ] **Step 4: Build doğrula — mo dosyası üretilmeli**

```bash
cd build && cmake .. -DBUILD_GUI=ON && make 2>&1 | grep -i "translation\|msgfmt\|locale"
```

Expected: `Compiling tr translation` mesajı ve `build/locale/tr/LC_MESSAGES/f87control.mo` dosyası.

- [ ] **Step 5: Türkçe çeviriyi test et**

```bash
ls build/locale/tr/LC_MESSAGES/f87control.mo && echo "OK"
```

- [ ] **Step 6: Commit**

```bash
git add po/f87control.pot po/tr.po po/POTFILES.in gui/CMakeLists.txt
git commit -m "feat(gui): Turkish translation and gettext mo compilation"
```

---

### Task 9: Final Build + Tüm Testlerin Geçtiğini Doğrula

**Files:** Hiçbir dosya değişikliği — sadece doğrulama.

- [ ] **Step 1: Temiz build**

```bash
cd /home/emrah/Projects/F87Control && rm -rf build && mkdir build && cd build && cmake .. -DBUILD_GUI=ON -DBUILD_DAEMON=ON && make -j$(nproc) 2>&1 | tail -20
```

Expected: Hatasız derleme, `f87control`, `f87d`, `f87ctl` binary'leri üretilir.

- [ ] **Step 2: Mevcut testleri çalıştır**

```bash
cd /home/emrah/Projects/F87Control/build && ctest --output-on-failure 2>&1
```

Expected: Tüm testler PASS. (GUI testleri yok — sadece lib/daemon testleri)

- [ ] **Step 3: mo dosyası doğrula**

```bash
ls -la /home/emrah/Projects/F87Control/build/locale/tr/LC_MESSAGES/f87control.mo
msgunfmt /home/emrah/Projects/F87Control/build/locale/tr/LC_MESSAGES/f87control.mo | head -20
```

Expected: `.mo` dosyası var ve çeviriler okunabiliyor.

- [ ] **Step 4: Commit (eğer düzeltme yapıldıysa)**

Sadece önceki adımlarda sorun çıkıp düzeltme yapıldıysa commit.
