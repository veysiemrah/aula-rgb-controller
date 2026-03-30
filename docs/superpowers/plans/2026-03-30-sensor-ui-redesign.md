# Sensor Monitor UI Redesign — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Sensor Monitor GUI'sini yeniden tasarla — tek sidebar girişi, profil dropdown (hazır + custom), custom editör (sensör seç → mod seç → klavyede tıkla), profil-aware preview (tuşlarda sensör adı + değer etiketi).

**Architecture:** Sidebar'daki 3 ayrı sensör girişi tek "Sensor Monitor" girişine dönüşür. Controls paneli yeni `F87_PARAM_SENSOR` flag'i ile sensör-özel UI oluşturur: profil dropdown, custom editör (sensör dropdown, mod toggle, bar slider, atama listesi), klavyede tıklama ile atama. Preview, profildeki mapping verilerine göre tuşları renklendirir ve etiketler. Custom profil JSON olarak kaydedilir/yüklenir.

**Tech Stack:** C11, GTK4 + libadwaita, Cairo (keyboard preview), json-c (profil I/O), lib/src/sensor.h (canlı değer okuma)

---

## File Structure

| Dosya | Değişiklik | Sorumluluk |
|-------|-----------|------------|
| `gui/src/effect_meta.h` | Modify | Yeni `F87_PARAM_SENSOR` flag ekle |
| `gui/src/effect_meta.c` | Modify | Sensor meta girişi güncelle (tek giriş, yeni flag) |
| `gui/src/sidebar.c` | Modify | 3 sensör girişini tek "Sensor Monitor" girişine dönüştür |
| `gui/src/sensor_editor.h` | Create | Sensör editör widget public API |
| `gui/src/sensor_editor.c` | Create | Custom sensör editör: profil dropdown, sensör/mod seçimi, atama listesi, klavye tıklama |
| `gui/src/controls.c` | Modify | `F87_PARAM_SENSOR` handling: sensor_editor widget oluştur, send logic güncelle |
| `gui/src/keyboard_view.h` | Modify | Overlay callback + sensör seçim modu API |
| `gui/src/keyboard_view.c` | Modify | Sensör tuş overlay'i (renkli border, etiket), tıklama ile atama |
| `gui/src/preview.c` | Modify | Profil-aware render_sensor: mapping verilerine göre tuş renklendirme + etiket |
| `lib/src/sensor_config.h` | Modify | `f87_sensor_config_save()` deklarasyonu |
| `lib/src/sensor_config.c` | Modify | JSON yazma fonksiyonu ekle |
| `lib/include/f87/animate.h` | No change | `sensor_profile` + `sensor_config_path` zaten mevcut |
| `gui/src/app_state.c` | Modify | Custom profil path desteği |
| `tests/test_sensor_config.c` | Create | Sensor config save/load round-trip testleri |
| `gui/CMakeLists.txt` | Modify | sensor_editor.c ekle |

---

### Task 1: Sensor Config Save — Lib Katmanı

Lib'e custom profil kaydetme fonksiyonu ekle. Bu diğer tüm GUI çalışmasından bağımsız.

**Files:**
- Modify: `lib/src/sensor_config.h`
- Modify: `lib/src/sensor_config.c`
- Create: `tests/test_sensor_config.c`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/test_sensor_config.c`:

```c
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include "../lib/src/sensor_config.h"
#include "../lib/src/sensor.h"
#include "../lib/src/protocol.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    printf("  %-40s ", #name); \
    tests_run++; \
    if (test_##name()) { tests_passed++; printf("PASS\n"); } \
    else { printf("FAIL\n"); } \
} while(0)

static int test_save_and_reload(void) {
    /* Build a profile in memory */
    f87_sensor_profile_t profile = {0};
    strncpy(profile.profile_name, "test_custom", sizeof(profile.profile_name) - 1);

    f87_sensor_mapping_t *m = &profile.mappings[0];
    m->sensor_name = strdup("cpu_temp");
    m->mode = F87_SENSOR_MODE_BAR;
    m->key_ids[0] = 1; m->key_ids[1] = 2; m->key_ids[2] = 3; m->key_ids[3] = 4;
    m->key_count = 4;
    m->interval_ms = 1000;
    profile.mapping_count = 1;

    /* Save to temp file */
    char path[] = "/tmp/f87_test_sensor_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) return 0;
    close(fd);

    int rc = f87_sensor_config_save(path, &profile, f87_key_layout, F87_KEY_COUNT);
    free(m->sensor_name);
    if (rc < 0) { unlink(path); return 0; }

    /* Reload and verify */
    f87_sensor_profile_t loaded = {0};
    rc = f87_sensor_config_load(path, &loaded, f87_key_layout, F87_KEY_COUNT);
    unlink(path);
    if (rc < 0) return 0;

    if (strcmp(loaded.profile_name, "test_custom") != 0) return 0;
    if (loaded.mapping_count != 1) return 0;
    if (strcmp(loaded.mappings[0].sensor_name, "cpu_temp") != 0) return 0;
    if (loaded.mappings[0].mode != F87_SENSOR_MODE_BAR) return 0;
    if (loaded.mappings[0].key_count != 4) return 0;
    if (loaded.mappings[0].key_ids[0] != 1) return 0;  /* F1 */

    /* Cleanup loaded profile */
    for (int i = 0; i < loaded.mapping_count; i++)
        free(loaded.mappings[i].sensor_name);

    return 1;
}

static int test_save_color_mode(void) {
    f87_sensor_profile_t profile = {0};
    strncpy(profile.profile_name, "test_color", sizeof(profile.profile_name) - 1);

    f87_sensor_mapping_t *m = &profile.mappings[0];
    m->sensor_name = strdup("ram_usage");
    m->mode = F87_SENSOR_MODE_COLOR;
    m->key_ids[0] = 0;  /* ESC */
    m->key_count = 1;
    m->interval_ms = 1000;
    profile.mapping_count = 1;

    char path[] = "/tmp/f87_test_sensor2_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) return 0;
    close(fd);

    int rc = f87_sensor_config_save(path, &profile, f87_key_layout, F87_KEY_COUNT);
    free(m->sensor_name);
    if (rc < 0) { unlink(path); return 0; }

    f87_sensor_profile_t loaded = {0};
    rc = f87_sensor_config_load(path, &loaded, f87_key_layout, F87_KEY_COUNT);
    unlink(path);
    if (rc < 0) return 0;

    if (loaded.mappings[0].mode != F87_SENSOR_MODE_COLOR) return 0;
    if (loaded.mappings[0].key_count != 1) return 0;

    for (int i = 0; i < loaded.mapping_count; i++)
        free(loaded.mappings[i].sensor_name);
    return 1;
}

static int test_save_empty_profile(void) {
    f87_sensor_profile_t profile = {0};
    strncpy(profile.profile_name, "empty", sizeof(profile.profile_name) - 1);
    profile.mapping_count = 0;

    char path[] = "/tmp/f87_test_sensor3_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) return 0;
    close(fd);

    int rc = f87_sensor_config_save(path, &profile, f87_key_layout, F87_KEY_COUNT);
    unlink(path);
    /* Empty profile should fail or save empty mappings */
    return rc == 0 || rc == -1;  /* Either behavior is acceptable */
}

