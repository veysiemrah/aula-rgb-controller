#include "controls.h"
#include "effect_meta.h"
#include "preview.h"
#include "sensor_editor.h"
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
    gboolean sw_running;  /* direct mode animation active */

    F87KeyboardView *keyboard;
    f87_preview_t *preview;
    F87SensorEditor *sensor_editor;
};

static void update_status(F87Controls *ctrl, const char *text)
{
    if (ctrl->status_cb)
        ctrl->status_cb(text, ctrl->user_data);
}

/* Forward declarations for live update */
static gboolean is_direct_mode_effect(F87Controls *ctrl);
static int send_sw_effect(F87Controls *ctrl);
static void update_button_state(F87Controls *ctrl);

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

    /* Live update running SW effect on keyboard */
    if (ctrl->sw_running && is_direct_mode_effect(ctrl)) {
        if (send_sw_effect(ctrl) < 0) {
            ctrl->sw_running = FALSE;
            update_button_state(ctrl);
        }
    }
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

static void on_brightness_changed(GtkRange *range, gpointer data)
{
    (void)range;
    F87Controls *ctrl = data;
    if (ctrl->sw_running && is_direct_mode_effect(ctrl))
        send_sw_effect(ctrl);
}

static void on_speed_changed(GtkRange *range, gpointer data)
{
    F87Controls *ctrl = data;
    if (ctrl->preview)
        f87_preview_set_speed(ctrl->preview, (uint8_t)gtk_range_get_value(range));

    /* Live update running SW effect */
    if (ctrl->sw_running && is_direct_mode_effect(ctrl))
        send_sw_effect(ctrl);
}

static void on_gain_changed(GtkRange *range, gpointer data)
{
    (void)range;
    F87Controls *ctrl = data;
    if (ctrl->sw_running && is_direct_mode_effect(ctrl))
        send_sw_effect(ctrl);
}

static void on_auto_gain_changed(GtkSwitch *sw, GParamSpec *pspec, gpointer data)
{
    (void)pspec;
    F87Controls *ctrl = data;

    /* Toggle gain slider visibility */
    GtkWidget *gain_slider = g_object_get_data(G_OBJECT(sw), "gain-slider");
    if (gain_slider)
        gtk_widget_set_visible(gain_slider, !gtk_switch_get_active(sw));

    if (ctrl->sw_running && is_direct_mode_effect(ctrl))
        send_sw_effect(ctrl);
}

