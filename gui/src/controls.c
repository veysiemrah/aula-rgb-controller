#include "controls.h"
#include "preview.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

/* Preset color palette — compact strip */
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
    GtkDropDown *side_light_dropdown;
    GtkDropDown *sensor_profile_dropdown;
    GtkButton *send_button;
    GtkButton *stop_button;

    /* Color picker state */
    float hue;        /* 0-360 */
    float sat;        /* 0-1 */
    float val;        /* 0-1 */
    uint8_t selected_color[3];
    GtkDrawingArea *sv_area;
    GtkScale *hue_scale;
    GtkEntry *hex_entry;
    GtkWidget *preview_swatch;
    gboolean sv_dragging;

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

    /* Update hex entry */
    if (ctrl->hex_entry) {
        char hex[8];
        snprintf(hex, sizeof(hex), "%02X%02X%02X",
                 ctrl->selected_color[0], ctrl->selected_color[1], ctrl->selected_color[2]);
        gtk_editable_set_text(GTK_EDITABLE(ctrl->hex_entry), hex);
    }

    /* Update preview swatch via CSS provider */
    if (ctrl->preview_swatch) {
        char css[128];
        snprintf(css, sizeof(css), ".color-preview { background: #%02x%02x%02x; }",
                 ctrl->selected_color[0], ctrl->selected_color[1], ctrl->selected_color[2]);
        GtkCssProvider *prov = gtk_css_provider_new();
        gtk_css_provider_load_from_string(prov, css);
        gtk_style_context_add_provider_for_display(
            gtk_widget_get_display(ctrl->preview_swatch),
            GTK_STYLE_PROVIDER(prov),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(prov);
    }

    /* Redraw SV area */
    if (ctrl->sv_area)
        gtk_widget_queue_draw(GTK_WIDGET(ctrl->sv_area));

    /* Update preview color */
    if (ctrl->preview)
        f87_preview_set_color(ctrl->preview,
                               ctrl->selected_color[0],
                               ctrl->selected_color[1],
                               ctrl->selected_color[2]);
}

/* ===== SV GRADIENT AREA (Saturation-Value picker) ===== */

#define SV_SIZE 120

static void sv_draw(GtkDrawingArea *area, cairo_t *cr,
                     int width, int height, gpointer data)
{
    (void)area;
    F87Controls *ctrl = data;

    /* Draw SV gradient for current hue */
    cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, height);
    uint32_t *pixels = (uint32_t *)cairo_image_surface_get_data(surf);
    int stride = cairo_image_surface_get_stride(surf) / 4;

    for (int y = 0; y < height; y++) {
        float v = 1.0f - (float)y / (float)(height - 1);
        for (int x = 0; x < width; x++) {
            float s = (float)x / (float)(width - 1);
            uint8_t r, g, b;
            hsv_to_rgb(ctrl->hue, s, v, &r, &g, &b);
            pixels[y * stride + x] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        }
    }
    cairo_surface_mark_dirty(surf);
    cairo_set_source_surface(cr, surf, 0, 0);
    cairo_paint(cr);
    cairo_surface_destroy(surf);

    /* Draw selector circle */
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
    F87Controls *ctrl = data;
    ctrl->sv_dragging = FALSE;
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

/* ===== HUE SLIDER ===== */

static void on_hue_changed(GtkRange *range, gpointer data)
{
    F87Controls *ctrl = data;
    ctrl->hue = (float)gtk_range_get_value(range);
    sync_color_from_hsv(ctrl);
}

/* ===== HEX INPUT ===== */

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

/* ===== PRESET SWATCH CLICK ===== */

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

/* ===== SLIDERS ===== */

static GtkWidget *create_slider(const char *label_text, double min, double max,
                                 double value, double step, GtkScale **out)
{
    GtkBox *box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2));
    GtkLabel *label = GTK_LABEL(gtk_label_new(label_text));
    gtk_widget_set_size_request(GTK_WIDGET(label), 30, -1);
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

/* ===== SPEED CHANGE -> PREVIEW UPDATE ===== */

