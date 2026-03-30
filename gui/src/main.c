#include <adwaita.h>
#include <f87/logger.h>
#include "window.h"
#include "i18n.h"

static void on_activate(GtkApplication *app, gpointer user_data)
{
    (void)user_data;

    /* Load CSS */
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_resource(css, "/com/f87control/style.css");
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );

    /* Force dark theme via libadwaita */
    AdwStyleManager *style = adw_style_manager_get_default();
    adw_style_manager_set_color_scheme(style, ADW_COLOR_SCHEME_FORCE_DARK);

    F87Window *win = f87_window_new(ADW_APPLICATION(app));
    gtk_window_present(GTK_WINDOW(win));
}

int main(int argc, char *argv[])
{
    f87_i18n_init();
    f87_log_init(F87_LOG_STDERR);

    AdwApplication *app = adw_application_new("com.f87control",
                                               G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