static void on_colorful_changed(GtkSwitch *sw, GParamSpec *pspec, gpointer data)
{
    (void)pspec;
    F87Controls *ctrl = data;
    if (ctrl->preview)
        f87_preview_set_colorful(ctrl->preview, gtk_switch_get_active(sw));

    if (ctrl->sw_running && is_direct_mode_effect(ctrl))
        send_sw_effect(ctrl);
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

static void on_key_demo(int key_id, gpointer user_data)
{
    F87Controls *ctrl = user_data;
    if (ctrl->preview)
        f87_preview_on_key(ctrl->preview, key_id);
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

/* ===== Helper: is current effect direct-mode (SW/music/sensor)? ===== */

static gboolean is_direct_mode_effect(F87Controls *ctrl)
{
    return strcmp(ctrl->category, "hw") != 0;
}

/* ===== Update button label/style based on state ===== */

static void update_button_state(F87Controls *ctrl)
{
    if (is_direct_mode_effect(ctrl)) {
        if (ctrl->sw_running) {
            gtk_button_set_label(ctrl->send_button, _("Stop"));
            gtk_widget_remove_css_class(GTK_WIDGET(ctrl->send_button), "action-button");
            gtk_widget_add_css_class(GTK_WIDGET(ctrl->send_button), "stop-button");
        } else {
            gtk_button_set_label(ctrl->send_button, _("Start"));
            gtk_widget_remove_css_class(GTK_WIDGET(ctrl->send_button), "stop-button");
            gtk_widget_add_css_class(GTK_WIDGET(ctrl->send_button), "action-button");
        }
    } else {
        gtk_button_set_label(ctrl->send_button, _("Save"));
        gtk_widget_remove_css_class(GTK_WIDGET(ctrl->send_button), "stop-button");
        gtk_widget_add_css_class(GTK_WIDGET(ctrl->send_button), "action-button");
    }
}

/* ===== SEND / STOP ===== */

static gboolean on_loading_done(gpointer data)
{
    F87Controls *ctrl = data;
    ctrl->loading_timer = 0;
    gtk_widget_set_sensitive(GTK_WIDGET(ctrl->send_button), TRUE);
    gtk_widget_remove_css_class(GTK_WIDGET(ctrl->send_button), "loading");
    update_button_state(ctrl);
    return G_SOURCE_REMOVE;
}

static int send_sw_effect(F87Controls *ctrl)
{
    uint8_t brightness = ctrl->brightness_scale ?
                         (uint8_t)gtk_range_get_value(GTK_RANGE(ctrl->brightness_scale)) : 3;
    uint8_t speed = ctrl->speed_scale ?
                    (uint8_t)gtk_range_get_value(GTK_RANGE(ctrl->speed_scale)) : 2;
    const effect_meta_t *meta = effect_meta_lookup(ctrl->effect_id);

    f87_anim_config_t config = {0};
    gboolean colorful_on = ctrl->colorful_switch &&
                           gtk_switch_get_active(ctrl->colorful_switch);
    if (!colorful_on) {
        config.color[0] = ctrl->selected_color[0];
        config.color[1] = ctrl->selected_color[1];
        config.color[2] = ctrl->selected_color[2];
    }
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

    if (meta->flags & F87_PARAM_SENSOR) {
        if (ctrl->sensor_editor) {
            const char *path = NULL;
            config.sensor_profile = f87_sensor_editor_get_profile(
                ctrl->sensor_editor, &path);
            config.sensor_config_path = path;
            config.brightness = f87_sensor_editor_get_brightness(ctrl->sensor_editor);
        }
    }

    /* Sensor effects all use F87_SW_SENSOR (106) */
    int eid = ctrl->effect_id;
    if (meta->flags & F87_PARAM_SENSOR)
        eid = F87_SW_SENSOR;

    return f87_app_state_start_sw(ctrl->state, eid, &config);
}

static void do_send(F87Controls *ctrl)
{
    const effect_meta_t *meta = effect_meta_lookup(ctrl->effect_id);
    int rc = -1;

    if (meta->flags & F87_PARAM_PAINT) {
        /* Custom per-key: one-shot send */
        if (ctrl->keyboard) {
            const uint8_t (*colors)[3] = f87_keyboard_view_get_colors(ctrl->keyboard);
            rc = f87_app_state_apply_custom(ctrl->state, colors, 88);
        }
    } else if (strcmp(ctrl->category, "hw") == 0) {
        /* HW effect: one-shot config write */
        uint8_t brightness = ctrl->brightness_scale ?
                             (uint8_t)gtk_range_get_value(GTK_RANGE(ctrl->brightness_scale)) : 3;
        uint8_t speed = ctrl->speed_scale ?
                        (uint8_t)gtk_range_get_value(GTK_RANGE(ctrl->speed_scale)) : 2;
        uint8_t colorful = ctrl->colorful_switch ?
                           gtk_switch_get_active(ctrl->colorful_switch) : 0;
        if (meta->tag && strcmp(meta->tag, "rainbow") == 0)
            colorful = 1;
        rc = f87_app_state_start_hw(ctrl->state, ctrl->effect_id,
                                     brightness, speed, colorful,
                                     ctrl->selected_color[0],
                                     ctrl->selected_color[1],
                                     ctrl->selected_color[2]);
    } else {
        /* SW/Music/Sensor: toggle start/stop */
        if (ctrl->sw_running) {
            /* Stop */
            f87_app_state_stop(ctrl->state);
            ctrl->sw_running = FALSE;
            update_status(ctrl, ctrl->state->status_text);
            update_button_state(ctrl);
            return;
        }
        rc = send_sw_effect(ctrl);
        if (rc == 0) {
            ctrl->sw_running = TRUE;
        } else {
            ctrl->sw_running = FALSE;
            update_button_state(ctrl);
        }
    }

    update_status(ctrl, ctrl->state->status_text);

    if (rc == 0) {
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
    ctrl->sw_running = FALSE;
    update_status(ctrl, ctrl->state->status_text);
    update_button_state(ctrl);
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

    if (ctrl->sensor_editor) {
        f87_sensor_editor_deactivate(ctrl->sensor_editor);
        f87_sensor_editor_destroy(ctrl->sensor_editor);
        ctrl->sensor_editor = NULL;
    }

    ctrl->sv_area = NULL;
    ctrl->hue_scale = NULL;
    ctrl->hex_entry = NULL;
    ctrl->preview_swatch = NULL;

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
        GtkLabel *reactive_label = GTK_LABEL(gtk_label_new(
            _("Click keyboard to preview")));
        gtk_widget_set_opacity(GTK_WIDGET(reactive_label), 0.5);
        gtk_widget_add_css_class(GTK_WIDGET(reactive_label), "caption");
        gtk_widget_set_valign(GTK_WIDGET(reactive_label), GTK_ALIGN_CENTER);
        gtk_box_append(title_row, GTK_WIDGET(reactive_label));
    }
    gtk_box_append(ctrl->params_box, GTK_WIDGET(title_row));

    /* Effect description */
    if (meta->desc) {
        GtkLabel *desc_label = GTK_LABEL(gtk_label_new(_(meta->desc)));
        gtk_label_set_xalign(desc_label, 0);
        gtk_widget_set_opacity(GTK_WIDGET(desc_label), 0.45);
        gtk_widget_add_css_class(GTK_WIDGET(desc_label), "caption");
        gtk_box_append(ctrl->params_box, GTK_WIDGET(desc_label));
    }

    /* No params for Off */
    if (flags == 0) return;

    /* Sensor monitor — uses its own editor widget */
    if (flags & F87_PARAM_SENSOR) {
        ctrl->sensor_editor = f87_sensor_editor_new(ctrl->keyboard, ctrl->state);
        gtk_box_append(ctrl->params_box,
                       f87_sensor_editor_get_widget(ctrl->sensor_editor));
        f87_sensor_editor_activate(ctrl->sensor_editor);

        ctrl->send_button = GTK_BUTTON(gtk_button_new_with_label(_("Start")));
        gtk_widget_add_css_class(GTK_WIDGET(ctrl->send_button), "action-button");
        g_signal_connect(ctrl->send_button, "clicked", G_CALLBACK(on_send_clicked), ctrl);
        gtk_box_append(ctrl->params_box, GTK_WIDGET(ctrl->send_button));

        if (ctrl->preview)
            f87_preview_start(ctrl->preview, ctrl->effect_id, ctrl->category,
                              2, 0, 0, 0);
        return;
    }

    /* Split layout: left params | divider | right color picker */
    GtkBox *split = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8));

    /* LEFT: sliders, switches, dropdowns */
    GtkBox *left = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 4));
    gtk_widget_set_hexpand(GTK_WIDGET(left), TRUE);

    if (flags & F87_PARAM_BRIGHTNESS) {
        gtk_box_append(left, create_slider(_("Brightness"), 1, 4, 3, 1, &ctrl->brightness_scale));
        g_signal_connect(ctrl->brightness_scale, "value-changed", G_CALLBACK(on_brightness_changed), ctrl);
    }

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
        g_signal_connect(ctrl->colorful_switch, "notify::active", G_CALLBACK(on_colorful_changed), ctrl);
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

        const char *tr_sources[] = {_("System Audio"), _("Microphone"), NULL};
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
        g_signal_connect(ctrl->auto_gain_switch, "notify::active", G_CALLBACK(on_auto_gain_changed), ctrl);
        gtk_box_append(gain_box, GTK_WIDGET(ctrl->auto_gain_switch));
        gtk_box_append(left, GTK_WIDGET(gain_box));

        GtkWidget *gain_slider = create_slider(_("Gain"), 1, 10, 3, 1, &ctrl->gain_scale);
        g_signal_connect(ctrl->gain_scale, "value-changed", G_CALLBACK(on_gain_changed), ctrl);
        /* Hide gain slider when auto gain is active */
        gtk_widget_set_visible(gain_slider, FALSE);
        g_object_set_data(G_OBJECT(ctrl->auto_gain_switch), "gain-slider", gain_slider);
        gtk_box_append(left, gain_slider);
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
        GtkSeparator *divider = GTK_SEPARATOR(gtk_separator_new(GTK_ORIENTATION_VERTICAL));
        gtk_box_append(split, GTK_WIDGET(divider));

        GtkWidget *picker = create_color_picker(ctrl);
        gtk_box_append(split, picker);
    }

    gtk_box_append(ctrl->params_box, GTK_WIDGET(split));

    /* Enable paint mode */
    if ((flags & F87_PARAM_PAINT) && ctrl->keyboard) {
        if (ctrl->preview)
            f87_preview_stop(ctrl->preview);
        f87_keyboard_view_clear(ctrl->keyboard);
        f87_keyboard_view_set_paint_mode(ctrl->keyboard, TRUE, on_key_painted, ctrl);
    }

    /* Enable reactive demo mode — click keys to trigger effect */
    if (f87_preview_is_reactive(ctrl->effect_id) && ctrl->keyboard) {
        f87_keyboard_view_set_paint_mode(ctrl->keyboard, TRUE, on_key_demo, ctrl);
    }
}

