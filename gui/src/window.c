#include "window.h"
#include "sidebar.h"
#include "app_state.h"
#include "controls.h"
#include "keyboard_view.h"
#include <stdio.h>

struct _F87Window {
    AdwApplicationWindow parent;
    GtkPaned *paned;
    GtkBox *sidebar_box;
    GtkBox *main_box;
    GtkLabel *status_label;
    f87_app_state_t app_state;
    F87Controls *controls;
    F87KeyboardView *keyboard;
};

G_DEFINE_TYPE(F87Window, f87_window, ADW_TYPE_APPLICATION_WINDOW)

static void on_status_update(const char *text, gpointer user_data)
{
    F87Window *self = user_data;
    gtk_label_set_text(self->status_label, text);
}

static void on_effect_selected(const char *category, const char *effect_name,
                                int effect_id, gpointer user_data)
{
    F87Window *self = user_data;
    f87_controls_set_effect(self->controls, category, effect_name, effect_id);

    /* Update keyboard preview with a default color for the selected effect */
    if (strcmp(category, "hw") == 0 || strcmp(category, "sw") == 0) {
        f87_keyboard_view_set_color(self->keyboard, 255, 80, 0);
    } else if (strcmp(category, "music") == 0) {
        f87_keyboard_view_set_color(self->keyboard, 0, 128, 255);
    } else if (strcmp(category, "sensor") == 0) {
        f87_keyboard_view_clear(self->keyboard);
        /* Show sensor bar preview on F-keys */
        for (int i = 1; i <= 4; i++)
            f87_keyboard_view_set_key(self->keyboard, i, 0, 200, 0);
        for (int i = 5; i <= 8; i++)
            f87_keyboard_view_set_key(self->keyboard, i, 200, 200, 0);
        for (int i = 9; i <= 12; i++)
            f87_keyboard_view_set_key(self->keyboard, i, 0, 100, 255);
    }

    char buf[256];
    snprintf(buf, sizeof(buf), "Secilen: %s", effect_name);
    gtk_label_set_text(self->status_label, buf);
}

static void f87_window_init(F87Window *self)
{
    /* Use AdwToolbarView for proper header + content layout */
    AdwHeaderBar *header = ADW_HEADER_BAR(adw_header_bar_new());

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

    /* Populate sidebar with effect categories */
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

    /* Keyboard preview */
    self->keyboard = f87_keyboard_view_new();
    gtk_widget_set_vexpand(GTK_WIDGET(self->keyboard), TRUE);
    gtk_box_append(right_box, GTK_WIDGET(self->keyboard));

    /* Control panel — scrollable parameters + fixed buttons at bottom */
    self->controls = f87_controls_new(&self->app_state, on_status_update, self);
    gtk_box_append(right_box, f87_controls_get_widget(self->controls));

    /* Status bar */
    self->status_label = GTK_LABEL(gtk_label_new("Bekleniyor"));
    gtk_widget_add_css_class(GTK_WIDGET(self->status_label), "status-bar");
    gtk_label_set_xalign(self->status_label, 0);
    gtk_box_append(right_box, GTK_WIDGET(self->status_label));

    self->main_box = right_box;
    gtk_paned_set_end_child(self->paned, GTK_WIDGET(right_box));

    adw_toolbar_view_set_content(toolbar_view, GTK_WIDGET(self->paned));
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(self),
                                       GTK_WIDGET(toolbar_view));

    /* Initialize device connection */
    f87_app_state_init(&self->app_state);
    gtk_label_set_text(self->status_label, self->app_state.status_text);
}

static void f87_window_dispose(GObject *obj)
{
    F87Window *self = F87_WINDOW(obj);
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
