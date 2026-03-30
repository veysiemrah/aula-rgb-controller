#include "sensor_editor.h"
#include "i18n.h"
#include "sensor.h"
#include "sensor_config.h"
#include "protocol.h"
#include <string.h>
#include <stdio.h>

#define MAX_CUSTOM_MAPPINGS 16
#define MAX_SENSORS 4

/* Sensor color palette — fixed per sensor type */
typedef struct {
    const char *name;
    uint8_t color[3];
} sensor_color_t;

static const sensor_color_t sensor_colors[] = {
    {"cpu_temp",  { 78, 205, 196}},   /* teal */
    {"cpu_load",  {255, 107, 107}},   /* coral */
    {"gpu_temp",  {168,  85, 247}},   /* purple */
    {"ram_usage", {255, 230, 109}},   /* yellow */
};

static const uint8_t *get_sensor_color(const char *name)
{
    for (int i = 0; i < MAX_SENSORS; i++) {
        if (strcmp(sensor_colors[i].name, name) == 0)
            return sensor_colors[i].color;
    }
    return sensor_colors[0].color;
}

/* Custom mapping entry (editor state) */
typedef struct {
    char sensor_name[32];
    int mode;           /* 0=color, 1=bar */
    int start_key;
    int bar_length;     /* 1 for color, 2-8 for bar */
} custom_mapping_t;

struct _F87SensorEditor {
    GtkBox *container;
    F87KeyboardView *keyboard;
    f87_app_state_t *state;

    /* Profile dropdown */
    GtkDropDown *profile_dropdown;

    /* Custom editor widgets */
    GtkBox *editor_box;
    GtkDropDown *sensor_dropdown;
    GtkToggleButton *mode_color_btn;
    GtkToggleButton *mode_bar_btn;
    GtkScale *bar_length_scale;
    GtkWidget *bar_length_row;
    GtkBox *mappings_list;
    GtkLabel *hint_label;
    GtkScale *brightness_scale;

    /* Builtin profile info */
    GtkBox *profile_info_box;

    /* Custom state */
    custom_mapping_t mappings[MAX_CUSTOM_MAPPINGS];
    int mapping_count;

    /* Sensor live read state */
    guint sensor_timer;
    void *sensor_ctx[MAX_SENSORS];
    float sensor_values[MAX_SENSORS];
    gboolean sensor_available[MAX_SENSORS];
    char sensor_labels[MAX_SENSORS][48];
    int sensor_count;

    /* Custom profile save path */
    char custom_path[256];
};

/* ===== Forward declarations ===== */

static void rebuild_mappings_list(F87SensorEditor *ed);
static void update_keyboard_overlays(F87SensorEditor *ed);
static void on_sensor_key_clicked(int key_id, gpointer user_data);
static void show_builtin_profile_info(F87SensorEditor *ed, const char *name);

/* ===== Sensor value reading ===== */

static void init_sensor_contexts(F87SensorEditor *ed)
{
    ed->sensor_count = f87_sensor_count();
    if (ed->sensor_count > MAX_SENSORS) ed->sensor_count = MAX_SENSORS;

    for (int i = 0; i < ed->sensor_count; i++) {
        const f87_sensor_t *s = f87_sensor_get(i);
        ed->sensor_available[i] = (s->init(&ed->sensor_ctx[i]) == 0);
        ed->sensor_values[i] = -1;
    }
}

