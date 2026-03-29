#include "controls.h"
#include <string.h>
#include <stdio.h>

/* Preset color palette */
static const uint8_t preset_colors[][3] = {
    {255,   0,   0}, {255,  80,   0}, {255, 165,   0}, {255, 255,   0},
    {  0, 255,   0}, {  0, 255, 128}, {  0, 255, 255}, {  0, 128, 255},
    {  0,   0, 255}, {128,   0, 255}, {255,   0, 255}, {255,   0, 128},
    {255, 255, 255}, {200, 200, 200}, {128, 128, 128}, {255,  50,  50},
};
#define NUM_PRESETS 16

struct _F87Controls {
    f87_app_state_t *state;
    F87ControlsStatusCallback status_cb;
    gpointer user_data;

    GtkBox *container;
    GtkBox *params_box;      /* Dynamic parameter area */

    /* Current selection */
    char category[16];
    char effect_name[64];
    int effect_id;

    /* Widgets (rebuilt per category) */
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

    /* Color */
    uint8_t selected_color[3];
};

static void update_status(F87Controls *ctrl, const char *text)
{
    if (ctrl->status_cb)
        ctrl->status_cb(text, ctrl->user_data);
}

/* ===== COLOR PALETTE ===== */

static void on_color_swatch_clicked(GtkButton *btn, gpointer data)
{
    F87Controls *ctrl = data;
    int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "color-idx"));
    ctrl->selected_color[0] = preset_colors[idx][0];
    ctrl->selected_color[1] = preset_colors[idx][1];
    ctrl->selected_color[2] = preset_colors[idx][2];
}

static void on_custom_color(GtkButton *btn, gpointer data)
{
    (void)btn;
    F87Controls *ctrl = data;

    GtkColorChooserDialog *dialog = GTK_COLOR_CHOOSER_DIALOG(
        gtk_color_chooser_dialog_new("Renk Sec", NULL));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);

    GdkRGBA current = {
        ctrl->selected_color[0] / 255.0,
        ctrl->selected_color[1] / 255.0,
        ctrl->selected_color[2] / 255.0,
        1.0
    };
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(dialog), &current);

    g_signal_connect(dialog, "response", G_CALLBACK(gtk_window_destroy), NULL);
    /* For simplicity, read color on destroy — GTK4 color chooser is complex.
       Use the native dialog's color-activated signal instead. */
    g_signal_connect(dialog, "color-activated",
                     G_CALLBACK(gtk_window_destroy), NULL);

    gtk_window_present(GTK_WINDOW(dialog));
}

static GtkWidget *create_color_palette(F87Controls *ctrl)
{
    GtkBox *box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 4));
    GtkLabel *label = GTK_LABEL(gtk_label_new("Renk"));
    gtk_label_set_xalign(label, 0);
    gtk_widget_set_opacity(GTK_WIDGET(label), 0.7);
    gtk_box_append(box, GTK_WIDGET(label));

    GtkFlowBox *flow = GTK_FLOW_BOX(gtk_flow_box_new());
    gtk_flow_box_set_max_children_per_line(flow, 8);
    gtk_flow_box_set_selection_mode(flow, GTK_SELECTION_NONE);

    for (int i = 0; i < NUM_PRESETS; i++) {
        GtkButton *btn = GTK_BUTTON(gtk_button_new_with_label("\xe2\x96\x88")); /* Unicode full block */
        char markup[128];
        snprintf(markup, sizeof(markup),
                 "<span foreground=\"#%02x%02x%02x\" font=\"16\">\xe2\x96\x88</span>",
                 preset_colors[i][0], preset_colors[i][1], preset_colors[i][2]);
        GtkWidget *child = gtk_button_get_child(btn);
        if (GTK_IS_LABEL(child))
            gtk_label_set_markup(GTK_LABEL(child), markup);

        g_object_set_data(G_OBJECT(btn), "color-idx", GINT_TO_POINTER(i));
        g_signal_connect(btn, "clicked", G_CALLBACK(on_color_swatch_clicked), ctrl);
        gtk_flow_box_append(flow, GTK_WIDGET(btn));
    }

    gtk_box_append(box, GTK_WIDGET(flow));

    GtkButton *custom_btn = GTK_BUTTON(gtk_button_new_with_label("Ozel Renk..."));
    g_signal_connect(custom_btn, "clicked", G_CALLBACK(on_custom_color), ctrl);
    gtk_box_append(box, GTK_WIDGET(custom_btn));

    return GTK_WIDGET(box);
}