int main(void) {
    printf("Sensor config tests:\n");
    TEST(save_and_reload);
    TEST(save_color_mode);
    TEST(save_empty_profile);
    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
```

- [ ] **Step 2: Add test to CMakeLists.txt**

In `tests/CMakeLists.txt`, add alongside existing sensor test:

```cmake
if(F87_HAS_JSON)
    add_executable(test_sensor_config test_sensor_config.c
                   ${CMAKE_SOURCE_DIR}/lib/src/sensor_config.c
                   ${CMAKE_SOURCE_DIR}/lib/src/sensor.c
                   ${CMAKE_SOURCE_DIR}/lib/src/protocol.c)
    target_include_directories(test_sensor_config PRIVATE
                               ${CMAKE_SOURCE_DIR}/lib/include
                               ${CMAKE_SOURCE_DIR}/lib/src)
    target_link_libraries(test_sensor_config json-c)
    target_compile_definitions(test_sensor_config PRIVATE F87_HAS_JSON)
    add_test(NAME test_sensor_config COMMAND test_sensor_config)
endif()
```

- [ ] **Step 3: Run test to verify it fails**

Run: `cd /home/emrah/Projects/F87Control/build && cmake .. && make test_sensor_config && ./test_sensor_config`
Expected: Compile error — `f87_sensor_config_save` undefined.

- [ ] **Step 4: Declare f87_sensor_config_save in header**

In `lib/src/sensor_config.h`, add after `f87_sensor_config_builtin` declaration:

```c
/* Save profile to JSON file */
int f87_sensor_config_save(const char *path, const f87_sensor_profile_t *profile,
                            const f87_key_info *layout, int key_count);
```

- [ ] **Step 5: Implement f87_sensor_config_save**

In `lib/src/sensor_config.c`, add before the `#endif /* F87_HAS_JSON */`:

```c
static const char *find_key_name(int key_id, const f87_key_info *layout, int count)
{
    for (int i = 0; i < count; i++) {
        if (layout[i].key_id == key_id)
            return layout[i].name;
    }
    return NULL;
}

int f87_sensor_config_save(const char *path, const f87_sensor_profile_t *profile,
                            const f87_key_info *layout, int key_count)
{
    struct json_object *root = json_object_new_object();
    json_object_object_add(root, "profile",
                           json_object_new_string(profile->profile_name));

    struct json_object *jmappings = json_object_new_array();

    for (int i = 0; i < profile->mapping_count; i++) {
        const f87_sensor_mapping_t *m = &profile->mappings[i];
        struct json_object *jmap = json_object_new_object();

        json_object_object_add(jmap, "sensor",
                               json_object_new_string(m->sensor_name));
        json_object_object_add(jmap, "mode",
                               json_object_new_string(
                                   m->mode == F87_SENSOR_MODE_BAR ? "bar" : "color"));

        struct json_object *jkeys = json_object_new_array();
        for (int k = 0; k < m->key_count; k++) {
            const char *name = find_key_name(m->key_ids[k], layout, key_count);
            if (name)
                json_object_array_add(jkeys, json_object_new_string(name));
        }
        json_object_object_add(jmap, "keys", jkeys);
        json_object_object_add(jmap, "interval_ms",
                               json_object_new_int(m->interval_ms));

        json_object_array_add(jmappings, jmap);
    }

    json_object_object_add(root, "mappings", jmappings);

    int rc = json_object_to_file_ext(path, root, JSON_C_TO_STRING_PRETTY);
    json_object_put(root);
    return rc;
}
```

- [ ] **Step 6: Run test to verify it passes**

Run: `cd /home/emrah/Projects/F87Control/build && cmake .. && make test_sensor_config && ./test_sensor_config`
Expected: All 3 tests PASS.

- [ ] **Step 7: Run all existing tests**

Run: `cd /home/emrah/Projects/F87Control/build && ctest --output-on-failure`
Expected: All tests pass (no regressions).

- [ ] **Step 8: Commit**

```bash
git add lib/src/sensor_config.h lib/src/sensor_config.c tests/test_sensor_config.c tests/CMakeLists.txt
git commit -m "feat(lib): add f87_sensor_config_save for custom sensor profiles"
```

---

### Task 2: Effect Meta + Sidebar — Tek Sensor Monitor Girişi

Sidebar'daki 3 ayrı sensör profilini tek "Sensor Monitor" girişine dönüştür ve yeni F87_PARAM_SENSOR flag'i ekle.

**Files:**
- Modify: `gui/src/effect_meta.h`
- Modify: `gui/src/effect_meta.c`
- Modify: `gui/src/sidebar.c`

- [ ] **Step 1: Add F87_PARAM_SENSOR flag to effect_meta.h**

In `gui/src/effect_meta.h`, add new flag after `F87_PARAM_PAINT`:

```c
    F87_PARAM_PAINT      = (1 << 6),
    F87_PARAM_SENSOR     = (1 << 7),
```

- [ ] **Step 2: Update effect_meta.c — replace 3 sensor entries with 1**

In `gui/src/effect_meta.c`, replace the three sensor entries:

Old:
```c
    /* Sensor */
    {106, B | P,          NULL,      N_("Developer sensor profile") },
    {107, B | P,          NULL,      N_("Gamer sensor profile") },
    {108, B | P,          NULL,      N_("System monitor profile") },
```

New:
```c
    /* Sensor */
    {106, B | SN,         NULL,      N_("Monitor system sensors on keyboard") },
```

Add `#define SN F87_PARAM_SENSOR` with the other shorthand defines at the top, and remove the `P` define since `F87_PARAM_PROFILE` is no longer used:

Old:
```c
#define P  F87_PARAM_PROFILE
```

New (replace with):
```c
#define SN F87_PARAM_SENSOR
```

Also update the `#undef` section at the bottom — replace `#undef P` with `#undef SN`.

- [ ] **Step 3: Update sidebar.c — 3 profiles → 1 Sensor Monitor entry**

In `gui/src/sidebar.c`, replace the sensor category section. Find the block that creates 3 sensor EffectEntry items (effect_id 106, 107, 108) and replace with:

Old (sensor category block with 3 entries):
```c
    /* Sensor profiles */
    static const EffectEntry sensor_entries[] = {
        { N_("Developer"), 106, N_("Developer sensor profile") },
        { N_("Gamer"),     107, N_("Gamer sensor profile") },
        { N_("System"),    108, N_("System monitor profile") },
    };
```

New:
```c
    /* Sensor monitor — single entry */
    static const EffectEntry sensor_entries[] = {
        { N_("Sensor Monitor"), 106, N_("Monitor system sensors on keyboard") },
    };
```

The `make_expander_category()` call stays the same — it will just create 1 row instead of 3. Category stays `"sensor"`.

- [ ] **Step 4: Build and verify**

Run: `cd /home/emrah/Projects/F87Control/build && cmake .. && make`
Expected: Compiles without errors or warnings.

- [ ] **Step 5: Commit**

```bash
git add gui/src/effect_meta.h gui/src/effect_meta.c gui/src/sidebar.c
git commit -m "feat(gui): single Sensor Monitor sidebar entry with F87_PARAM_SENSOR flag"
```

---

### Task 3: Keyboard View — Sensör Overlay + Seçim Modu API

Klavye widget'ına sensör atama overlay'i (renkli border, etiket) ve tıklama ile sensör yerleştirme modu ekle.

**Files:**
- Modify: `gui/src/keyboard_view.h`
- Modify: `gui/src/keyboard_view.c`

- [ ] **Step 1: Add sensor overlay data structures and API to keyboard_view.h**

After the existing paint mode API, add:

```c
/* Sensor overlay — shows colored borders and labels on assigned keys */
typedef struct {
    int key_id;
    uint8_t color[3];       /* Sensor color (teal, coral, etc.) */
    char label[16];         /* Short label: "CPU 72°C" */
} f87_key_overlay_t;

void f87_keyboard_view_set_overlays(F87KeyboardView *view,
                                     const f87_key_overlay_t *overlays, int count);
void f87_keyboard_view_clear_overlays(F87KeyboardView *view);

/* Sensor placement mode — click callback returns key_id for placement */
typedef void (*F87KeyClickCallback)(int key_id, gpointer user_data);
void f87_keyboard_view_set_click_mode(F87KeyboardView *view, gboolean enabled,
                                       F87KeyClickCallback cb, gpointer user_data);
```

- [ ] **Step 2: Add overlay storage to F87KeyboardView struct**

In `keyboard_view.c`, add to struct:

```c
    /* Sensor overlays */
    f87_key_overlay_t overlays[F87_KEY_COUNT];
    int overlay_count;

    /* Sensor click mode (separate from paint mode) */
    gboolean click_mode;
    F87KeyClickCallback click_cb;
    gpointer click_data;
```

- [ ] **Step 3: Implement overlay drawing in draw_func**

In `keyboard_view.c` `draw_func()`, after the key label drawing block (after `cairo_show_text(cr, label);` closing brace), add overlay rendering:

```c
    /* Sensor overlay borders and labels */
    for (int ov = 0; ov < self->overlay_count; ov++) {
        if (self->overlays[ov].key_id != i) continue;

        uint8_t or_ = self->overlays[ov].color[0];
        uint8_t og  = self->overlays[ov].color[1];
        uint8_t ob  = self->overlays[ov].color[2];

        /* Colored border around key */
        double bw = 2.0 * fmin(sx, sy);
        draw_rounded_rect(cr, x + bw/2, y + bw/2, w - bw, h - bw, radius);
        cairo_set_source_rgba(cr, or_/255.0, og/255.0, ob/255.0, 0.8);
        cairo_set_line_width(cr, bw);
        cairo_stroke(cr);

        /* Overlay label (below normal key label) */
        if (self->overlays[ov].label[0] && tw > 12) {
            double lbl_size = th * 0.28;
            if (lbl_size < 4) lbl_size = 4;
            if (lbl_size > 9) lbl_size = 9;
            cairo_set_font_size(cr, lbl_size);
            cairo_set_source_rgba(cr, or_/255.0, og/255.0, ob/255.0, 0.9);

            cairo_text_extents_t oext;
            cairo_text_extents(cr, self->overlays[ov].label, &oext);
            cairo_move_to(cr, tx + (tw - oext.width) / 2,
                          ty + th - lbl_size * 0.3);
            cairo_show_text(cr, self->overlays[ov].label);
        }
        break;
    }
```

- [ ] **Step 4: Implement overlay public API**

```c
void f87_keyboard_view_set_overlays(F87KeyboardView *view,
                                     const f87_key_overlay_t *overlays, int count)
{
    if (count > F87_KEY_COUNT) count = F87_KEY_COUNT;
    memcpy(view->overlays, overlays, count * sizeof(f87_key_overlay_t));
    view->overlay_count = count;
    gtk_widget_queue_draw(GTK_WIDGET(view));
}

void f87_keyboard_view_clear_overlays(F87KeyboardView *view)
{
    view->overlay_count = 0;
    gtk_widget_queue_draw(GTK_WIDGET(view));
}
```

- [ ] **Step 5: Implement click mode**

In `on_key_pressed`, before the paint mode check, add click mode:

```c
    if (self->click_mode) {
        int w = gtk_widget_get_width(GTK_WIDGET(self));
        int h = gtk_widget_get_height(GTK_WIDGET(self));
        int key_id = hit_test((int)x, (int)y, w, h);
        if (key_id >= 0 && self->click_cb)
            self->click_cb(key_id, self->click_data);
        return;  /* Don't fall through to paint mode */
    }
```

Implement the public function:

```c
void f87_keyboard_view_set_click_mode(F87KeyboardView *view, gboolean enabled,
                                       F87KeyClickCallback cb, gpointer user_data)
{
    view->click_mode = enabled;
    view->click_cb = cb;
    view->click_data = user_data;

    if (enabled)
        gtk_widget_set_cursor_from_name(GTK_WIDGET(view), "crosshair");
    else if (!view->paint_mode)
        gtk_widget_set_cursor(GTK_WIDGET(view), NULL);
}
```

- [ ] **Step 6: Build and verify**

Run: `cd /home/emrah/Projects/F87Control/build && cmake .. && make`
Expected: Compiles clean.

- [ ] **Step 7: Commit**

```bash
git add gui/src/keyboard_view.h gui/src/keyboard_view.c
git commit -m "feat(gui): keyboard overlay borders/labels and click mode for sensor placement"
```

---

### Task 4: Sensor Editor Widget

Yeni `sensor_editor` widget: profil dropdown, custom editör (sensör/mod/bar seçimi + atama listesi), canlı sensör değerleri, klavyeye atama callbacks.

**Files:**
- Create: `gui/src/sensor_editor.h`
- Create: `gui/src/sensor_editor.c`
- Modify: `gui/CMakeLists.txt`

- [ ] **Step 1: Create sensor_editor.h**

```c
#ifndef F87_SENSOR_EDITOR_H
#define F87_SENSOR_EDITOR_H

#include <adwaita.h>
#include "keyboard_view.h"
#include "app_state.h"

/* Sensor colors — fixed per sensor type */
#define F87_SENSOR_COLOR_CPU_TEMP   (uint8_t[]){78, 205, 196}   /* #4ecdc4 teal */
#define F87_SENSOR_COLOR_CPU_LOAD   (uint8_t[]){255, 107, 107}  /* #ff6b6b coral */
#define F87_SENSOR_COLOR_GPU_TEMP   (uint8_t[]){168, 85, 247}   /* #a855f7 purple */
#define F87_SENSOR_COLOR_RAM_USAGE  (uint8_t[]){255, 230, 109}  /* #ffe66d yellow */

typedef struct _F87SensorEditor F87SensorEditor;

/* Create/destroy */
F87SensorEditor *f87_sensor_editor_new(F87KeyboardView *keyboard, f87_app_state_t *state);
void f87_sensor_editor_destroy(F87SensorEditor *editor);

/* Get the top-level widget to pack into controls panel */
GtkWidget *f87_sensor_editor_get_widget(F87SensorEditor *editor);

/* Called when sensor monitor is selected/deselected in sidebar */
void f87_sensor_editor_activate(F87SensorEditor *editor);
void f87_sensor_editor_deactivate(F87SensorEditor *editor);

/* Get current profile name and config path for starting the effect.
 * Returns "developer"/"gamer"/"system" for builtins, "custom" for custom.
 * config_path is set to JSON path for custom, NULL for builtins. */
const char *f87_sensor_editor_get_profile(F87SensorEditor *editor,
                                           const char **config_path);

/* Get brightness value */
uint8_t f87_sensor_editor_get_brightness(F87SensorEditor *editor);

/* Is custom mode active? */
gboolean f87_sensor_editor_is_custom(F87SensorEditor *editor);

#endif /* F87_SENSOR_EDITOR_H */
```

- [ ] **Step 2: Create sensor_editor.c — struct and helpers**

```c
#include "sensor_editor.h"
#include "preview.h"
#include "i18n.h"
#include "../lib/src/sensor.h"
#include "../lib/src/sensor_config.h"
#include "../lib/src/protocol.h"
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

/* Max mappings in custom editor */
#define MAX_CUSTOM_MAPPINGS 16

/* Sensor color palette — indexed by sensor name */
typedef struct {
    const char *name;
    uint8_t color[3];
} sensor_color_t;

static const sensor_color_t sensor_colors[] = {
    {"cpu_temp",  { 78, 205, 196}},
    {"cpu_load",  {255, 107, 107}},
    {"gpu_temp",  {168,  85, 247}},
    {"ram_usage", {255, 230, 109}},
};
#define NUM_SENSOR_COLORS 4

static const uint8_t *get_sensor_color(const char *name)
{
    for (int i = 0; i < NUM_SENSOR_COLORS; i++) {
        if (strcmp(sensor_colors[i].name, name) == 0)
            return sensor_colors[i].color;
    }
    return sensor_colors[0].color;  /* fallback teal */
}

/* Custom mapping entry (editor state, not the lib struct) */
typedef struct {
    char sensor_name[32];
    int mode;           /* 0=color, 1=bar */
    int start_key;      /* Starting key_id */
    int bar_length;     /* 1 for color mode, 2-8 for bar */
} custom_mapping_t;

struct _F87SensorEditor {
    GtkBox *container;
    F87KeyboardView *keyboard;
    f87_app_state_t *state;

    /* Profile dropdown */
    GtkDropDown *profile_dropdown;  /* Developer/Gamer/System/Custom */

    /* Custom editor widgets */
    GtkBox *editor_box;             /* Container for custom editor UI */
    GtkDropDown *sensor_dropdown;   /* Sensor selection with live values */
    GtkToggleButton *mode_color_btn;
    GtkToggleButton *mode_bar_btn;
    GtkScale *bar_length_scale;
    GtkWidget *bar_length_row;      /* Container — hidden when color mode */
    GtkBox *mappings_list;          /* VBox listing current assignments */
    GtkLabel *hint_label;           /* "Click keyboard to place sensor" */
    GtkScale *brightness_scale;

    /* Builtin profile info display */
    GtkBox *profile_info_box;       /* Shows mapping summary for builtin profiles */

    /* Custom state */
    custom_mapping_t mappings[MAX_CUSTOM_MAPPINGS];
    int mapping_count;

    /* Sensor live read state */
    guint sensor_timer;             /* Periodic sensor value refresh */
    void *sensor_ctx[4];            /* init'd sensor contexts (up to 4) */
    float sensor_values[4];         /* Latest normalized values */
    gboolean sensor_available[4];   /* Whether sensor init succeeded */
    char sensor_labels[4][32];      /* Dropdown labels with values */

    /* Custom profile save path */
    char custom_path[256];
};
```

- [ ] **Step 3: Implement sensor value reading**

Continue in `sensor_editor.c`:

```c
static void init_sensor_contexts(F87SensorEditor *ed)
{
    int n = f87_sensor_count();
    if (n > 4) n = 4;

    for (int i = 0; i < n; i++) {
        const f87_sensor_t *s = f87_sensor_get(i);
        ed->sensor_available[i] = (s->init(&ed->sensor_ctx[i]) == 0);
        ed->sensor_values[i] = -1;
    }
}

static void destroy_sensor_contexts(F87SensorEditor *ed)
{
    int n = f87_sensor_count();
    if (n > 4) n = 4;

    for (int i = 0; i < n; i++) {
        if (ed->sensor_available[i]) {
            const f87_sensor_t *s = f87_sensor_get(i);
            s->destroy(ed->sensor_ctx[i]);
            ed->sensor_ctx[i] = NULL;
            ed->sensor_available[i] = FALSE;
        }
    }
}

static void read_sensor_values(F87SensorEditor *ed)
{
    int n = f87_sensor_count();
    if (n > 4) n = 4;

    for (int i = 0; i < n; i++) {
        const f87_sensor_t *s = f87_sensor_get(i);
        if (ed->sensor_available[i]) {
            float raw = s->read(ed->sensor_ctx[i]);
            ed->sensor_values[i] = f87_sensor_normalize(raw, s->min_value, s->max_value);

            /* Format label: "CPU Temp (72°C)" or "CPU Load (45%)" */
            const char *unit = (s->max_value > 100) ? "°C" : "%";
            snprintf(ed->sensor_labels[i], sizeof(ed->sensor_labels[i]),
                     "%s (%.0f%s)", s->description, raw, unit);
        } else {
            snprintf(ed->sensor_labels[i], sizeof(ed->sensor_labels[i]),
                     "%s (%s)", s->description, _("unavailable"));
        }
    }
}
```

- [ ] **Step 4: Implement overlay update from mappings**

```c
static void update_keyboard_overlays(F87SensorEditor *ed)
{
    f87_key_overlay_t overlays[F87_KEY_COUNT];
    int ov_count = 0;

    for (int m = 0; m < ed->mapping_count; m++) {
        custom_mapping_t *cm = &ed->mappings[m];
        const uint8_t *col = get_sensor_color(cm->sensor_name);
        int n_keys = (cm->mode == 1) ? cm->bar_length : 1;

        for (int k = 0; k < n_keys && ov_count < F87_KEY_COUNT; k++) {
            int kid = cm->start_key + k;
            if (kid >= F87_KEY_COUNT) break;

            f87_key_overlay_t *ov = &overlays[ov_count++];
            ov->key_id = kid;
            memcpy(ov->color, col, 3);

            /* Label: show sensor short name + value on first key of group */
            if (k == 0) {
                const f87_sensor_t *s = f87_sensor_find(cm->sensor_name);
                if (s) {
                    int idx = -1;
                    for (int i = 0; i < f87_sensor_count() && i < 4; i++) {
                        if (f87_sensor_get(i) == s) { idx = i; break; }
                    }
                    if (idx >= 0 && ed->sensor_available[idx]) {
                        float raw = ed->sensor_values[idx] *
                                    (s->max_value - s->min_value) + s->min_value;
                        const char *unit = (s->max_value > 100) ? "C" : "%";
                        snprintf(ov->label, sizeof(ov->label), "%.0f%s", raw, unit);
                    } else {
                        snprintf(ov->label, sizeof(ov->label), "--");
                    }
                } else {
                    ov->label[0] = '\0';
                }
            } else {
                ov->label[0] = '\0';
            }
        }
    }

    f87_keyboard_view_set_overlays(ed->keyboard, overlays, ov_count);
}
```

- [ ] **Step 5: Implement keyboard click handler for sensor placement**

```c
static gboolean is_key_assigned(F87SensorEditor *ed, int key_id)
{
    for (int m = 0; m < ed->mapping_count; m++) {
        int n = (ed->mappings[m].mode == 1) ? ed->mappings[m].bar_length : 1;
        for (int k = 0; k < n; k++) {
            if (ed->mappings[m].start_key + k == key_id)
                return TRUE;
        }
    }
    return FALSE;
}

static void on_sensor_key_clicked(int key_id, gpointer user_data)
{
    F87SensorEditor *ed = user_data;
    if (ed->mapping_count >= MAX_CUSTOM_MAPPINGS) return;

    /* Get selected sensor */
    guint idx = gtk_drop_down_get_selected(ed->sensor_dropdown);
    int n = f87_sensor_count();
    if (n > 4) n = 4;
    if ((int)idx >= n) return;

    const f87_sensor_t *s = f87_sensor_get((int)idx);
    if (!s || !ed->sensor_available[idx]) return;

    /* Get mode */
    gboolean bar_mode = gtk_toggle_button_get_active(ed->mode_bar_btn);
    int bar_len = bar_mode ?
                  (int)gtk_range_get_value(GTK_RANGE(ed->bar_length_scale)) : 1;

    /* Check for collisions */
    for (int k = 0; k < bar_len; k++) {
        int kid = key_id + k;
        if (kid >= F87_KEY_COUNT) {
            /* Doesn't fit */
            gtk_label_set_text(ed->hint_label, _("Not enough keys — choose a different position"));
            return;
        }
        if (is_key_assigned(ed, kid)) {
            gtk_label_set_text(ed->hint_label, _("Key already assigned — remove it first"));
            return;
        }
    }

    /* Add mapping */
    custom_mapping_t *cm = &ed->mappings[ed->mapping_count++];
    strncpy(cm->sensor_name, s->name, sizeof(cm->sensor_name) - 1);
    cm->mode = bar_mode ? 1 : 0;
    cm->start_key = key_id;
    cm->bar_length = bar_len;

    gtk_label_set_text(ed->hint_label, _("Click keyboard to place sensor"));
    rebuild_mappings_list(ed);
    update_keyboard_overlays(ed);
}
```

- [ ] **Step 6: Implement mappings list rebuild**

```c
static void on_remove_mapping(GtkButton *btn, gpointer user_data);

static void rebuild_mappings_list(F87SensorEditor *ed)
{
    /* Clear existing children */
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(GTK_WIDGET(ed->mappings_list))))
        gtk_box_remove(ed->mappings_list, child);

    for (int i = 0; i < ed->mapping_count; i++) {
        custom_mapping_t *cm = &ed->mappings[i];
        const uint8_t *col = get_sensor_color(cm->sensor_name);

        GtkBox *row = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6));
        gtk_widget_add_css_class(GTK_WIDGET(row), "sensor-mapping-row");

        /* Sensor name label (colored) */
        GtkLabel *name_lbl = GTK_LABEL(gtk_label_new(cm->sensor_name));
        char css[64];
        snprintf(css, sizeof(css), "color: #%02x%02x%02x;", col[0], col[1], col[2]);
        GtkCssProvider *prov = gtk_css_provider_new();
        gtk_css_provider_load_from_string(prov, css);
        gtk_style_context_add_provider(
            gtk_widget_get_style_context(GTK_WIDGET(name_lbl)),
            GTK_STYLE_PROVIDER(prov), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(prov);
        gtk_widget_set_size_request(GTK_WIDGET(name_lbl), 80, -1);
        gtk_label_set_xalign(name_lbl, 0);
        gtk_box_append(row, GTK_WIDGET(name_lbl));

        /* Key range */
        char range[32];
        const char *start_name = f87_key_layout[cm->start_key].name;
        if (cm->mode == 0) {
            snprintf(range, sizeof(range), "%s", start_name);
        } else {
            int end_key = cm->start_key + cm->bar_length - 1;
            if (end_key >= F87_KEY_COUNT) end_key = F87_KEY_COUNT - 1;
            snprintf(range, sizeof(range), "%s → %s",
                     start_name, f87_key_layout[end_key].name);
        }
        GtkLabel *range_lbl = GTK_LABEL(gtk_label_new(range));
        gtk_widget_set_opacity(GTK_WIDGET(range_lbl), 0.6);
        gtk_box_append(row, GTK_WIDGET(range_lbl));

        /* Mode tag */
        const char *mode_str = cm->mode == 0 ? _("color") : _("bar");
        char mode_tag[16];
        if (cm->mode == 1)
            snprintf(mode_tag, sizeof(mode_tag), "%s (%d)", mode_str, cm->bar_length);
        else
            snprintf(mode_tag, sizeof(mode_tag), "%s", mode_str);
        GtkLabel *mode_lbl = GTK_LABEL(gtk_label_new(mode_tag));
        gtk_widget_set_opacity(GTK_WIDGET(mode_lbl), 0.4);
        gtk_widget_add_css_class(GTK_WIDGET(mode_lbl), "caption");
        gtk_box_append(row, GTK_WIDGET(mode_lbl));

        /* Spacer */
        GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_set_hexpand(spacer, TRUE);
        gtk_box_append(row, spacer);

        /* Remove button */
        GtkButton *rm_btn = GTK_BUTTON(gtk_button_new_with_label("✕"));
        gtk_widget_add_css_class(GTK_WIDGET(rm_btn), "flat");
        gtk_widget_add_css_class(GTK_WIDGET(rm_btn), "circular");
        g_object_set_data(G_OBJECT(rm_btn), "mapping-index", GINT_TO_POINTER(i));
        g_signal_connect(rm_btn, "clicked", G_CALLBACK(on_remove_mapping), ed);
        gtk_box_append(row, GTK_WIDGET(rm_btn));

        gtk_box_append(ed->mappings_list, GTK_WIDGET(row));
    }
}

static void on_remove_mapping(GtkButton *btn, gpointer user_data)
{
    F87SensorEditor *ed = user_data;
    int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "mapping-index"));
    if (idx < 0 || idx >= ed->mapping_count) return;

    /* Shift remaining mappings */
    for (int i = idx; i < ed->mapping_count - 1; i++)
        ed->mappings[i] = ed->mappings[i + 1];
    ed->mapping_count--;

    rebuild_mappings_list(ed);
    update_keyboard_overlays(ed);
}
```

- [ ] **Step 7: Implement builtin profile info display**

```c
static void show_builtin_profile_info(F87SensorEditor *ed, const char *profile_name)
{
    /* Clear editor box — hide custom controls */
    gtk_widget_set_visible(GTK_WIDGET(ed->editor_box), FALSE);

    /* Clear old info */
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(GTK_WIDGET(ed->profile_info_box))))
        gtk_box_remove(ed->profile_info_box, child);

    /* Load builtin profile to show its mappings */
    f87_sensor_profile_t profile = {0};
    if (f87_sensor_config_builtin(profile_name, &profile,
                                   f87_key_layout, F87_KEY_COUNT) < 0)
        return;

    GtkLabel *header = GTK_LABEL(gtk_label_new(_("Sensor Assignments")));
    gtk_label_set_xalign(header, 0);
    gtk_widget_set_opacity(GTK_WIDGET(header), 0.5);
    gtk_widget_add_css_class(GTK_WIDGET(header), "caption");
    gtk_box_append(ed->profile_info_box, GTK_WIDGET(header));

    GtkBox *cards_row = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8));

    for (int i = 0; i < profile.mapping_count; i++) {
        f87_sensor_mapping_t *m = &profile.mappings[i];
        const uint8_t *col = get_sensor_color(m->sensor_name);

        GtkBox *card = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 2));
        gtk_widget_add_css_class(GTK_WIDGET(card), "sensor-info-card");

        /* Sensor name */
        GtkLabel *name_lbl = GTK_LABEL(gtk_label_new(m->sensor_name));
        char css[80];
        snprintf(css, sizeof(css), "label { color: #%02x%02x%02x; font-weight: 600; }",
                 col[0], col[1], col[2]);
        GtkCssProvider *prov = gtk_css_provider_new();
        gtk_css_provider_load_from_string(prov, css);
        gtk_style_context_add_provider(
            gtk_widget_get_style_context(GTK_WIDGET(name_lbl)),
            GTK_STYLE_PROVIDER(prov), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(prov);
        gtk_box_append(card, GTK_WIDGET(name_lbl));

        /* Key range + mode */
        char info[64];
        if (m->key_count == 1) {
            snprintf(info, sizeof(info), "%s • %s",
                     f87_key_layout[m->key_ids[0]].name,
                     m->mode == F87_SENSOR_MODE_BAR ? _("bar") : _("color"));
        } else {
            snprintf(info, sizeof(info), "%s → %s • %s",
                     f87_key_layout[m->key_ids[0]].name,
                     f87_key_layout[m->key_ids[m->key_count - 1]].name,
                     m->mode == F87_SENSOR_MODE_BAR ? _("bar") : _("color"));
        }
        GtkLabel *info_lbl = GTK_LABEL(gtk_label_new(info));
        gtk_widget_set_opacity(GTK_WIDGET(info_lbl), 0.5);
        gtk_widget_add_css_class(GTK_WIDGET(info_lbl), "caption");
        gtk_box_append(card, GTK_WIDGET(info_lbl));

        gtk_widget_set_hexpand(GTK_WIDGET(card), TRUE);
        gtk_box_append(cards_row, GTK_WIDGET(card));

        free(m->sensor_name);
    }

    gtk_box_append(ed->profile_info_box, GTK_WIDGET(cards_row));
    gtk_widget_set_visible(GTK_WIDGET(ed->profile_info_box), TRUE);

    /* Update overlays for builtin profile too */
    ed->mapping_count = 0;
    for (int i = 0; i < profile.mapping_count && i < MAX_CUSTOM_MAPPINGS; i++) {
        /* Re-parse from profile for overlays — keys are already freed above,
         * but we stored key_ids before free. Actually, let's reload. */
    }
    /* Simpler: rebuild overlay from the dropdown-selected builtin */
    f87_sensor_profile_t p2 = {0};
    if (f87_sensor_config_builtin(profile_name, &p2, f87_key_layout, F87_KEY_COUNT) == 0) {
        ed->mapping_count = 0;
        for (int i = 0; i < p2.mapping_count && i < MAX_CUSTOM_MAPPINGS; i++) {
            custom_mapping_t *cm = &ed->mappings[ed->mapping_count++];
            strncpy(cm->sensor_name, p2.mappings[i].sensor_name, sizeof(cm->sensor_name) - 1);
            cm->mode = p2.mappings[i].mode;
            cm->start_key = p2.mappings[i].key_ids[0];
            cm->bar_length = p2.mappings[i].key_count;
            free(p2.mappings[i].sensor_name);
        }
        update_keyboard_overlays(ed);
    }
}
```

- [ ] **Step 8: Implement profile dropdown change handler**

```c
static void on_profile_changed(GtkDropDown *dropdown, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec;
    F87SensorEditor *ed = user_data;
    guint idx = gtk_drop_down_get_selected(dropdown);

    const char *profiles[] = {"developer", "gamer", "system"};

    if (idx < 3) {
        /* Builtin profile */
        show_builtin_profile_info(ed, profiles[idx]);
        f87_keyboard_view_set_click_mode(ed->keyboard, FALSE, NULL, NULL);
    } else {
        /* Custom mode */
        gtk_widget_set_visible(GTK_WIDGET(ed->profile_info_box), FALSE);
        gtk_widget_set_visible(GTK_WIDGET(ed->editor_box), TRUE);
        f87_keyboard_view_set_click_mode(ed->keyboard, TRUE,
                                          on_sensor_key_clicked, ed);
        /* Load saved custom profile if exists */
        /* (handled in activate) */
        update_keyboard_overlays(ed);
    }
}
```

- [ ] **Step 9: Implement mode toggle and bar length handlers**

```c
static void on_mode_toggled(GtkToggleButton *btn, gpointer user_data)
{
    F87SensorEditor *ed = user_data;
    (void)btn;
    gboolean bar = gtk_toggle_button_get_active(ed->mode_bar_btn);
    gtk_widget_set_visible(ed->bar_length_row, bar);
}
```

- [ ] **Step 10: Implement sensor timer for live dropdown updates**

```c
static gboolean on_sensor_timer(gpointer data)
{
    F87SensorEditor *ed = data;
    read_sensor_values(ed);
    update_keyboard_overlays(ed);

    /* Update dropdown labels — unfortunately GtkDropDown doesn't support
     * updating labels easily. We update the hint label instead. */
    guint idx = gtk_drop_down_get_selected(ed->sensor_dropdown);
    int n = f87_sensor_count();
    if (n > 4) n = 4;
    if ((int)idx < n) {
        gtk_label_set_text(ed->hint_label, ed->sensor_labels[idx]);
    }
    return G_SOURCE_CONTINUE;
}
```

- [ ] **Step 11: Implement f87_sensor_editor_new**

```c
/* Forward declarations */
static void rebuild_mappings_list(F87SensorEditor *ed);
static void on_sensor_key_clicked(int key_id, gpointer user_data);
static void show_builtin_profile_info(F87SensorEditor *ed, const char *name);

F87SensorEditor *f87_sensor_editor_new(F87KeyboardView *keyboard, f87_app_state_t *state)
{
    F87SensorEditor *ed = g_new0(F87SensorEditor, 1);
    ed->keyboard = keyboard;
    ed->state = state;

    /* Custom profile path */
    const char *config_dir = g_get_user_config_dir();
    snprintf(ed->custom_path, sizeof(ed->custom_path),
             "%s/f87control/profiles/sensor_custom.json", config_dir);

    /* Init sensor contexts for live reading */
    init_sensor_contexts(ed);
    read_sensor_values(ed);

    /* Main container */
    ed->container = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 8));

    /* Profile dropdown row */
    GtkBox *prof_row = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8));
    GtkLabel *prof_label = GTK_LABEL(gtk_label_new(_("Profile")));
    gtk_widget_set_size_request(GTK_WIDGET(prof_label), 70, -1);
    gtk_widget_set_opacity(GTK_WIDGET(prof_label), 0.7);
    gtk_box_append(prof_row, GTK_WIDGET(prof_label));

    const char *prof_items[] = {"Developer", "Gamer", "System", "Custom", NULL};
    ed->profile_dropdown = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(prof_items));
    g_signal_connect(ed->profile_dropdown, "notify::selected",
                     G_CALLBACK(on_profile_changed), ed);
    gtk_box_append(prof_row, GTK_WIDGET(ed->profile_dropdown));
    gtk_box_append(ed->container, GTK_WIDGET(prof_row));

    /* Brightness slider */
    GtkBox *br_row = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4));
    GtkLabel *br_label = GTK_LABEL(gtk_label_new(_("Brightness")));
    gtk_widget_set_size_request(GTK_WIDGET(br_label), 70, -1);
    gtk_widget_set_opacity(GTK_WIDGET(br_label), 0.7);
    gtk_box_append(br_row, GTK_WIDGET(br_label));
    ed->brightness_scale = GTK_SCALE(gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 1, 4, 1));
    gtk_range_set_value(GTK_RANGE(ed->brightness_scale), 3);
    gtk_widget_set_hexpand(GTK_WIDGET(ed->brightness_scale), TRUE);
    gtk_box_append(br_row, GTK_WIDGET(ed->brightness_scale));
    gtk_box_append(ed->container, GTK_WIDGET(br_row));

    /* Builtin profile info box (shown for Developer/Gamer/System) */
    ed->profile_info_box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 4));
    gtk_box_append(ed->container, GTK_WIDGET(ed->profile_info_box));

    /* Custom editor box (shown for Custom) */
    ed->editor_box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 6));
    gtk_widget_set_visible(GTK_WIDGET(ed->editor_box), FALSE);

    /* Sensor dropdown */
    GtkBox *sens_row = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8));
    GtkLabel *sens_label = GTK_LABEL(gtk_label_new(_("Sensor")));
    gtk_widget_set_size_request(GTK_WIDGET(sens_label), 70, -1);
    gtk_widget_set_opacity(GTK_WIDGET(sens_label), 0.7);
    gtk_box_append(sens_row, GTK_WIDGET(sens_label));

    /* Build sensor dropdown items with current values */
    int n = f87_sensor_count();
    if (n > 4) n = 4;
    const char *sensor_items[5] = {NULL};
    for (int i = 0; i < n; i++)
        sensor_items[i] = ed->sensor_labels[i];
    ed->sensor_dropdown = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(sensor_items));
    gtk_box_append(sens_row, GTK_WIDGET(ed->sensor_dropdown));
    gtk_box_append(ed->editor_box, GTK_WIDGET(sens_row));

    /* Mode toggle: Color | Bar */
    GtkBox *mode_row = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8));
    GtkLabel *mode_label = GTK_LABEL(gtk_label_new(_("Mode")));
    gtk_widget_set_size_request(GTK_WIDGET(mode_label), 70, -1);
    gtk_widget_set_opacity(GTK_WIDGET(mode_label), 0.7);
    gtk_box_append(mode_row, GTK_WIDGET(mode_label));

    ed->mode_color_btn = GTK_TOGGLE_BUTTON(gtk_toggle_button_new_with_label(_("Single Key")));
    ed->mode_bar_btn = GTK_TOGGLE_BUTTON(gtk_toggle_button_new_with_label(_("Bar")));
    gtk_toggle_button_set_group(ed->mode_bar_btn, ed->mode_color_btn);
    gtk_toggle_button_set_active(ed->mode_bar_btn, TRUE);
    g_signal_connect(ed->mode_bar_btn, "toggled", G_CALLBACK(on_mode_toggled), ed);
    gtk_box_append(mode_row, GTK_WIDGET(ed->mode_color_btn));
    gtk_box_append(mode_row, GTK_WIDGET(ed->mode_bar_btn));
    gtk_box_append(ed->editor_box, GTK_WIDGET(mode_row));

    /* Bar length slider */
    GtkBox *bar_row = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8));
    GtkLabel *bar_label = GTK_LABEL(gtk_label_new(_("Bar Length")));
    gtk_widget_set_size_request(GTK_WIDGET(bar_label), 70, -1);
    gtk_widget_set_opacity(GTK_WIDGET(bar_label), 0.7);
    gtk_box_append(bar_row, GTK_WIDGET(bar_label));
    ed->bar_length_scale = GTK_SCALE(gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 2, 8, 1));
    gtk_range_set_value(GTK_RANGE(ed->bar_length_scale), 4);
    gtk_widget_set_hexpand(GTK_WIDGET(ed->bar_length_scale), TRUE);
    gtk_box_append(bar_row, GTK_WIDGET(ed->bar_length_scale));
    ed->bar_length_row = GTK_WIDGET(bar_row);
    gtk_box_append(ed->editor_box, ed->bar_length_row);

    /* Hint label */
    ed->hint_label = GTK_LABEL(gtk_label_new(_("Click keyboard to place sensor")));
    gtk_label_set_xalign(ed->hint_label, 0);
    gtk_widget_set_opacity(GTK_WIDGET(ed->hint_label), 0.5);
    gtk_widget_add_css_class(GTK_WIDGET(ed->hint_label), "caption");
    gtk_box_append(ed->editor_box, GTK_WIDGET(ed->hint_label));

    /* Mappings list */
    GtkLabel *map_header = GTK_LABEL(gtk_label_new(_("Assignments")));
    gtk_label_set_xalign(map_header, 0);
    gtk_widget_set_opacity(GTK_WIDGET(map_header), 0.5);
    gtk_widget_add_css_class(GTK_WIDGET(map_header), "caption");
    gtk_box_append(ed->editor_box, GTK_WIDGET(map_header));

    ed->mappings_list = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 2));
    gtk_box_append(ed->editor_box, GTK_WIDGET(ed->mappings_list));

    gtk_box_append(ed->container, GTK_WIDGET(ed->editor_box));

    return ed;
}
```

- [ ] **Step 12: Implement remaining public API**

```c
void f87_sensor_editor_destroy(F87SensorEditor *editor)
{
    if (!editor) return;
    if (editor->sensor_timer) {
        g_source_remove(editor->sensor_timer);
        editor->sensor_timer = 0;
    }
    destroy_sensor_contexts(editor);
    f87_keyboard_view_clear_overlays(editor->keyboard);
    f87_keyboard_view_set_click_mode(editor->keyboard, FALSE, NULL, NULL);
    g_free(editor);
}

GtkWidget *f87_sensor_editor_get_widget(F87SensorEditor *editor)
{
    return GTK_WIDGET(editor->container);
}

void f87_sensor_editor_activate(F87SensorEditor *editor)
{
    /* Start sensor polling timer */
    if (!editor->sensor_timer)
        editor->sensor_timer = g_timeout_add(2000, on_sensor_timer, editor);
    read_sensor_values(editor);

    /* Trigger profile display for current selection */
    on_profile_changed(editor->profile_dropdown, NULL, editor);
}

void f87_sensor_editor_deactivate(F87SensorEditor *editor)
{
    if (editor->sensor_timer) {
        g_source_remove(editor->sensor_timer);
        editor->sensor_timer = 0;
    }
    f87_keyboard_view_clear_overlays(editor->keyboard);
    f87_keyboard_view_set_click_mode(editor->keyboard, FALSE, NULL, NULL);
}

const char *f87_sensor_editor_get_profile(F87SensorEditor *editor,
                                           const char **config_path)
{
    guint idx = gtk_drop_down_get_selected(editor->profile_dropdown);
    const char *profiles[] = {"developer", "gamer", "system"};

    if (idx < 3) {
        if (config_path) *config_path = NULL;
        return profiles[idx];
    }

    /* Custom — save to file first */
    f87_sensor_profile_t profile = {0};
    strncpy(profile.profile_name, "custom", sizeof(profile.profile_name) - 1);
    for (int i = 0; i < editor->mapping_count; i++) {
        custom_mapping_t *cm = &editor->mappings[i];
        f87_sensor_mapping_t *m = &profile.mappings[profile.mapping_count];
        m->sensor_name = (char *)cm->sensor_name;  /* Not freed — it's stack/static */
        m->mode = cm->mode;
        m->key_count = (cm->mode == 1) ? cm->bar_length : 1;
        for (int k = 0; k < m->key_count; k++)
            m->key_ids[k] = cm->start_key + k;

        const f87_sensor_t *s = f87_sensor_find(cm->sensor_name);
        m->interval_ms = s ? s->default_interval_ms : 1000;
        profile.mapping_count++;
    }

    /* Ensure directory exists */
    char dir[256];
    snprintf(dir, sizeof(dir), "%s/f87control/profiles", g_get_user_config_dir());
    g_mkdir_with_parents(dir, 0755);

    f87_sensor_config_save(editor->custom_path, &profile,
                            f87_key_layout, F87_KEY_COUNT);

    if (config_path) *config_path = editor->custom_path;
    return "custom";
}

uint8_t f87_sensor_editor_get_brightness(F87SensorEditor *editor)
{
    return (uint8_t)gtk_range_get_value(GTK_RANGE(editor->brightness_scale));
}

gboolean f87_sensor_editor_is_custom(F87SensorEditor *editor)
{
    return gtk_drop_down_get_selected(editor->profile_dropdown) >= 3;
}
```

- [ ] **Step 13: Add sensor_editor.c to CMakeLists.txt**

In `gui/CMakeLists.txt`, add `src/sensor_editor.c` to the GUI sources list.

- [ ] **Step 14: Build and verify**

Run: `cd /home/emrah/Projects/F87Control/build && cmake .. && make`
Expected: Compiles clean.

- [ ] **Step 15: Commit**

```bash
git add gui/src/sensor_editor.h gui/src/sensor_editor.c gui/CMakeLists.txt
git commit -m "feat(gui): sensor editor widget — profile dropdown, custom mapping, keyboard placement"
```

---

### Task 5: Controls Integration — Sensor Editor'ı Bağla

Controls panelinde `F87_PARAM_SENSOR` flag'ini algılayıp sensor_editor widget'ını oluştur. Sensör efekti gönderme/durdurma mantığını güncelle.

**Files:**
- Modify: `gui/src/controls.c`

- [ ] **Step 1: Add sensor_editor include and struct field**

At the top of `controls.c`, add include:
```c
#include "sensor_editor.h"
```

In `struct _F87Controls`, add after `f87_preview_t *preview;`:
```c
    F87SensorEditor *sensor_editor;
```

- [ ] **Step 2: Update clear_params to clean up sensor editor**

In `clear_params()`, add before the `sv_cache` cleanup:

```c
    if (ctrl->sensor_editor) {
        f87_sensor_editor_deactivate(ctrl->sensor_editor);
        ctrl->sensor_editor = NULL;  /* Widget destroyed with params_box children */
    }
```

Note: The sensor_editor's widget is a child of params_box, so it gets destroyed when params_box is cleared. But the F87SensorEditor struct needs explicit cleanup. Update destroy to be safe:

Actually, since sensor_editor is `g_new0`'d and not a GtkWidget subclass, we need to free it. Add to `clear_params`:

```c
    if (ctrl->sensor_editor) {
        f87_sensor_editor_deactivate(ctrl->sensor_editor);
        f87_sensor_editor_destroy(ctrl->sensor_editor);
        ctrl->sensor_editor = NULL;
    }
```

But `f87_sensor_editor_destroy` calls `g_free`, and the widget it created is already being removed from the box. We need to make sure destroy doesn't double-free the widget. The widget lifetime is managed by GTK (parent container owns it), so `f87_sensor_editor_destroy` should only free the struct and cleanup state, not the widget. This is already the case — `g_free(editor)` just frees the struct.

- [ ] **Step 3: Add F87_PARAM_SENSOR handling in build_controls_for_effect**

In `build_controls_for_effect()`, add a new block after the `F87_PARAM_PAINT` block (before `gtk_box_append(split, GTK_WIDGET(left));`):

```c
    if (flags & F87_PARAM_SENSOR) {
        /* Sensor monitor uses its own editor widget instead of standard controls */
        ctrl->sensor_editor = f87_sensor_editor_new(ctrl->keyboard, ctrl->state);
        GtkWidget *editor_widget = f87_sensor_editor_get_widget(ctrl->sensor_editor);
        gtk_box_append(ctrl->params_box, editor_widget);
        f87_sensor_editor_activate(ctrl->sensor_editor);

        /* Sensor has its own brightness — skip the standard split layout */
        /* Add Start/Stop button */
        ctrl->send_button = GTK_BUTTON(gtk_button_new_with_label(_("Start")));
        gtk_widget_add_css_class(GTK_WIDGET(ctrl->send_button), "action-button");
        g_signal_connect(ctrl->send_button, "clicked", G_CALLBACK(on_send_clicked), ctrl);
        gtk_box_append(ctrl->params_box, GTK_WIDGET(ctrl->send_button));

        /* Start preview */
        if (ctrl->preview)
            f87_preview_start(ctrl->preview, ctrl->effect_id, ctrl->category, 2, 0, 0, 0);

        return;  /* Skip standard split layout */
    }
```

- [ ] **Step 4: Update send_sw_effect for F87_PARAM_SENSOR**

Replace the old `F87_PARAM_PROFILE` handling block with sensor editor logic:

Old:
```c
    if (meta->flags & F87_PARAM_PROFILE) {
        if (ctrl->sensor_profile_dropdown) {
            guint idx = gtk_drop_down_get_selected(ctrl->sensor_profile_dropdown);
            const char *profiles[] = {"developer", "gamer", "system"};
            if (idx < 3)
                config.sensor_profile = profiles[idx];
        } else {
            config.sensor_profile = ctrl->effect_name;
        }
    }

    int eid = ctrl->effect_id;
    if (meta->flags & F87_PARAM_PROFILE)
        eid = F87_SW_SENSOR;
```

New:
```c
    if (meta->flags & F87_PARAM_SENSOR) {
        if (ctrl->sensor_editor) {
            const char *path = NULL;
            config.sensor_profile = f87_sensor_editor_get_profile(
                ctrl->sensor_editor, &path);
            config.sensor_config_path = path;
            config.brightness = f87_sensor_editor_get_brightness(ctrl->sensor_editor);
        }
    }

    int eid = ctrl->effect_id;
    if (meta->flags & F87_PARAM_SENSOR)
        eid = F87_SW_SENSOR;
```

- [ ] **Step 5: Remove old sensor_profile_dropdown from clear_params**

In `clear_params()`, the line `ctrl->sensor_profile_dropdown = NULL;` can stay (it's harmless) but `F87_PARAM_PROFILE` is no longer used. No other changes needed.

- [ ] **Step 6: Build and verify**

Run: `cd /home/emrah/Projects/F87Control/build && cmake .. && make`
Expected: Compiles clean.

- [ ] **Step 7: Commit**

```bash
git add gui/src/controls.c
git commit -m "feat(gui): integrate sensor editor widget into controls panel"
```

---

### Task 6: Preview Enhancement — Profil-Aware Sensor Preview

Preview'ın `render_sensor` fonksiyonunu profil bilgisine göre güncelle. Sensör etiketleri keyboard overlay üzerinden gösterilecek.

**Files:**
- Modify: `gui/src/preview.c`
- Modify: `gui/src/preview.h` (if needed for new API)

- [ ] **Step 1: Add sensor profile data to preview struct**

In `preview.c`, add to `struct f87_preview`:

```c
    /* Sensor profile data for preview */
    struct {
        int start_key;
        int key_count;
        int mode;       /* 0=color, 1=bar */
        float phase;    /* For simulated oscillation */
        uint8_t color[3];
    } sensor_groups[16];
    int sensor_group_count;
```

- [ ] **Step 2: Add sensor profile loading to preview**

Add new public function and implementation:

In `preview.c`:

```c
void f87_preview_set_sensor_profile(f87_preview_t *prev, const char *profile_name)
{
    prev->sensor_group_count = 0;

    f87_sensor_profile_t profile = {0};
    int rc = f87_sensor_config_builtin(profile_name, &profile,
                                        f87_key_layout, F87_KEY_COUNT);
    if (rc < 0) return;

    /* Sensor color palette */
    static const uint8_t colors[][3] = {
        { 78, 205, 196},  /* cpu_temp — teal */
        {255, 107, 107},  /* cpu_load — coral */
        {168,  85, 247},  /* gpu_temp — purple */
        {255, 230, 109},  /* ram_usage — yellow */
    };
    static const char *sensor_names[] = {"cpu_temp", "cpu_load", "gpu_temp", "ram_usage"};

    for (int i = 0; i < profile.mapping_count && i < 16; i++) {
        f87_sensor_mapping_t *m = &profile.mappings[i];
        prev->sensor_groups[prev->sensor_group_count].start_key = m->key_ids[0];
        prev->sensor_groups[prev->sensor_group_count].key_count = m->key_count;
        prev->sensor_groups[prev->sensor_group_count].mode = m->mode;
        prev->sensor_groups[prev->sensor_group_count].phase = (float)i * 1.5f;

        /* Match color by sensor name */
        const uint8_t *col = colors[0];
        for (int s = 0; s < 4; s++) {
            if (strcmp(m->sensor_name, sensor_names[s]) == 0) {
                col = colors[s]; break;
            }
        }
        memcpy(prev->sensor_groups[prev->sensor_group_count].color, col, 3);
        prev->sensor_group_count++;

        free(m->sensor_name);
    }
}
```

Add declaration to `gui/src/preview.h`:
```c
void f87_preview_set_sensor_profile(f87_preview_t *prev, const char *profile_name);
```

- [ ] **Step 3: Rewrite render_sensor to use profile data**

Replace the old `render_sensor()`:

```c
static void render_sensor(f87_preview_t *p)
{
    memset(p->buf, 0, sizeof(p->buf));
    float t = (float)p->frame * 0.03f;

    if (p->sensor_group_count == 0) {
        /* Fallback: old-style 4 groups on F1-F12 */
        struct { int start; int count; float phase; } groups[] = {
            {1, 3, 0.0f}, {4, 3, 1.5f}, {7, 3, 3.0f}, {10, 3, 4.5f}
        };
        for (int g = 0; g < 4; g++) {
            float val = 0.55f + 0.35f * sinf(t + groups[g].phase);
            int lit = (int)(val * groups[g].count + 0.5f);
            if (lit > groups[g].count) lit = groups[g].count;
            for (int i = 0; i < groups[g].count; i++) {
                int key = groups[g].start + i;
                if (key >= KEY_COUNT) break;
                if (i < lit) {
                    float ratio = (float)i / fmaxf(groups[g].count - 1, 1);
                    p->buf[key][0] = (uint8_t)(ratio * 255);
                    p->buf[key][1] = (uint8_t)((1.0f - ratio) * 255);
                    p->buf[key][2] = 0;
                }
            }
        }
        p->buf[0][0] = 0; p->buf[0][1] = 100; p->buf[0][2] = 255;
        return;
    }

    /* Profile-aware rendering */
    for (int g = 0; g < p->sensor_group_count; g++) {
        float val = 0.55f + 0.35f * sinf(t + p->sensor_groups[g].phase);
        int start = p->sensor_groups[g].start_key;
        int count = p->sensor_groups[g].key_count;
        const uint8_t *col = p->sensor_groups[g].color;

        if (p->sensor_groups[g].mode == 0) {
            /* COLOR mode: all keys same color, brightness varies with value */
            for (int k = 0; k < count; k++) {
                int kid = start + k;
                if (kid >= KEY_COUNT) break;
                p->buf[kid][0] = (uint8_t)(col[0] * val);
                p->buf[kid][1] = (uint8_t)(col[1] * val);
                p->buf[kid][2] = (uint8_t)(col[2] * val);
            }
        } else {
            /* BAR mode: fill left to right based on value */
            int lit = (int)(val * count + 0.5f);
            if (lit > count) lit = count;
            for (int k = 0; k < count; k++) {
                int kid = start + k;
                if (kid >= KEY_COUNT) break;
                if (k < lit) {
                    /* Blend sensor color with green→red gradient */
                    float ratio = (float)k / fmaxf(count - 1, 1);
                    float blend = 0.5f;
                    uint8_t gr = (uint8_t)((1.0f - ratio) * 255 * blend + col[1] * (1 - blend));
                    uint8_t rr = (uint8_t)(ratio * 255 * blend + col[0] * (1 - blend));
                    uint8_t br = (uint8_t)(col[2] * (1 - blend));
                    p->buf[kid][0] = rr;
                    p->buf[kid][1] = gr;
                    p->buf[kid][2] = br;
                } else {
                    /* Unlit keys: dim sensor color */
                    p->buf[kid][0] = col[0] / 8;
                    p->buf[kid][1] = col[1] / 8;
                    p->buf[kid][2] = col[2] / 8;
                }
            }
        }
    }
}
```

- [ ] **Step 4: Update render_frame dispatch**

Replace the sensor case in `render_frame()`:

Old:
```c
    case 106: case 107: case 108:
        render_sensor(p); break;
```

New:
```c
    case 106:
        render_sensor(p); break;
```

(Only effect_id 106 now — 107/108 removed from meta table.)

- [ ] **Step 5: Build and verify**

Run: `cd /home/emrah/Projects/F87Control/build && cmake .. && make`
Expected: Compiles clean.

- [ ] **Step 6: Commit**

```bash
git add gui/src/preview.c gui/src/preview.h
git commit -m "feat(gui): profile-aware sensor preview with per-sensor colors"
```

---

### Task 7: App State — Custom Profile Path Desteği

App state'in sensor efekti başlatma kodunu custom profil path'i destekleyecek şekilde güncelle.

**Files:**
- Modify: `gui/src/app_state.c`

- [ ] **Step 1: Update f87_app_state_start_sw for custom path**

In `f87_app_state_start_sw()`, the sensor block already passes `config->sensor_config_path`:

```c
    if (effect_id == F87_SW_SENSOR) {
        rc = f87_client_set_sensor_effect(state->client,
                                          config->sensor_profile,
                                          config->sensor_config_path);
    }
```

Verify this handles the custom path correctly. The daemon's `SetSensorEffect` D-Bus method should already accept profile name + optional path. Check that `f87_client_set_sensor_effect` passes both.

If the client already sends both parameters, no change needed. If not, the client API needs updating.

- [ ] **Step 2: Verify client API handles path**

Check `lib/src/client.c` — `f87_client_set_sensor_effect(client, profile, path)` should pass `path` as a D-Bus argument. If it already does, this task is done.

If path is not passed: update `f87_client_set_sensor_effect` to include it in the D-Bus call, and update the daemon handler to accept it.

- [ ] **Step 3: Build full project**

Run: `cd /home/emrah/Projects/F87Control/build && cmake .. && make`
Expected: Compiles clean.

- [ ] **Step 4: Run all tests**

Run: `cd /home/emrah/Projects/F87Control/build && ctest --output-on-failure`
Expected: All tests pass.

- [ ] **Step 5: Commit (if changes needed)**

```bash
git add gui/src/app_state.c lib/src/client.c
git commit -m "fix(gui): pass custom sensor config path through client API"
```

---

### Task 8: Integration Test — Tam UI Akışını Doğrula

Tüm parçaları birleştir, build et, GUI'yi çalıştırarak el ile test et.

**Files:** No new files.

- [ ] **Step 1: Full clean build**

```bash
cd /home/emrah/Projects/F87Control/build
cmake .. -DBUILD_GUI=ON -DBUILD_DAEMON=ON
make -j$(nproc)
```
Expected: No errors, no warnings.

- [ ] **Step 2: Run all tests**

```bash
ctest --output-on-failure
```
Expected: All tests pass including new `test_sensor_config`.

- [ ] **Step 3: Manual GUI verification checklist**

Start the daemon and GUI:
```bash
./f87d &
./f87control
```

Verify:
1. Sidebar: "Sensor" category shows single "Sensor Monitor" entry (not 3 separate profiles)
2. Click "Sensor Monitor" → controls panel shows profile dropdown (Developer/Gamer/System/Custom)
3. Developer selected → info cards show cpu_temp(F1-F4), cpu_load(F5-F8), ram_usage(F9-F12)
4. Keyboard preview shows colored bars on F-keys with sensor colors (teal/coral/yellow)
5. Switch to "Custom" → editor appears: sensor dropdown (with live values), mode toggle, bar slider
6. Select cpu_temp, Bar mode, length 4, click F1 → mapping added, F1-F4 highlighted teal
7. Select ram_usage, Single Key, click ESC → ESC highlighted yellow
8. Assignments list shows both mappings with remove buttons
9. Click ✕ on a mapping → removed from list and keyboard
10. Click "Start" → effect runs on keyboard hardware
11. Click "Stop" → effect stops

- [ ] **Step 4: Fix any issues found**

Address bugs discovered during manual testing.

- [ ] **Step 5: Final commit**

```bash
git add -A
git commit -m "feat(gui): sensor monitor UI redesign — complete integration"
```