/* ===== PUBLIC API ===== */

void f87_controls_set_effect(F87Controls *ctrl, const char *category,
                              const char *effect_name, int effect_id)
{
    gboolean was_direct = is_direct_mode_effect(ctrl) && ctrl->sw_running;

    strncpy(ctrl->category, category, sizeof(ctrl->category) - 1);
    strncpy(ctrl->effect_name, effect_name, sizeof(ctrl->effect_name) - 1);
    ctrl->effect_id = effect_id;

    if (ctrl->keyboard)
        f87_keyboard_view_set_paint_mode(ctrl->keyboard, FALSE, NULL, NULL);

    clear_params(ctrl);
    build_controls_for_effect(ctrl);

    /* If direct mode was running and new effect is also direct-mode,
     * auto-apply immediately (hot-switch without stopping) */
    if (was_direct && is_direct_mode_effect(ctrl)) {
        int rc = send_sw_effect(ctrl);
        if (rc == 0) {
            ctrl->sw_running = TRUE;
            update_status(ctrl, ctrl->state->status_text);
        } else {
            ctrl->sw_running = FALSE;
        }
    } else if (was_direct && !is_direct_mode_effect(ctrl)) {
        /* Switching from SW to HW — stop direct mode */
        f87_app_state_stop(ctrl->state);
        ctrl->sw_running = FALSE;
    }

    gtk_widget_set_sensitive(GTK_WIDGET(ctrl->send_button), TRUE);
    update_button_state(ctrl);

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
            int cf = ctrl->colorful_switch ?
                     gtk_switch_get_active(ctrl->colorful_switch) : 0;
            f87_preview_set_colorful(ctrl->preview, cf);
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

void f87_controls_reset_sw_state(F87Controls *ctrl)
{
    ctrl->sw_running = FALSE;
    update_button_state(ctrl);
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

    /* Stop button removed — send button toggles Start/Stop for SW effects */

    gtk_box_append(ctrl->container, GTK_WIDGET(btn_box));

    return ctrl;
}
