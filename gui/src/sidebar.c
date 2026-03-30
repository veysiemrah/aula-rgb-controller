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
        {"Explode",    110, N_("Explosions on key press, speed controls blast radius")},
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
