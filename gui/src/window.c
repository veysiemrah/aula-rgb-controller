#include "window.h"
#include "sidebar.h"
#include "app_state.h"
#include "controls.h"
#include "keyboard_view.h"
#include "effect_meta.h"
#include "i18n.h"
#include <stdio.h>
#include <sys/stat.h>


/* Auto-reconnect: poll every 3s, give up after 3 consecutive failures */
#define POLL_INTERVAL_MS  3000
#define RECONNECT_MAX     3

/* ===== Window state persistence ===== */

static char *get_state_path(void)
{
    const char *config = g_get_user_config_dir();
    char *dir = g_build_filename(config, "f87control", NULL);
    g_mkdir_with_parents(dir, 0755);
    char *path = g_build_filename(dir, "window.conf", NULL);
    g_free(dir);
    return path;
}

static void save_window_state(F87Window *self)
{
    char *path = get_state_path();
    gboolean maximized = gtk_window_is_maximized(GTK_WINDOW(self));
    int w = gtk_widget_get_width(GTK_WIDGET(self));
    int h = gtk_widget_get_height(GTK_WIDGET(self));

    FILE *f = fopen(path, "w");
    if (f) {
        fprintf(f, "width=%d\nheight=%d\nmaximized=%d\n", w, h, maximized ? 1 : 0);
        fclose(f);
    }
    g_free(path);
}

static void load_window_state(F87Window *self)
{
    int w = 1000, h = 550, maximized = 0;

    char *path = get_state_path();
    FILE *f = fopen(path, "r");
    g_free(path);
    if (f) {
        char line[64];
        while (fgets(line, sizeof(line), f)) {
            sscanf(line, "width=%d", &w);
            sscanf(line, "height=%d", &h);
            sscanf(line, "maximized=%d", &maximized);
        }
        fclose(f);
    }

    if (w > 200 && h > 200)
        gtk_window_set_default_size(GTK_WINDOW(self), w, h);
    if (maximized)
        gtk_window_maximize(GTK_WINDOW(self));
}

struct _F87Window {
    AdwApplicationWindow parent;
    GtkPaned *paned;
    GtkBox *sidebar_box;
    GtkBox *main_box;
    GtkLabel *status_label;
    GtkButton *rescan_button;
    GtkDropDown *esc_led_dropdown;
    GtkScale *esc_led_speed;
    GtkDropDown *esc_led_color;
    guint esc_led_speed_timer;
    gboolean esc_led_updating;  /* set while syncing dropdown from daemon */
    f87_app_state_t app_state;
    F87Controls *controls;
    F87KeyboardView *keyboard;
    guint rescan_timer;
    guint poll_timer;
    int reconnect_failures;
    gboolean was_connected;
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

    /* If effect is now running (auto-applied via hot-switch), show running status.
     * Otherwise show selection. */
    if (self->app_state.status == F87_GUI_RUNNING)
        on_status_update(self->app_state.status_text, self);
    else {
        char buf[256];
        snprintf(buf, sizeof(buf), _("Selected: %s"), effect_name);
        gtk_label_set_text(self->status_label, buf);
    }
}

/* ===== Auto-reconnect poll ===== */