static void destroy_sensor_contexts(F87SensorEditor *ed)
{
    for (int i = 0; i < ed->sensor_count; i++) {
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
    for (int i = 0; i < ed->sensor_count; i++) {
        const f87_sensor_t *s = f87_sensor_get(i);
        if (ed->sensor_available[i]) {
            float raw = s->read(ed->sensor_ctx[i]);
            ed->sensor_values[i] = f87_sensor_normalize(raw, s->min_value, s->max_value);
            const char *unit = (s->max_value > 101) ? "\302\260C" : "%";
            snprintf(ed->sensor_labels[i], sizeof(ed->sensor_labels[i]),
                     "%s (%.0f%s)", s->description, raw, unit);
        } else {
            snprintf(ed->sensor_labels[i], sizeof(ed->sensor_labels[i]),
                     "%s (%s)", s->description, _("unavailable"));
        }
    }
}

/* ===== Keyboard overlay ===== */

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

            if (k == 0) {
                /* Show value on first key */
                const f87_sensor_t *s = f87_sensor_find(cm->sensor_name);
                if (s) {
                    int idx = -1;
                    for (int i = 0; i < ed->sensor_count; i++) {
                        if (f87_sensor_get(i) == s) { idx = i; break; }
                    }
                    if (idx >= 0 && ed->sensor_available[idx]) {
                        float raw = ed->sensor_values[idx] *
                                    (s->max_value - s->min_value) + s->min_value;
                        const char *unit = (s->max_value > 101) ? "C" : "%";
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

/* ===== Key collision check ===== */

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

/* ===== Keyboard click handler ===== */

static void on_sensor_key_clicked(int key_id, gpointer user_data)
{
    F87SensorEditor *ed = user_data;
    if (ed->mapping_count >= MAX_CUSTOM_MAPPINGS) return;

    guint idx = gtk_drop_down_get_selected(ed->sensor_dropdown);
    if ((int)idx >= ed->sensor_count) return;

    const f87_sensor_t *s = f87_sensor_get((int)idx);
    if (!s || !ed->sensor_available[idx]) return;

    gboolean bar_mode = gtk_toggle_button_get_active(ed->mode_bar_btn);
    int bar_len = bar_mode ?
                  (int)gtk_range_get_value(GTK_RANGE(ed->bar_length_scale)) : 1;

    /* Check collisions */
    for (int k = 0; k < bar_len; k++) {
        int kid = key_id + k;
        if (kid >= F87_KEY_COUNT) {
            gtk_label_set_text(ed->hint_label,
                               _("Not enough keys — choose a different position"));
            return;
        }
        if (is_key_assigned(ed, kid)) {
            gtk_label_set_text(ed->hint_label,
                               _("Key already assigned — remove it first"));
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

/* ===== Remove mapping ===== */

static void on_remove_mapping(GtkButton *btn, gpointer user_data)
{
    F87SensorEditor *ed = user_data;
    int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "mapping-index"));
    if (idx < 0 || idx >= ed->mapping_count) return;

    for (int i = idx; i < ed->mapping_count - 1; i++)
        ed->mappings[i] = ed->mappings[i + 1];
    ed->mapping_count--;

    rebuild_mappings_list(ed);
    update_keyboard_overlays(ed);
}

/* ===== Rebuild mappings list UI ===== */

static void rebuild_mappings_list(F87SensorEditor *ed)
{
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(GTK_WIDGET(ed->mappings_list))))
        gtk_box_remove(ed->mappings_list, child);

    for (int i = 0; i < ed->mapping_count; i++) {
        custom_mapping_t *cm = &ed->mappings[i];
        const uint8_t *col = get_sensor_color(cm->sensor_name);

        GtkBox *row = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6));

        /* Sensor name (colored via markup) */
        char markup[128];
        snprintf(markup, sizeof(markup),
                 "<span foreground=\"#%02x%02x%02x\" font_weight=\"bold\">%s</span>",
                 col[0], col[1], col[2], cm->sensor_name);
        GtkLabel *name_lbl = GTK_LABEL(gtk_label_new(NULL));
        gtk_label_set_markup(name_lbl, markup);
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
            snprintf(range, sizeof(range), "%s \xe2\x86\x92 %s",
                     start_name, f87_key_layout[end_key].name);
        }
        GtkLabel *range_lbl = GTK_LABEL(gtk_label_new(range));
        gtk_widget_set_opacity(GTK_WIDGET(range_lbl), 0.6);
        gtk_box_append(row, GTK_WIDGET(range_lbl));

        /* Mode tag */
        char mode_tag[24];
        if (cm->mode == 1)
            snprintf(mode_tag, sizeof(mode_tag), "%s (%d)", _("bar"), cm->bar_length);
        else
            snprintf(mode_tag, sizeof(mode_tag), "%s", _("color"));
        GtkLabel *mode_lbl = GTK_LABEL(gtk_label_new(mode_tag));
        gtk_widget_set_opacity(GTK_WIDGET(mode_lbl), 0.4);
        gtk_widget_add_css_class(GTK_WIDGET(mode_lbl), "caption");
        gtk_box_append(row, GTK_WIDGET(mode_lbl));

        /* Spacer */
        GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_set_hexpand(spacer, TRUE);
        gtk_box_append(row, spacer);

        /* Remove button */
        GtkButton *rm_btn = GTK_BUTTON(gtk_button_new_with_label("\xe2\x9c\x95"));
        gtk_widget_add_css_class(GTK_WIDGET(rm_btn), "flat");
        gtk_widget_add_css_class(GTK_WIDGET(rm_btn), "circular");
        g_object_set_data(G_OBJECT(rm_btn), "mapping-index", GINT_TO_POINTER(i));
        g_signal_connect(rm_btn, "clicked", G_CALLBACK(on_remove_mapping), ed);
        gtk_box_append(row, GTK_WIDGET(rm_btn));

        gtk_box_append(ed->mappings_list, GTK_WIDGET(row));
    }
}

/* ===== Builtin profile info display ===== */