/* ===== SLIDERS ===== */

static GtkWidget *create_slider(const char *label_text, double min, double max,
                                 double value, double step, GtkScale **out)
{
    GtkBox *box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8));
    GtkLabel *label = GTK_LABEL(gtk_label_new(label_text));
    gtk_widget_set_size_request(GTK_WIDGET(label), 80, -1);
    gtk_label_set_xalign(label, 0);
    gtk_box_append(box, GTK_WIDGET(label));

    GtkScale *scale = GTK_SCALE(gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
                                                          min, max, step));
    gtk_range_set_value(GTK_RANGE(scale), value);
    gtk_scale_set_draw_value(scale, TRUE);
    gtk_widget_set_hexpand(GTK_WIDGET(scale), TRUE);
    gtk_box_append(box, GTK_WIDGET(scale));

    *out = scale;
    return GTK_WIDGET(box);
}

/* ===== SEND / STOP ===== */

static void on_send_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    F87Controls *ctrl = data;

    uint8_t brightness = (uint8_t)gtk_range_get_value(GTK_RANGE(ctrl->brightness_scale));
    uint8_t speed = ctrl->speed_scale ?
                    (uint8_t)gtk_range_get_value(GTK_RANGE(ctrl->speed_scale)) : 2;

    if (strcmp(ctrl->category, "hw") == 0) {
        uint8_t colorful = ctrl->colorful_switch ?
                           gtk_switch_get_active(ctrl->colorful_switch) : 0;
        int rc = f87_app_state_start_hw(ctrl->state, ctrl->effect_id,
                                         brightness, speed, colorful,
                                         ctrl->selected_color[0],
                                         ctrl->selected_color[1],
                                         ctrl->selected_color[2]);
        (void)rc;
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

        int rc = f87_app_state_start_sw(ctrl->state, ctrl->effect_id, &config);
        (void)rc;
    }

    update_status(ctrl, ctrl->state->status_text);
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
}

static void build_hw_controls(F87Controls *ctrl)
{
    gtk_box_append(ctrl->params_box,
                   create_slider("Parlaklik", 1, 4, 3, 1, &ctrl->brightness_scale));
    gtk_box_append(ctrl->params_box,
                   create_slider("Hiz", 0, 4, 2, 1, &ctrl->speed_scale));
    gtk_box_append(ctrl->params_box, create_color_palette(ctrl));

    /* Colorful toggle */
    GtkBox *cf_box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8));
    GtkLabel *cf_label = GTK_LABEL(gtk_label_new("Renkli mod"));
    gtk_widget_set_size_request(GTK_WIDGET(cf_label), 80, -1);
    gtk_box_append(cf_box, GTK_WIDGET(cf_label));
    ctrl->colorful_switch = GTK_SWITCH(gtk_switch_new());
    gtk_box_append(cf_box, GTK_WIDGET(ctrl->colorful_switch));
    gtk_box_append(ctrl->params_box, GTK_WIDGET(cf_box));
}

static void build_sw_controls(F87Controls *ctrl)
{
    gtk_box_append(ctrl->params_box,
                   create_slider("Parlaklik", 1, 4, 3, 1, &ctrl->brightness_scale));
    gtk_box_append(ctrl->params_box,
                   create_slider("Hiz", 0, 4, 2, 1, &ctrl->speed_scale));
    gtk_box_append(ctrl->params_box, create_color_palette(ctrl));
}

