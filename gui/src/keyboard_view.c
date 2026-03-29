#include "keyboard_view.h"
#include "protocol.h"
#include <string.h>

struct _F87KeyboardView {
    GtkDrawingArea parent;
    uint8_t colors[F87_KEY_COUNT][3];
};

G_DEFINE_TYPE(F87KeyboardView, f87_keyboard_view, GTK_TYPE_DRAWING_AREA)

static void draw_func(GtkDrawingArea *area, cairo_t *cr,
                       int width, int height, gpointer user_data)
{
    (void)area;
    (void)user_data;
    F87KeyboardView *self = F87_KEYBOARD_VIEW(area);

    /* Calculate key dimensions to fit widget */
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
    cairo_set_source_rgb(cr, 0.06, 0.20, 0.37); /* #0f3460 */
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_fill(cr);

    for (int i = 0; i < F87_KEY_COUNT; i++) {
        double x = pad + f87_key_layout[i].col * (key_w + gap);
        double y = pad + f87_key_layout[i].row * (key_h + gap);

        uint8_t r = self->colors[i][0];
        uint8_t g = self->colors[i][1];
        uint8_t b = self->colors[i][2];

        /* Key background */
        if (r == 0 && g == 0 && b == 0)
            cairo_set_source_rgb(cr, 0.12, 0.12, 0.18);
        else
            cairo_set_source_rgb(cr, r / 255.0, g / 255.0, b / 255.0);

        /* Rounded rectangle */
        cairo_new_sub_path(cr);
        cairo_arc(cr, x + key_w - radius, y + radius, radius, -G_PI / 2, 0);
        cairo_arc(cr, x + key_w - radius, y + key_h - radius, radius, 0, G_PI / 2);
        cairo_arc(cr, x + radius, y + key_h - radius, radius, G_PI / 2, G_PI);
        cairo_arc(cr, x + radius, y + radius, radius, G_PI, 3 * G_PI / 2);
        cairo_close_path(cr);
        cairo_fill(cr);

        /* Key label (first 3 chars) */
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

static void f87_keyboard_view_init(F87KeyboardView *self)
{
    memset(self->colors, 0, sizeof(self->colors));
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(self), draw_func, NULL, NULL);
    gtk_widget_set_size_request(GTK_WIDGET(self), 600, 200);
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