static void show_builtin_profile_info(F87SensorEditor *ed, const char *profile_name)
{
    gtk_widget_set_visible(GTK_WIDGET(ed->editor_box), FALSE);

    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(GTK_WIDGET(ed->profile_info_box))))
        gtk_box_remove(ed->profile_info_box, child);

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
    gtk_widget_set_margin_top(GTK_WIDGET(cards_row), 4);

    for (int i = 0; i < profile.mapping_count; i++) {
        f87_sensor_mapping_t *m = &profile.mappings[i];
        const uint8_t *col = get_sensor_color(m->sensor_name);

        GtkBox *card = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 2));

        char markup[128];
        snprintf(markup, sizeof(markup),
                 "<span foreground=\"#%02x%02x%02x\" font_weight=\"bold\">%s</span>",
                 col[0], col[1], col[2], m->sensor_name);
        GtkLabel *nlbl = GTK_LABEL(gtk_label_new(NULL));
        gtk_label_set_markup(nlbl, markup);
        gtk_box_append(card, GTK_WIDGET(nlbl));

        char info[64];
        if (m->key_count == 1) {
            snprintf(info, sizeof(info), "%s \xc2\xb7 %s",
                     f87_key_layout[m->key_ids[0]].name,
                     m->mode == F87_SENSOR_MODE_BAR ? _("bar") : _("color"));
        } else {
            snprintf(info, sizeof(info), "%s \xe2\x86\x92 %s \xc2\xb7 %s",
                     f87_key_layout[m->key_ids[0]].name,
                     f87_key_layout[m->key_ids[m->key_count - 1]].name,
                     m->mode == F87_SENSOR_MODE_BAR ? _("bar") : _("color"));
        }
        GtkLabel *ilbl = GTK_LABEL(gtk_label_new(info));
        gtk_widget_set_opacity(GTK_WIDGET(ilbl), 0.5);
        gtk_widget_add_css_class(GTK_WIDGET(ilbl), "caption");
        gtk_box_append(card, GTK_WIDGET(ilbl));

        gtk_widget_set_hexpand(GTK_WIDGET(card), TRUE);
        gtk_box_append(cards_row, GTK_WIDGET(card));
    }

    gtk_box_append(ed->profile_info_box, GTK_WIDGET(cards_row));
    gtk_widget_set_visible(GTK_WIDGET(ed->profile_info_box), TRUE);

    /* Update overlays for builtin profile */
    ed->mapping_count = 0;
    for (int i = 0; i < profile.mapping_count && i < MAX_CUSTOM_MAPPINGS; i++) {
        custom_mapping_t *cm = &ed->mappings[ed->mapping_count++];
        strncpy(cm->sensor_name, profile.mappings[i].sensor_name,
                sizeof(cm->sensor_name) - 1);
        cm->mode = profile.mappings[i].mode;
        cm->start_key = profile.mappings[i].key_ids[0];
        cm->bar_length = profile.mappings[i].key_count;
    }
    update_keyboard_overlays(ed);

    /* Free profile data */
    for (int i = 0; i < profile.mapping_count; i++)
        free(profile.mappings[i].sensor_name);
}

/* ===== Callbacks ===== */

static void on_profile_changed(GtkDropDown *dropdown, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec;
    F87SensorEditor *ed = user_data;
    guint idx = gtk_drop_down_get_selected(dropdown);

    static const char *profiles[] = {"developer", "gamer", "system"};

    if (idx < 3) {
        show_builtin_profile_info(ed, profiles[idx]);
        f87_keyboard_view_set_click_mode(ed->keyboard, FALSE, NULL, NULL);
    } else {
        gtk_widget_set_visible(GTK_WIDGET(ed->profile_info_box), FALSE);
        gtk_widget_set_visible(GTK_WIDGET(ed->editor_box), TRUE);
        ed->mapping_count = 0;
        rebuild_mappings_list(ed);
        f87_keyboard_view_set_click_mode(ed->keyboard, TRUE,
                                          on_sensor_key_clicked, ed);
        update_keyboard_overlays(ed);
    }
}

static void on_mode_toggled(GtkToggleButton *btn, gpointer user_data)
{
    (void)btn;
    F87SensorEditor *ed = user_data;
    gboolean bar = gtk_toggle_button_get_active(ed->mode_bar_btn);
    gtk_widget_set_visible(ed->bar_length_row, bar);
}

static gboolean on_sensor_timer(gpointer data)
{
    F87SensorEditor *ed = data;
    read_sensor_values(ed);
    update_keyboard_overlays(ed);
    return G_SOURCE_CONTINUE;
}

/* ===== Constructor ===== */

