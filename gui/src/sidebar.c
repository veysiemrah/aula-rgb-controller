#include "sidebar.h"

typedef struct {
    F87SidebarCallback callback;
    gpointer user_data;
} SidebarData;

typedef struct {
    const char *name;
    int id;
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

static GtkListBoxRow *make_effect_row(const char *category, const char *name, int id)
{
    GtkListBoxRow *row = GTK_LIST_BOX_ROW(gtk_list_box_row_new());
    GtkLabel *label = GTK_LABEL(gtk_label_new(name));
    gtk_label_set_xalign(label, 0);
    gtk_list_box_row_set_child(row, GTK_WIDGET(label));

    g_object_set_data(G_OBJECT(row), "category", (gpointer)category);
    g_object_set_data(G_OBJECT(row), "effect-name", (gpointer)name);
    g_object_set_data(G_OBJECT(row), "effect-id", GINT_TO_POINTER(id));

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
            make_effect_row(category, entries[i].name, entries[i].id)));

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

    /* HW Effects */
    static const EffectEntry hw[] = {
        {"Off", 0}, {"Static", 1}, {"Breathing", 2}, {"Wave", 3},
        {"Spectrum", 4}, {"Rain", 5}, {"Ripple", 7}, {"Starlight", 8},
        {"Snake", 10}, {"Aurora", 11}, {"Reactive", 12}, {"Marquee", 13},
        {"Circle", 15}, {"Rain Down", 16}, {"Center Ripple", 17}, {"Custom", 18},
        {NULL, 0}
    };
    gtk_box_append(box, make_expander_category("Donanim Efektleri", "hw", hw, sd));

    /* SW Effects */
    static const EffectEntry sw[] = {
        {"Fire", 100}, {"Matrix", 101}, {"Plasma", 102}, {"Radar", 104},
        {"Lightning", 105}, {"Explode", 110}, {"Ripple SW", 111},
        {"Typewriter", 112}, {"Life", 113}, {"KeyHeat", 114},
        {NULL, 0}
    };
    gtk_box_append(box, make_expander_category("Yazilimsal Efektler", "sw", sw, sd));

    /* Music */
    static const EffectEntry mu[] = {
        {"Spectrum", 200}, {"Beat", 201}, {"Energy", 202},
        {"VU Meter", 203}, {"FreqMap", 204},
        {NULL, 0}
    };
    gtk_box_append(box, make_expander_category("Muzik", "music", mu, sd));

    /* Sensor — special: rows carry sensor-profile data */
    static const EffectEntry se[] = {
        {"Developer", 106}, {"Gamer", 106}, {"System", 106},
        {NULL, 0}
    };
    GtkListBox *se_list = GTK_LIST_BOX(gtk_list_box_new());
    gtk_widget_add_css_class(GTK_WIDGET(se_list), "sidebar");
    g_signal_connect(se_list, "row-activated", G_CALLBACK(on_row_activated), sd);
    for (int i = 0; se[i].name; i++) {
        GtkListBoxRow *row = make_effect_row("sensor", se[i].name, se[i].id);
        g_object_set_data(G_OBJECT(row), "sensor-profile", (gpointer)se[i].name);
        gtk_list_box_append(se_list, GTK_WIDGET(row));
    }

    GtkExpander *se_expander = GTK_EXPANDER(gtk_expander_new("Sensor"));
    gtk_expander_set_expanded(se_expander, FALSE);
    gtk_expander_set_child(se_expander, GTK_WIDGET(se_list));
    gtk_widget_add_css_class(GTK_WIDGET(se_expander), "category-expander");
    gtk_box_append(box, GTK_WIDGET(se_expander));

    return GTK_WIDGET(box);
}