static void on_speed_changed(GtkRange *range, gpointer data)
{
    F87Controls *ctrl = data;
    if (ctrl->preview)
        f87_preview_set_speed(ctrl->preview, (uint8_t)gtk_range_get_value(range));
}

/* ===== CUSTOM PAINT CALLBACK ===== */

static void on_key_painted(int key_id, gpointer user_data)
{
    F87Controls *ctrl = user_data;
    if (!ctrl->keyboard) return;
    f87_keyboard_view_set_key(ctrl->keyboard, key_id,
                               ctrl->selected_color[0],
                               ctrl->selected_color[1],
                               ctrl->selected_color[2]);
}

/* ===== SEND / STOP with loading animation ===== */

static gboolean on_loading_done(gpointer data)
{
    F87Controls *ctrl = data;
    ctrl->loading_timer = 0;

    gtk_button_set_label(ctrl->send_button, "Kaydet");
    gtk_widget_set_sensitive(GTK_WIDGET(ctrl->send_button), TRUE);
    gtk_widget_remove_css_class(GTK_WIDGET(ctrl->send_button), "loading");

    return G_SOURCE_REMOVE;
}

static void on_send_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    F87Controls *ctrl = data;

    uint8_t brightness = (uint8_t)gtk_range_get_value(GTK_RANGE(ctrl->brightness_scale));
    uint8_t speed = ctrl->speed_scale ?
                    (uint8_t)gtk_range_get_value(GTK_RANGE(ctrl->speed_scale)) : 2;

    int rc = -1;
    if (strcmp(ctrl->category, "hw") == 0 && ctrl->effect_id == 18) {
        /* Custom per-key mode — send keyboard view colors */
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

        if (strcmp(ctrl->category, "music") == 0) {
            guint src_idx = ctrl->source_dropdown ?
                            gtk_drop_down_get_selected(ctrl->source_dropdown) : 0;
            config.audio_source = (f87_audio_source_t)src_idx;

            if (ctrl->auto_gain_switch && gtk_switch_get_active(ctrl->auto_gain_switch))
                config.gain = 0;
            else if (ctrl->gain_scale)
                config.gain = (float)gtk_range_get_value(GTK_RANGE(ctrl->gain_scale));
        }

        if (strcmp(ctrl->category, "sensor") == 0) {
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
        gtk_button_set_label(ctrl->send_button, "Kaydediliyor...");
        gtk_widget_set_sensitive(GTK_WIDGET(ctrl->send_button), FALSE);
        gtk_widget_add_css_class(GTK_WIDGET(ctrl->send_button), "loading");

        if (ctrl->loading_timer)
            g_source_remove(ctrl->loading_timer);
        ctrl->loading_timer = g_timeout_add(2000, on_loading_done, ctrl);
    }
}

static void on_stop_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    F87Controls *ctrl = data;
    f87_app_state_stop(ctrl->state);
    update_status(ctrl, ctrl->state->status_text);
}

/* ===== DYNAMIC PANEL BUILDER ===== */

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
    ctrl->side_light_dropdown = NULL;
    ctrl->sensor_profile_dropdown = NULL;
    ctrl->sv_area = NULL;
    ctrl->hue_scale = NULL;
    ctrl->hex_entry = NULL;
    ctrl->preview_swatch = NULL;
}