static gboolean on_poll_connection(gpointer data)
{
    F87Window *self = data;
    if (!self->app_state.client) return G_SOURCE_CONTINUE;

    int connected = f87_client_is_connected(self->app_state.client) > 0;

    if (connected) {
        /* Connection OK — reset failure counter */
        if (!self->was_connected) {
            self->was_connected = TRUE;
            self->reconnect_failures = 0;
            self->app_state.device_connected = true;
            /* Show current effect or idle status */
            if (self->app_state.status == F87_GUI_RUNNING) {
                on_status_update(self->app_state.status_text, self);
            } else {
                snprintf(self->app_state.status_text,
                         sizeof(self->app_state.status_text),
                         "%s", _("Connected (daemon)"));
                self->app_state.status = F87_GUI_IDLE;
                self->app_state.status_level = F87_LOG_INFO;
                on_status_update(self->app_state.status_text, self);
            }
        }
        return G_SOURCE_CONTINUE;
    }

    /* Disconnected */
    if (self->was_connected) {
        /* Just lost connection — reset direct mode state and start reconnect */
        self->was_connected = FALSE;
        self->reconnect_failures = 0;
        f87_controls_reset_sw_state(self->controls);
    }

    if (self->reconnect_failures >= RECONNECT_MAX) {
        /* Already gave up — keep polling but don't spam rescan */
        return G_SOURCE_CONTINUE;
    }

    /* Attempt rescan */
    self->reconnect_failures++;
    f87_app_state_rescan(&self->app_state);

    if (self->app_state.device_connected) {
        /* Reconnected! */
        self->was_connected = TRUE;
        self->reconnect_failures = 0;
        on_status_update(self->app_state.status_text, self);
    } else if (self->reconnect_failures >= RECONNECT_MAX) {
        /* Final attempt failed */
        snprintf(self->app_state.status_text,
                 sizeof(self->app_state.status_text),
                 "%s", _("Connection lost — could not reconnect"));
        self->app_state.status = F87_GUI_ERROR;
        self->app_state.status_level = F87_LOG_ERROR;
        on_status_update(self->app_state.status_text, self);
    } else {
        char buf[256];
        snprintf(buf, sizeof(buf), _("Reconnecting... (%d/%d)"),
                 self->reconnect_failures, RECONNECT_MAX);
        gtk_label_set_text(self->status_label, buf);
        gtk_widget_remove_css_class(GTK_WIDGET(self->status_label), "status-ok");
        gtk_widget_remove_css_class(GTK_WIDGET(self->status_label), "status-error");
        gtk_widget_add_css_class(GTK_WIDGET(self->status_label), "status-warn");
    }

    return G_SOURCE_CONTINUE;
}

/* ===== ESC indicator LED (battery light) ===== */

/* Mode dropdown entries. "Off" is static mode with colour index 7 — mode 0
 * does not switch the LED off on the F87 TK. */
#define ESC_LED_OFF 0xFE
static const uint8_t esc_led_modes[] = {
    1, 2, 3, 4, ESC_LED_OFF,
};
#define ESC_LED_COUNT (sizeof(esc_led_modes) / sizeof(esc_led_modes[0]))

static uint8_t esc_led_selected_mode(F87Window *self)
{
    guint i = gtk_drop_down_get_selected(self->esc_led_dropdown);
    return i < ESC_LED_COUNT ? esc_led_modes[i] : 1;
}

static void esc_led_update_sensitivity(F87Window *self)
{
    uint8_t mode = esc_led_selected_mode(self);
    /* Speed: animated modes. Colour: static and single-colour breathing. */
    gtk_widget_set_sensitive(GTK_WIDGET(self->esc_led_speed),
                             mode == 1 || mode == 2 || mode == 4);
    gtk_widget_set_sensitive(GTK_WIDGET(self->esc_led_color),
                             mode == 3 || mode == 4);
}

static void esc_led_select_mode(F87Window *self, uint8_t mode)
{
    for (guint i = 0; i < ESC_LED_COUNT; i++) {
        if (esc_led_modes[i] == mode) {
            gtk_drop_down_set_selected(self->esc_led_dropdown, i);
            return;
        }
    }
}

/* Mirror the menu's selection on the LED dot in the keyboard picture */
static void esc_led_update_view(F87Window *self)
{
    static const uint8_t rgb[7][3] = {
        {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 0},
        {255, 64, 192}, {64, 192, 255}, {255, 255, 255},
    };
    if (!self->keyboard) return;

    uint8_t mode = esc_led_selected_mode(self);
    guint c = gtk_drop_down_get_selected(self->esc_led_color);
    if (c >= 7) c = 0;

    if (mode == ESC_LED_OFF)
        f87_keyboard_view_set_indicator(self->keyboard, F87_INDICATOR_OFF, 0, 0, 0);
    else if (mode == 1 || mode == 2)
        f87_keyboard_view_set_indicator(self->keyboard, F87_INDICATOR_RAINBOW, 0, 0, 0);
    else
        f87_keyboard_view_set_indicator(self->keyboard, F87_INDICATOR_SOLID,
                                        rgb[c][0], rgb[c][1], rgb[c][2]);
}

