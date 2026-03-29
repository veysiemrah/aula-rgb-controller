#include "keyboard_view.h"
#include "protocol.h"
#include <string.h>

struct _F87KeyboardView {
    GtkDrawingArea parent;
    uint8_t colors[F87_KEY_COUNT][3];

    /* Paint mode */
    gboolean paint_mode;
    F87KeyPaintCallback paint_cb;
    gpointer paint_data;
};

G_DEFINE_TYPE(F87KeyboardView, f87_keyboard_view, GTK_TYPE_DRAWING_AREA)

/* Find which key was clicked given pixel coordinates */
static int hit_test(int px, int py, int width, int height)
{
    int max_col = 0, max_row = 0;
    for (int i = 0; i < F87_KEY_COUNT; i++) {
        if (f87_key_layout[i].col > max_col) max_col = f87_key_layout[i].col;
        if (f87_key_layout[i].row > max_row) max_row = f87_key_layout[i].row;
    }
    max_col++;
    max_row++;

    double pad = 8.0, gap = 3.0;
    double key_w = (width - 2 * pad - (max_col - 1) * gap) / (double)max_col;
    double key_h = (height - 2 * pad - (max_row - 1) * gap) / (double)max_row;
    if (key_w < 8) key_w = 8;
    if (key_h < 8) key_h = 8;

    for (int i = 0; i < F87_KEY_COUNT; i++) {
        double x = pad + f87_key_layout[i].col * (key_w + gap);
        double y = pad + f87_key_layout[i].row * (key_h + gap);
        if (px >= x && px <= x + key_w && py >= y && py <= y + key_h)
            return i;
    }
    return -1;
}

static void draw_func(GtkDrawingArea *area, cairo_t *cr,
                       int width, int height, gpointer user_data)
{
    (void)area;
    (void)user_data;
    F87KeyboardView *self = F87_KEYBOARD_VIEW(area);

    int max_col = 0, max_row = 0;
    for (int i = 0; i < F87_KEY_COUNT; i++) {
        if (f87_key_layout[i].col > max_col) max_col = f87_key_layout[i].col;
        if (f87_key_layout[i].row > max_row) max_row = f87_key_layout[i].row;
    }
    max_col++;
    max_row++;

    double pad = 8.0;
    double gap = 3.0;
    double key_w = (width - 2 * pad - (max_col - 1) * gap) / (double)max_col;
    double key_h = (height - 2 * pad - (max_row - 1) * gap) / (double)max_row;
    if (key_w < 8) key_w = 8;
    if (key_h < 8) key_h = 8;
    double radius = 3.0;

    /* Background */
    cairo_set_source_rgb(cr, 0.06, 0.20, 0.37);
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_fill(cr);

    for (int i = 0; i < F87_KEY_COUNT; i++) {
        double x = pad + f87_key_layout[i].col * (key_w + gap);
        double y = pad + f87_key_layout[i].row * (key_h + gap);

        uint8_t r = self->colors[i][0];
        uint8_t g = self->colors[i][1];
        uint8_t b = self->colors[i][2];

        if (r == 0 && g == 0 && b == 0)
            cairo_set_source_rgb(cr, 0.12, 0.12, 0.18);
        else
            cairo_set_source_rgb(cr, r / 255.0, g / 255.0, b / 255.0);

        cairo_new_sub_path(cr);
        cairo_arc(cr, x + key_w - radius, y + radius, radius, -G_PI / 2, 0);
        cairo_arc(cr, x + key_w - radius, y + key_h - radius, radius, 0, G_PI / 2);
        cairo_arc(cr, x + radius, y + key_h - radius, radius, G_PI / 2, G_PI);
        cairo_arc(cr, x + radius, y + radius, radius, G_PI, 3 * G_PI / 2);
        cairo_close_path(cr);
        cairo_fill(cr);

        if (f87_key_layout[i].name && key_w > 14) {
            cairo_set_source_rgba(cr, 1, 1, 1, 0.6);
            double font_size = key_h * 0.35;
            if (font_size < 6) font_size = 6;
            if (font_size > 12) font_size = 12;
            cairo_set_font_size(cr, font_size);

            char label[4] = {0};
            strncpy(label, f87_key_layout[i].name, 3);

            cairo_text_extents_t ext;
            cairo_text_extents(cr, label, &ext);
            cairo_move_to(cr, x + (key_w - ext.width) / 2,
                          y + (key_h + ext.height) / 2);
            cairo_show_text(cr, label);
        }
    }
}