/* Left column: sliders + toggles. Right column: color picker. */
static GtkWidget *build_split_layout(F87Controls *ctrl, gboolean show_colorful)
{
    GtkBox *row = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8));

    /* Left: sliders stacked vertically */
    GtkBox *left = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 4));
    gtk_widget_set_hexpand(GTK_WIDGET(left), TRUE);

    gtk_box_append(left, create_slider("Prk", 1, 4, 3, 1, &ctrl->brightness_scale));
    gtk_box_append(left, create_slider("Hiz", 0, 4, 2, 1, &ctrl->speed_scale));
    g_signal_connect(ctrl->speed_scale, "value-changed", G_CALLBACK(on_speed_changed), ctrl);

    if (show_colorful) {
        GtkBox *cf_box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4));
        GtkLabel *cf_label = GTK_LABEL(gtk_label_new("Renkli"));
        gtk_widget_set_opacity(GTK_WIDGET(cf_label), 0.7);
        gtk_box_append(cf_box, GTK_WIDGET(cf_label));
        ctrl->colorful_switch = GTK_SWITCH(gtk_switch_new());
        gtk_widget_set_margin_start(GTK_WIDGET(ctrl->colorful_switch), 4);
        gtk_box_append(cf_box, GTK_WIDGET(ctrl->colorful_switch));
        gtk_box_append(left, GTK_WIDGET(cf_box));
    }

    /* Hex input + preview under sliders */
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

    gtk_box_append(left, GTK_WIDGET(hex_row));

    /* Preset swatches under hex */
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

    /* Load preset color CSS (idempotent — CSS class names are static) */
    {
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
    }

    gtk_box_append(left, GTK_WIDGET(flow));

    gtk_box_append(row, GTK_WIDGET(left));

    /* Right: SV area + hue slider (compact, no hex/presets) */
    GtkBox *right = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 2));

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

    ctrl->hue_scale = GTK_SCALE(gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
                                                           0, 359, 1));
    gtk_range_set_value(GTK_RANGE(ctrl->hue_scale), ctrl->hue);
    gtk_scale_set_draw_value(ctrl->hue_scale, FALSE);
    gtk_widget_add_css_class(GTK_WIDGET(ctrl->hue_scale), "hue-slider");
    g_signal_connect(ctrl->hue_scale, "value-changed", G_CALLBACK(on_hue_changed), ctrl);
    gtk_box_append(right, GTK_WIDGET(ctrl->hue_scale));

    gtk_box_append(row, GTK_WIDGET(right));

    sync_color_from_hsv(ctrl);

    return GTK_WIDGET(row);
}

static void build_custom_controls(F87Controls *ctrl)
{
    /* Custom mode: only color picker + brightness, no speed/colorful */
    GtkBox *row = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8));

    GtkBox *left = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 4));
    gtk_widget_set_hexpand(GTK_WIDGET(left), TRUE);

    gtk_box_append(left, create_slider("Prk", 1, 4, 3, 1, &ctrl->brightness_scale));

    GtkLabel *hint = GTK_LABEL(gtk_label_new("Renk sec, tuslarini tiklayarak boya"));
    gtk_label_set_xalign(hint, 0);
    gtk_widget_set_opacity(GTK_WIDGET(hint), 0.5);
    gtk_widget_add_css_class(GTK_WIDGET(hint), "caption");
    gtk_box_append(left, GTK_WIDGET(hint));

    /* Hex input + preview */
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

    gtk_box_append(left, GTK_WIDGET(hex_row));

    /* Presets */
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
    {
        GString *pcss = g_string_new("");
        for (int i = 0; i < NUM_PRESETS; i++)
            g_string_append_printf(pcss,
                ".preset-%d { background: #%02x%02x%02x; min-width: 14px; "
                "min-height: 14px; padding: 0; border-radius: 2px; }\n",
                i, preset_colors[i][0], preset_colors[i][1], preset_colors[i][2]);
        GtkCssProvider *pprov = gtk_css_provider_new();
        gtk_css_provider_load_from_string(pprov, pcss->str);
        gtk_style_context_add_provider_for_display(
            gdk_display_get_default(), GTK_STYLE_PROVIDER(pprov),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(pprov);
        g_string_free(pcss, TRUE);
    }
    gtk_box_append(left, GTK_WIDGET(flow));
    gtk_box_append(row, GTK_WIDGET(left));

    /* Right: SV + hue */
    GtkBox *right = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 2));

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

    ctrl->hue_scale = GTK_SCALE(gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 359, 1));
    gtk_range_set_value(GTK_RANGE(ctrl->hue_scale), ctrl->hue);
    gtk_scale_set_draw_value(ctrl->hue_scale, FALSE);
    gtk_widget_add_css_class(GTK_WIDGET(ctrl->hue_scale), "hue-slider");
    g_signal_connect(ctrl->hue_scale, "value-changed", G_CALLBACK(on_hue_changed), ctrl);
    gtk_box_append(right, GTK_WIDGET(ctrl->hue_scale));

    gtk_box_append(row, GTK_WIDGET(right));

    sync_color_from_hsv(ctrl);
    gtk_box_append(ctrl->params_box, GTK_WIDGET(row));

    /* Enable paint mode on keyboard */
    if (ctrl->keyboard) {
        f87_keyboard_view_clear(ctrl->keyboard);
        f87_keyboard_view_set_paint_mode(ctrl->keyboard, TRUE, on_key_painted, ctrl);
    }
}