static void esc_led_sync(F87Window *self)
{
    if (!self->app_state.client || !self->esc_led_dropdown) return;
    f87_client *c = self->app_state.client;
    self->esc_led_updating = TRUE;

    int mode = f87_client_get_battery_light(c);
    int color = f87_client_get_battery_light_color(c);
    int speed = f87_client_get_battery_light_speed(c);

    if (color == F87_BATTERY_COLOR_OFF && (mode == 3 || mode == 4))
        mode = ESC_LED_OFF;
    if (mode >= 0)
        esc_led_select_mode(self, (uint8_t)mode);

    if (color >= 0 && color < F87_BATTERY_COLOR_OFF)
        gtk_drop_down_set_selected(self->esc_led_color, (guint)color);
    if (speed >= 0 && speed <= F87_BATTERY_SPEED_MAX)
        gtk_range_set_value(GTK_RANGE(self->esc_led_speed), speed);

    esc_led_update_sensitivity(self);
    esc_led_update_view(self);
    self->esc_led_updating = FALSE;
}

static void esc_led_report(F87Window *self, int rc, const char *ok_text,
                           const char *fail_text)
{
    gtk_widget_remove_css_class(GTK_WIDGET(self->status_label), "status-error");
    if (rc < 0) {
        gtk_label_set_text(self->status_label, fail_text);
        gtk_widget_add_css_class(GTK_WIDGET(self->status_label), "status-error");
    } else {
        gtk_label_set_text(self->status_label, ok_text);
    }
}

static void on_esc_led_changed(GtkDropDown *dd, GParamSpec *pspec, gpointer data)
{
    (void)dd; (void)pspec;
    F87Window *self = data;
    esc_led_update_sensitivity(self);
    if (self->esc_led_updating) return;
    esc_led_update_view(self);

    f87_client *c = self->app_state.client;
    uint8_t mode = esc_led_selected_mode(self);
    int rc = -1;
    if (c) {
        if (mode == ESC_LED_OFF) {
            rc = f87_client_set_battery_light(c, 3);
            if (rc >= 0)
                rc = f87_client_set_battery_light_color(c, F87_BATTERY_COLOR_OFF);
        } else {
            rc = f87_client_set_battery_light(c, mode);
            /* Leaving "Off": bring back the colour shown in the colour list */
            if (rc >= 0 && (mode == 3 || mode == 4) &&
                f87_client_get_battery_light_color(c) == F87_BATTERY_COLOR_OFF)
                rc = f87_client_set_battery_light_color(c,
                        (uint8_t)gtk_drop_down_get_selected(self->esc_led_color));
        }
    }
    esc_led_report(self, rc, _("ESC indicator LED updated"),
                   _("Failed to set ESC indicator LED"));
}

static void on_esc_led_color_changed(GtkDropDown *dd, GParamSpec *pspec, gpointer data)
{
    (void)pspec;
    F87Window *self = data;
    if (self->esc_led_updating) return;
    esc_led_update_view(self);

    guint color = gtk_drop_down_get_selected(dd);
    if (color >= F87_BATTERY_COLOR_OFF) return;

    int rc = self->app_state.client
        ? f87_client_set_battery_light_color(self->app_state.client, (uint8_t)color)
        : -1;
    esc_led_report(self, rc, _("ESC indicator LED colour updated"),
                   _("Failed to set ESC indicator LED colour"));
}

/* Apply speed when the user lets go of the slider, not on every step:
 * each change is a flash write on the keyboard. */
static gboolean esc_led_apply_speed(gpointer data)
{
    F87Window *self = data;
    self->esc_led_speed_timer = 0;

    int speed = (int)gtk_range_get_value(GTK_RANGE(self->esc_led_speed));
    int rc = self->app_state.client
        ? f87_client_set_battery_light_speed(self->app_state.client, (uint8_t)speed)
        : -1;
    esc_led_report(self, rc, _("ESC indicator LED speed updated"),
                   _("Failed to set ESC indicator LED speed"));
    return G_SOURCE_REMOVE;
}

