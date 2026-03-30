#include "window.h"
#include "sidebar.h"
#include "app_state.h"
#include "controls.h"
#include "keyboard_view.h"
#include "effect_meta.h"
#include "i18n.h"
#include <stdio.h>

/* Auto-reconnect: poll every 3s, give up after 3 consecutive failures */
#define POLL_INTERVAL_MS  3000
#define RECONNECT_MAX     3

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

    char buf[256];
    snprintf(buf, sizeof(buf), _("Selected: %s"), effect_name);
    gtk_label_set_text(self->status_label, buf);
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
            snprintf(self->app_state.status_text,
                     sizeof(self->app_state.status_text),
                     "%s", _("Connected (daemon)"));
            self->app_state.status = F87_GUI_IDLE;
            self->app_state.status_level = F87_LOG_INFO;
            on_status_update(self->app_state.status_text, self);
        }
        return G_SOURCE_CONTINUE;
    }

    /* Disconnected */
    if (self->was_connected) {
        /* Just lost connection — start reconnect attempts */
        self->was_connected = FALSE;
        self->reconnect_failures = 0;
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
    gtk_label_set_text(self->status_label, self->app_state.status_text);
    if (self->app_state.status == F87_GUI_ERROR)
        gtk_widget_add_css_class(GTK_WIDGET(self->status_label), "status-error");

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