static void build_hw_controls(F87Controls *ctrl)
{
    if (ctrl->effect_id == 18) {
        build_custom_controls(ctrl);
        return;
    }
    gtk_box_append(ctrl->params_box, build_split_layout(ctrl, TRUE));
}

static void build_sw_controls(F87Controls *ctrl)
{
    gtk_box_append(ctrl->params_box, build_split_layout(ctrl, FALSE));
}

static void build_music_controls(F87Controls *ctrl)
{
    gtk_box_append(ctrl->params_box,
                   create_slider("Parlaklik", 1, 4, 4, 1, &ctrl->brightness_scale));

    GtkBox *src_box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4));
    GtkLabel *src_label = GTK_LABEL(gtk_label_new("Kaynak"));
    gtk_widget_set_size_request(GTK_WIDGET(src_label), 50, -1);
    gtk_widget_set_opacity(GTK_WIDGET(src_label), 0.7);
    gtk_box_append(src_box, GTK_WIDGET(src_label));

    const char *sources[] = {"Sistem Sesi", "Mikrofon", NULL};
    ctrl->source_dropdown = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(sources));
    gtk_box_append(src_box, GTK_WIDGET(ctrl->source_dropdown));
    gtk_box_append(ctrl->params_box, GTK_WIDGET(src_box));

    GtkBox *gain_box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4));
    GtkLabel *gain_label = GTK_LABEL(gtk_label_new("Auto"));
    gtk_widget_set_size_request(GTK_WIDGET(gain_label), 50, -1);
    gtk_widget_set_opacity(GTK_WIDGET(gain_label), 0.7);
    gtk_box_append(gain_box, GTK_WIDGET(gain_label));
    ctrl->auto_gain_switch = GTK_SWITCH(gtk_switch_new());
    gtk_switch_set_active(ctrl->auto_gain_switch, TRUE);
    gtk_box_append(gain_box, GTK_WIDGET(ctrl->auto_gain_switch));
    gtk_box_append(ctrl->params_box, GTK_WIDGET(gain_box));

    gtk_box_append(ctrl->params_box,
                   create_slider("Gain", 1, 10, 3, 1, &ctrl->gain_scale));
}

static void build_sensor_controls(F87Controls *ctrl)
{
    gtk_box_append(ctrl->params_box,
                   create_slider("Parlaklik", 1, 4, 3, 1, &ctrl->brightness_scale));

    GtkBox *prof_box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4));
    GtkLabel *prof_label = GTK_LABEL(gtk_label_new("Profil"));
    gtk_widget_set_size_request(GTK_WIDGET(prof_label), 50, -1);
    gtk_widget_set_opacity(GTK_WIDGET(prof_label), 0.7);
    gtk_box_append(prof_box, GTK_WIDGET(prof_label));

    const char *profiles[] = {"Developer", "Gamer", "System", NULL};
    ctrl->sensor_profile_dropdown = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(profiles));
    gtk_box_append(prof_box, GTK_WIDGET(ctrl->sensor_profile_dropdown));
    gtk_box_append(ctrl->params_box, GTK_WIDGET(prof_box));
}

/* ===== PUBLIC API ===== */