static void on_key_clicked(GtkGestureClick *gesture, int n_press, double x, double y,
                            gpointer user_data)
{
    (void)gesture; (void)n_press;
    F87KeyboardView *self = user_data;
    if (!self->paint_mode) return;

    int w = gtk_widget_get_width(GTK_WIDGET(self));
    int h = gtk_widget_get_height(GTK_WIDGET(self));
    int key_id = hit_test((int)x, (int)y, w, h);
    if (key_id >= 0 && self->paint_cb)
        self->paint_cb(key_id, self->paint_data);
}

static void f87_keyboard_view_init(F87KeyboardView *self)
{
    memset(self->colors, 0, sizeof(self->colors));
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(self), draw_func, NULL, NULL);
    /* Fixed size */
    gtk_widget_set_size_request(GTK_WIDGET(self), 650, 190);
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(self), 650);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(self), 190);
    gtk_widget_set_halign(GTK_WIDGET(self), GTK_ALIGN_CENTER);
    gtk_widget_set_valign(GTK_WIDGET(self), GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(GTK_WIDGET(self), FALSE);
    gtk_widget_set_vexpand(GTK_WIDGET(self), FALSE);

    /* Click handler for paint mode */
    GtkGesture *click = gtk_gesture_click_new();
    g_signal_connect(click, "pressed", G_CALLBACK(on_key_clicked), self);
    gtk_widget_add_controller(GTK_WIDGET(self), GTK_EVENT_CONTROLLER(click));
}

static void f87_keyboard_view_class_init(F87KeyboardViewClass *klass)
{
    (void)klass;
}

F87KeyboardView *f87_keyboard_view_new(void)
{
    return g_object_new(F87_TYPE_KEYBOARD_VIEW, NULL);
}

void f87_keyboard_view_set_color(F87KeyboardView *view, uint8_t r, uint8_t g, uint8_t b)
{
    for (int i = 0; i < F87_KEY_COUNT; i++) {
        view->colors[i][0] = r;
        view->colors[i][1] = g;
        view->colors[i][2] = b;
    }
    gtk_widget_queue_draw(GTK_WIDGET(view));
}

void f87_keyboard_view_set_key(F87KeyboardView *view, int key_id,
                                uint8_t r, uint8_t g, uint8_t b)
{
    if (key_id < 0 || key_id >= F87_KEY_COUNT) return;
    view->colors[key_id][0] = r;
    view->colors[key_id][1] = g;
    view->colors[key_id][2] = b;
    gtk_widget_queue_draw(GTK_WIDGET(view));
}

void f87_keyboard_view_clear(F87KeyboardView *view)
{
    memset(view->colors, 0, sizeof(view->colors));
    gtk_widget_queue_draw(GTK_WIDGET(view));
}

void f87_keyboard_view_set_all_keys(F87KeyboardView *view,
                                     const uint8_t colors[][3], int count)
{
    if (count > F87_KEY_COUNT) count = F87_KEY_COUNT;
    for (int i = 0; i < count; i++) {
        view->colors[i][0] = colors[i][0];
        view->colors[i][1] = colors[i][1];
        view->colors[i][2] = colors[i][2];
    }
    gtk_widget_queue_draw(GTK_WIDGET(view));
}

void f87_keyboard_view_set_paint_mode(F87KeyboardView *view, gboolean enabled,
                                       F87KeyPaintCallback cb, gpointer user_data)
{
    view->paint_mode = enabled;
    view->paint_cb = cb;
    view->paint_data = user_data;

    if (enabled)
        gtk_widget_set_cursor_from_name(GTK_WIDGET(view), "cell");
    else
        gtk_widget_set_cursor(GTK_WIDGET(view), NULL);
}

const uint8_t (*f87_keyboard_view_get_colors(F87KeyboardView *view))[3]
{
    return (const uint8_t (*)[3])view->colors;
}