F87SensorEditor *f87_sensor_editor_new(F87KeyboardView *keyboard, f87_app_state_t *state)
{
    F87SensorEditor *ed = g_new0(F87SensorEditor, 1);
    ed->keyboard = keyboard;
    ed->state = state;

    const char *config_dir = g_get_user_config_dir();
    snprintf(ed->custom_path, sizeof(ed->custom_path),
             "%s/f87control/profiles/sensor_custom.json", config_dir);

    init_sensor_contexts(ed);
    read_sensor_values(ed);

    /* Main container */
    ed->container = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 8));

    /* Profile dropdown row */
    GtkBox *prof_row = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8));
    GtkLabel *prof_label = GTK_LABEL(gtk_label_new(_("Profile")));
    gtk_widget_set_size_request(GTK_WIDGET(prof_label), 70, -1);
    gtk_label_set_xalign(prof_label, 0);
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
    gtk_label_set_xalign(br_label, 0);
    gtk_widget_set_opacity(GTK_WIDGET(br_label), 0.7);
    gtk_box_append(br_row, GTK_WIDGET(br_label));
    ed->brightness_scale = GTK_SCALE(gtk_scale_new_with_range(
        GTK_ORIENTATION_HORIZONTAL, 1, 4, 1));
    gtk_range_set_value(GTK_RANGE(ed->brightness_scale), 3);
    gtk_scale_set_draw_value(ed->brightness_scale, TRUE);
    gtk_widget_set_hexpand(GTK_WIDGET(ed->brightness_scale), TRUE);
    gtk_box_append(br_row, GTK_WIDGET(ed->brightness_scale));
    gtk_box_append(ed->container, GTK_WIDGET(br_row));

    /* Builtin profile info box */
    ed->profile_info_box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 4));
    gtk_box_append(ed->container, GTK_WIDGET(ed->profile_info_box));

    /* Custom editor box */
    ed->editor_box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 6));
    gtk_widget_set_visible(GTK_WIDGET(ed->editor_box), FALSE);

    /* Sensor dropdown */
    GtkBox *sens_row = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8));
    GtkLabel *sens_label = GTK_LABEL(gtk_label_new(_("Sensor")));
    gtk_widget_set_size_request(GTK_WIDGET(sens_label), 70, -1);
    gtk_label_set_xalign(sens_label, 0);
    gtk_widget_set_opacity(GTK_WIDGET(sens_label), 0.7);
    gtk_box_append(sens_row, GTK_WIDGET(sens_label));

    const char *sensor_items[MAX_SENSORS + 1];
    for (int i = 0; i < ed->sensor_count; i++)
        sensor_items[i] = ed->sensor_labels[i];
    sensor_items[ed->sensor_count] = NULL;
    ed->sensor_dropdown = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(sensor_items));
    gtk_box_append(sens_row, GTK_WIDGET(ed->sensor_dropdown));
    gtk_box_append(ed->editor_box, GTK_WIDGET(sens_row));

    /* Mode toggle */
    GtkBox *mode_row = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8));
    GtkLabel *mode_label = GTK_LABEL(gtk_label_new(_("Mode")));
    gtk_widget_set_size_request(GTK_WIDGET(mode_label), 70, -1);
    gtk_label_set_xalign(mode_label, 0);
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
    gtk_label_set_xalign(bar_label, 0);
    gtk_widget_set_opacity(GTK_WIDGET(bar_label), 0.7);
    gtk_box_append(bar_row, GTK_WIDGET(bar_label));
    ed->bar_length_scale = GTK_SCALE(gtk_scale_new_with_range(
        GTK_ORIENTATION_HORIZONTAL, 2, 8, 1));
    gtk_range_set_value(GTK_RANGE(ed->bar_length_scale), 4);
    gtk_scale_set_draw_value(ed->bar_length_scale, TRUE);
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

/* ===== Destructor ===== */

void f87_sensor_editor_destroy(F87SensorEditor *editor)
{
    if (!editor) return;
    if (editor->sensor_timer) {
        g_source_remove(editor->sensor_timer);
        editor->sensor_timer = 0;
    }
    destroy_sensor_contexts(editor);
    g_free(editor);
}

/* ===== Public API ===== */

GtkWidget *f87_sensor_editor_get_widget(F87SensorEditor *editor)
{
    return GTK_WIDGET(editor->container);
}

void f87_sensor_editor_activate(F87SensorEditor *editor)
{
    if (!editor->sensor_timer)
        editor->sensor_timer = g_timeout_add(2000, on_sensor_timer, editor);
    read_sensor_values(editor);
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
    static const char *profiles[] = {"developer", "gamer", "system"};

    if (idx < 3) {
        if (config_path) *config_path = NULL;
        return profiles[idx];
    }

    /* Custom — save to file */
    f87_sensor_profile_t profile = {0};
    strncpy(profile.profile_name, "custom", sizeof(profile.profile_name) - 1);

    for (int i = 0; i < editor->mapping_count; i++) {
        custom_mapping_t *cm = &editor->mappings[i];
        f87_sensor_mapping_t *m = &profile.mappings[profile.mapping_count];
        m->sensor_name = cm->sensor_name;
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