void f87_controls_set_effect(F87Controls *ctrl, const char *category,
                              const char *effect_name, int effect_id)
{
    strncpy(ctrl->category, category, sizeof(ctrl->category) - 1);
    strncpy(ctrl->effect_name, effect_name, sizeof(ctrl->effect_name) - 1);
    ctrl->effect_id = effect_id;

    /* Disable paint mode from previous custom selection */
    if (ctrl->keyboard)
        f87_keyboard_view_set_paint_mode(ctrl->keyboard, FALSE, NULL, NULL);

    clear_params(ctrl);

    /* Title + reactive indicator */
    GtkBox *title_row = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6));
    GtkLabel *title_label = GTK_LABEL(gtk_label_new(effect_name));
    gtk_label_set_xalign(title_label, 0);
    gtk_widget_add_css_class(GTK_WIDGET(title_label), "title-4");
    gtk_box_append(title_row, GTK_WIDGET(title_label));

    if (f87_preview_is_reactive(effect_id)) {
        GtkLabel *reactive_label = GTK_LABEL(gtk_label_new("tus basmaya duyarli"));
        gtk_widget_set_opacity(GTK_WIDGET(reactive_label), 0.5);
        gtk_widget_add_css_class(GTK_WIDGET(reactive_label), "caption");
        gtk_widget_set_valign(GTK_WIDGET(reactive_label), GTK_ALIGN_CENTER);
        gtk_box_append(title_row, GTK_WIDGET(reactive_label));
    }
    gtk_box_append(ctrl->params_box, GTK_WIDGET(title_row));

    if (strcmp(category, "hw") == 0)
        build_hw_controls(ctrl);
    else if (strcmp(category, "sw") == 0)
        build_sw_controls(ctrl);
    else if (strcmp(category, "music") == 0)
        build_music_controls(ctrl);
    else if (strcmp(category, "sensor") == 0)
        build_sensor_controls(ctrl);

    if (ctrl->stop_button) {
        gboolean show_stop = strcmp(category, "hw") != 0;
        gtk_widget_set_visible(GTK_WIDGET(ctrl->stop_button), show_stop);
    }

    gtk_button_set_label(ctrl->send_button, "Kaydet");
    gtk_widget_set_sensitive(GTK_WIDGET(ctrl->send_button), TRUE);

    /* Start preview animation (not for Custom paint mode) */
    if (ctrl->preview) {
        uint8_t spd = ctrl->speed_scale ?
                       (uint8_t)gtk_range_get_value(GTK_RANGE(ctrl->speed_scale)) : 2;
        f87_preview_start(ctrl->preview, effect_id, category, spd,
                           ctrl->selected_color[0],
                           ctrl->selected_color[1],
                           ctrl->selected_color[2]);
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
    GtkLabel *placeholder = GTK_LABEL(gtk_label_new("Efekt seciniz"));
    gtk_widget_set_opacity(GTK_WIDGET(placeholder), 0.5);
    gtk_box_append(ctrl->params_box, GTK_WIDGET(placeholder));

    gtk_scrolled_window_set_child(scroll, GTK_WIDGET(ctrl->params_box));
    gtk_box_append(ctrl->container, GTK_WIDGET(scroll));

    GtkBox *btn_box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8));
    gtk_widget_set_margin_top(GTK_WIDGET(btn_box), 4);

    ctrl->send_button = GTK_BUTTON(gtk_button_new_with_label("Kaydet"));
    gtk_widget_add_css_class(GTK_WIDGET(ctrl->send_button), "action-button");
    gtk_widget_set_hexpand(GTK_WIDGET(ctrl->send_button), TRUE);
    g_signal_connect(ctrl->send_button, "clicked", G_CALLBACK(on_send_clicked), ctrl);
    gtk_box_append(btn_box, GTK_WIDGET(ctrl->send_button));

    ctrl->stop_button = GTK_BUTTON(gtk_button_new_with_label("Durdur"));
    gtk_widget_add_css_class(GTK_WIDGET(ctrl->stop_button), "stop-button");
    g_signal_connect(ctrl->stop_button, "clicked", G_CALLBACK(on_stop_clicked), ctrl);
    gtk_box_append(btn_box, GTK_WIDGET(ctrl->stop_button));

    gtk_box_append(ctrl->container, GTK_WIDGET(btn_box));

    return ctrl;
}