static void on_esc_led_speed_changed(GtkRange *range, gpointer data)
{
    (void)range;
    F87Window *self = data;
    if (self->esc_led_updating) return;
    if (self->esc_led_speed_timer)
        g_source_remove(self->esc_led_speed_timer);
    self->esc_led_speed_timer = g_timeout_add(400, esc_led_apply_speed, self);
}

/* The LED hint shows until the user has opened the LED menu once */
static char *esc_led_hint_path(void)
{
    return g_build_filename(g_get_user_config_dir(), "f87control",
                            "led-hint-seen", NULL);
}

static void on_esc_led_hint_dismissed(gpointer data)
{
    (void)data;
    char *path = esc_led_hint_path();
    char *dir = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0755);
    g_file_set_contents(path, "", 0, NULL);
    g_free(dir);
    g_free(path);
}

static void on_esc_led_open(gpointer data)
{
    esc_led_sync(data);
}

static GtkWidget *esc_led_row_label(const char *text)
{
    GtkLabel *l = GTK_LABEL(gtk_label_new(text));
    gtk_label_set_xalign(l, 0);
    gtk_widget_add_css_class(GTK_WIDGET(l), "dim-label");
    return GTK_WIDGET(l);
}

static GtkWidget *create_esc_led_menu(F87Window *self)
{
    GtkBox *box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 8));
    gtk_widget_set_size_request(GTK_WIDGET(box), 280, -1);
    gtk_widget_set_margin_start(GTK_WIDGET(box), 8);
    gtk_widget_set_margin_end(GTK_WIDGET(box), 8);
    gtk_widget_set_margin_top(GTK_WIDGET(box), 8);
    gtk_widget_set_margin_bottom(GTK_WIDGET(box), 8);

    GtkLabel *title = GTK_LABEL(gtk_label_new(_("ESC Indicator LED")));
    gtk_widget_add_css_class(GTK_WIDGET(title), "title-4");
    gtk_label_set_xalign(title, 0);
    gtk_box_append(box, GTK_WIDGET(title));

    /* Mode */
    gtk_box_append(box, esc_led_row_label(_("Mode")));
    const char *names[] = {
        _("Rainbow"),
        _("Breathing (colour cycle)"), _("Static"), _("Breathing"), _("Off"), NULL
    };
    self->esc_led_dropdown = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(names));
    gtk_box_append(box, GTK_WIDGET(self->esc_led_dropdown));
    g_signal_connect(self->esc_led_dropdown, "notify::selected",
                     G_CALLBACK(on_esc_led_changed), self);

    /* Colour */
    gtk_box_append(box, esc_led_row_label(_("Colour")));
    const char *colors[] = {
        _("Red"), _("Green"), _("Blue"), _("Yellow"), _("Pink"),
        _("Light blue"), _("White"), NULL
    };
    self->esc_led_color = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(colors));
    gtk_box_append(box, GTK_WIDGET(self->esc_led_color));
    g_signal_connect(self->esc_led_color, "notify::selected",
                     G_CALLBACK(on_esc_led_color_changed), self);

    /* Speed */
    gtk_box_append(box, esc_led_row_label(_("Speed")));
    self->esc_led_speed = GTK_SCALE(gtk_scale_new_with_range(
        GTK_ORIENTATION_HORIZONTAL, 0, F87_BATTERY_SPEED_MAX, 1));
    gtk_scale_set_draw_value(self->esc_led_speed, FALSE);
    gtk_range_set_round_digits(GTK_RANGE(self->esc_led_speed), 0);
    gtk_scale_add_mark(self->esc_led_speed, 0, GTK_POS_BOTTOM, _("Slow"));
    gtk_scale_add_mark(self->esc_led_speed, 2, GTK_POS_BOTTOM, NULL);
    gtk_scale_add_mark(self->esc_led_speed, F87_BATTERY_SPEED_MAX, GTK_POS_BOTTOM, _("Fast"));
    gtk_box_append(box, GTK_WIDGET(self->esc_led_speed));
    g_signal_connect(self->esc_led_speed, "value-changed",
                     G_CALLBACK(on_esc_led_speed_changed), self);

    /* Firmware caveat */
    GtkLabel *note = GTK_LABEL(gtk_label_new(
        _("If the LED keeps a fast rainbow no matter what you pick, the keyboard "
          "firmware has taken it over (this happens after the keyboard restarts). "
          "Hold Fn+Esc for 5 s to reset the keyboard, then choose again.")));
    gtk_label_set_wrap(note, TRUE);
    gtk_label_set_max_width_chars(note, 36);
    gtk_label_set_xalign(note, 0);
    gtk_widget_add_css_class(GTK_WIDGET(note), "dim-label");
    gtk_widget_add_css_class(GTK_WIDGET(note), "caption");
    gtk_widget_set_margin_top(GTK_WIDGET(note), 4);
    gtk_box_append(box, GTK_WIDGET(note));

    GtkPopover *popover = GTK_POPOVER(gtk_popover_new());
    gtk_popover_set_child(popover, GTK_WIDGET(box));
    return GTK_WIDGET(popover);
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
    self->reconnect_failures = 0;  /* Reset so auto-reconnect retries too */
    f87_app_state_rescan(&self->app_state);
    self->was_connected = self->app_state.device_connected;
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