static void build_music_controls(F87Controls *ctrl)
{
    gtk_box_append(ctrl->params_box,
                   create_slider("Parlaklik", 1, 4, 4, 1, &ctrl->brightness_scale));

    /* Audio source dropdown */
    GtkBox *src_box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8));
    GtkLabel *src_label = GTK_LABEL(gtk_label_new("Kaynak"));
    gtk_widget_set_size_request(GTK_WIDGET(src_label), 80, -1);
    gtk_box_append(src_box, GTK_WIDGET(src_label));

    const char *sources[] = {"Sistem Sesi", "Mikrofon", NULL};
    ctrl->source_dropdown = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(sources));
    gtk_box_append(src_box, GTK_WIDGET(ctrl->source_dropdown));
    gtk_box_append(ctrl->params_box, GTK_WIDGET(src_box));

    /* Gain */
    GtkBox *gain_box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8));
    GtkLabel *gain_label = GTK_LABEL(gtk_label_new("Auto Gain"));
    gtk_widget_set_size_request(GTK_WIDGET(gain_label), 80, -1);
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

    GtkBox *prof_box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8));
    GtkLabel *prof_label = GTK_LABEL(gtk_label_new("Profil"));
    gtk_widget_set_size_request(GTK_WIDGET(prof_label), 80, -1);
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

    clear_params(ctrl);

    /* Title */
    char title[128];
    snprintf(title, sizeof(title), "%s", effect_name);
    GtkLabel *title_label = GTK_LABEL(gtk_label_new(title));
    gtk_label_set_xalign(title_label, 0);
    gtk_widget_add_css_class(GTK_WIDGET(title_label), "title-3");
    gtk_box_append(ctrl->params_box, GTK_WIDGET(title_label));

    /* Build category-specific controls */
    if (strcmp(category, "hw") == 0)
        build_hw_controls(ctrl);
    else if (strcmp(category, "sw") == 0)
        build_sw_controls(ctrl);
    else if (strcmp(category, "music") == 0)
        build_music_controls(ctrl);
    else if (strcmp(category, "sensor") == 0)
        build_sensor_controls(ctrl);

    /* Action buttons */
    GtkBox *btn_box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8));
    gtk_widget_set_margin_top(GTK_WIDGET(btn_box), 8);

    ctrl->send_button = GTK_BUTTON(gtk_button_new_with_label("Klavyeye Gonder"));
    gtk_widget_add_css_class(GTK_WIDGET(ctrl->send_button), "action-button");
    g_signal_connect(ctrl->send_button, "clicked", G_CALLBACK(on_send_clicked), ctrl);
    gtk_box_append(btn_box, GTK_WIDGET(ctrl->send_button));

    ctrl->stop_button = GTK_BUTTON(gtk_button_new_with_label("Durdur"));
    gtk_widget_add_css_class(GTK_WIDGET(ctrl->stop_button), "stop-button");
    g_signal_connect(ctrl->stop_button, "clicked", G_CALLBACK(on_stop_clicked), ctrl);
    gtk_box_append(btn_box, GTK_WIDGET(ctrl->stop_button));

    gtk_box_append(ctrl->params_box, GTK_WIDGET(btn_box));
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

    ctrl->container = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
    gtk_widget_add_css_class(GTK_WIDGET(ctrl->container), "controls-panel");
    gtk_widget_set_size_request(GTK_WIDGET(ctrl->container), -1, 250);
    gtk_widget_set_vexpand(GTK_WIDGET(ctrl->container), FALSE);

    ctrl->params_box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 6));
    GtkLabel *placeholder = GTK_LABEL(gtk_label_new("Efekt seciniz"));
    gtk_widget_set_opacity(GTK_WIDGET(placeholder), 0.5);
    gtk_box_append(ctrl->params_box, GTK_WIDGET(placeholder));

    gtk_box_append(ctrl->container, GTK_WIDGET(ctrl->params_box));
    return ctrl;
}