static gboolean on_close_request(GtkWindow *window, gpointer data)
{
    (void)data;
    save_window_state(F87_WINDOW(window));
    return FALSE;
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
    /* Click the LED dot between ESC and F1 to open its menu */
    f87_keyboard_view_set_indicator_popover(self->keyboard,
        create_esc_led_menu(self), on_esc_led_open, self);
    {
        char *hint = esc_led_hint_path();
        if (!g_file_test(hint, G_FILE_TEST_EXISTS))
            f87_keyboard_view_set_indicator_hint(self->keyboard, TRUE,
                                                 on_esc_led_hint_dismissed, self);
        g_free(hint);
    }
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
    GtkEventController *sc = gtk_shortcut_controller_new();
    gtk_shortcut_controller_set_scope(GTK_SHORTCUT_CONTROLLER(sc), GTK_SHORTCUT_SCOPE_GLOBAL);
    gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(sc), gtk_shortcut_new(
        gtk_shortcut_trigger_parse_string("<Control>s"),
        gtk_callback_action_new(on_save_shortcut, self, NULL)));
    gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(sc), gtk_shortcut_new(
        gtk_shortcut_trigger_parse_string("Escape"),
        gtk_callback_action_new(on_stop_shortcut, self, NULL)));
    gtk_widget_add_controller(GTK_WIDGET(self), sc);

    /* Initialize device connection */
    f87_app_state_init(&self->app_state);
    self->was_connected = self->app_state.device_connected;
    esc_led_sync(self);  /* draw the LED dot in its current state */
    gtk_label_set_text(self->status_label, self->app_state.status_text);
    if (self->app_state.status == F87_GUI_ERROR)
        gtk_widget_add_css_class(GTK_WIDGET(self->status_label), "status-error");

    /* Restore saved window size + save on close */
    load_window_state(self);
    g_signal_connect(self, "close-request", G_CALLBACK(on_close_request), NULL);

    /* Start connection poll timer */
    self->poll_timer = g_timeout_add(POLL_INTERVAL_MS, on_poll_connection, self);
}

static void f87_window_dispose(GObject *obj)
{
    F87Window *self = F87_WINDOW(obj);
    if (self->poll_timer) {
        g_source_remove(self->poll_timer);
        self->poll_timer = 0;
    }
    if (self->rescan_timer) {
        g_source_remove(self->rescan_timer);
        self->rescan_timer = 0;
    }
    if (self->esc_led_speed_timer) {
        g_source_remove(self->esc_led_speed_timer);
        self->esc_led_speed_timer = 0;
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
                        "title", "AULA RGB Control",
                        NULL);
}
