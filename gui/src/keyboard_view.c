#include "keyboard_view.h"
#include "protocol.h"
#include <string.h>
#include <math.h>

/*
 * Key rectangle coordinates extracted from AULA F87 TK Windows software
 * (Dev/kb/1/KB.ini). Each entry is {x1, y1, x2, y2} in the reference
 * coordinate space (689 x 255). Key index matches f87_key_layout[] order.
 */
typedef struct {
    int x1, y1, x2, y2;
} key_rect_t;

static const key_rect_t key_rects[F87_KEY_COUNT] = {
    /* K1  ESC   */ { 25, 21, 49, 47},
    /* K2  F1    */ { 97, 21,121, 47},
    /* K3  F2    */ {132, 21,156, 47},
    /* K4  F3    */ {168, 21,192, 47},
    /* K5  F4    */ {204, 21,228, 47},
    /* K6  F5    */ {260, 21,284, 47},
    /* K7  F6    */ {295, 22,319, 48},
    /* K8  F7    */ {331, 22,355, 48},
    /* K9  F8    */ {366, 22,390, 48},
    /* K10 F9    */ {420, 22,444, 48},
    /* K11 F10   */ {455, 23,479, 49},
    /* K12 F11   */ {492, 23,516, 49},
    /* K13 F12   */ {527, 23,551, 49},
    /* K14 `     */ { 25, 63, 49, 89},
    /* K15 1     */ { 61, 64, 85, 90},
    /* K16 2     */ { 97, 64,121, 90},
    /* K17 3     */ {133, 64,157, 90},
    /* K18 4     */ {169, 64,193, 90},
    /* K19 5     */ {205, 64,229, 90},
    /* K20 6     */ {241, 64,265, 90},
    /* K21 7     */ {276, 64,300, 90},
    /* K22 8     */ {313, 64,337, 90},
    /* K23 9     */ {349, 64,373, 90},
    /* K24 0     */ {384, 65,408, 91},
    /* K25 -     */ {421, 65,445, 91},
    /* K26 =     */ {456, 65,480, 91},
    /* K27 BKSP  */ {491, 65,553, 91},
    /* K28 PRTSC */ {569, 23,593, 49},
    /* K29 SCRLK */ {605, 23,629, 49},
    /* K30 PAUSE */ {640, 23,664, 49},
    /* K31 TAB   */ { 25, 99, 67,125},
    /* K32 Q     */ { 79, 99,103,125},
    /* K33 W     */ {116, 99,140,125},
    /* K34 E     */ {151, 99,175,125},
    /* K35 R     */ {187, 99,211,125},
    /* K36 T     */ {223,100,247,126},
    /* K37 Y     */ {259,100,283,126},
    /* K38 U     */ {295,100,319,126},
    /* K39 I     */ {331,100,355,126},
    /* K40 O     */ {366,100,390,126},
    /* K41 P     */ {402,100,426,126},
    /* K42 [     */ {438,100,462,126},
    /* K43 ]     */ {473,100,497,126},
    /* K44 ENTER */ {509,100,551,126},
    /* K45 DEL   */ {569,100,593,126},
    /* K46 INS   */ {569, 65,593, 91},
    /* K47 HOME  */ {604, 65,628, 91},
    /* K48 PGUP  */ {640, 65,664, 91},
    /* K49 CAPS  */ { 25,135, 75,161},
    /* K50 A     */ { 87,135,111,161},
    /* K51 S     */ {123,135,147,161},
    /* K52 D     */ {160,135,184,161},
    /* K53 F     */ {196,136,220,162},
    /* K54 G     */ {232,136,256,162},
    /* K55 H     */ {268,136,292,162},
    /* K56 J     */ {304,136,328,162},
    /* K57 K     */ {340,136,364,162},
    /* K58 L     */ {375,136,399,162},
    /* K59 ;     */ {411,136,435,162},
    /* K60 '     */ {447,136,471,162},
    /* K61 \     */ {482,136,506,162},
    /* K62 END   */ {604,101,628,127},
    /* K63 PGDN  */ {640,101,664,127},
    /* K64 LSHFT */ { 25,170, 58,196},
    /* K65 Z     */ {106,171,130,197},
    /* K66 X     */ {141,171,165,197},
    /* K67 C     */ {177,171,201,197},
    /* K68 V     */ {214,171,238,197},
    /* K69 B     */ {250,171,274,197},
    /* K70 N     */ {286,171,310,197},
    /* K71 M     */ {322,171,346,197},
    /* K72 ,     */ {358,171,382,197},
    /* K73 .     */ {393,171,417,197},
    /* K74 /     */ {429,171,453,197},
    /* K75 RSHFT */ {465,171,552,197},
    /* K76 UP    */ {605,172,629,198},
    /* K77 RCTRL */ {519,207,551,233},
    /* K78 LCTRL */ { 25,206, 57,232},
    /* K79 LWIN  */ { 70,206,102,232},
    /* K80 LALT  */ {115,206,147,232},
    /* K81 SPACE */ {160,207,370,233},
    /* K82 RALT  */ {385,207,417,233},
    /* K83 FN    */ {429,207,461,233},
    /* K84 APP   */ {474,207,506,233},
    /* K85 LEFT  */ {569,208,593,234},
    /* K86 DOWN  */ {605,208,629,234},
    /* K87 RIGHT */ {640,208,664,234},
    /* K88 ISO   */ { 70,171, 94,197},
};

/* Reference coordinate space from KB.ini */
#define REF_W   689.0
#define REF_H   255.0

struct _F87KeyboardView {
    GtkDrawingArea parent;
    uint8_t colors[F87_KEY_COUNT][3];

    /* Paint mode */
    gboolean paint_mode;
    F87KeyPaintCallback paint_cb;
    gpointer paint_data;
};

G_DEFINE_TYPE(F87KeyboardView, f87_keyboard_view, GTK_TYPE_DRAWING_AREA)

/*
 * Map reference coordinates to widget coordinates.
 * Keys are expanded from their center to reduce gaps and appear larger.
 * The grow factor controls how many extra ref-pixels each edge gains.
 */
#define KEY_GROW  4.0   /* px in ref space — shrinks ~12px gap to ~4px */

static void ref_to_widget(const key_rect_t *kr, int width, int height,
                           double *ox, double *oy, double *ow, double *oh)
{
    double sx = (double)width  / REF_W;
    double sy = (double)height / REF_H;
    *ox = (kr->x1 - KEY_GROW) * sx;
    *oy = (kr->y1 - KEY_GROW) * sy;
    *ow = (kr->x2 - kr->x1 + 2 * KEY_GROW) * sx;
    *oh = (kr->y2 - kr->y1 + 2 * KEY_GROW) * sy;
}

static void draw_rounded_rect(cairo_t *cr, double x, double y,
                               double w, double h, double r)
{
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r,     r, -G_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r,  0, G_PI / 2);
    cairo_arc(cr, x + r,     y + h - r, r,  G_PI / 2, G_PI);
    cairo_arc(cr, x + r,     y + r,     r,  G_PI, 3 * G_PI / 2);
    cairo_close_path(cr);
}

/* Find which key was clicked */
static int hit_test(int px, int py, int width, int height)
{
    for (int i = 0; i < F87_KEY_COUNT; i++) {
        double x, y, w, h;
        ref_to_widget(&key_rects[i], width, height, &x, &y, &w, &h);
        if (px >= x && px <= x + w && py >= y && py <= y + h)
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

    double sx = (double)width  / REF_W;
    double sy = (double)height / REF_H;
    double radius = 3.0 * fmin(sx, sy);

    /* Keyboard case background */
    double case_r = 10.0 * fmin(sx, sy);
    draw_rounded_rect(cr, 4 * sx, 6 * sy,
                      (REF_W - 8) * sx, (REF_H - 12) * sy, case_r);
    cairo_set_source_rgb(cr, 0.12, 0.12, 0.14);
    cairo_fill(cr);

    /* Inner case bevel */
    draw_rounded_rect(cr, 8 * sx, 10 * sy,
                      (REF_W - 16) * sx, (REF_H - 20) * sy, case_r * 0.8);
    cairo_set_source_rgb(cr, 0.15, 0.15, 0.17);
    cairo_fill(cr);

    for (int i = 0; i < F87_KEY_COUNT; i++) {
        double x, y, w, h;
        ref_to_widget(&key_rects[i], width, height, &x, &y, &w, &h);

        uint8_t r = self->colors[i][0];
        uint8_t g = self->colors[i][1];
        uint8_t b = self->colors[i][2];

        /* Key base (slightly lighter than case) */
        draw_rounded_rect(cr, x, y, w, h, radius);
        if (r == 0 && g == 0 && b == 0) {
            cairo_set_source_rgb(cr, 0.20, 0.20, 0.24);
        } else {
            /* Blend effect color with key base */
            double fr = r / 255.0, fg = g / 255.0, fb = b / 255.0;
            cairo_set_source_rgb(cr, fr * 0.3 + 0.10,
                                     fg * 0.3 + 0.10,
                                     fb * 0.3 + 0.10);
        }
        cairo_fill(cr);

        /* Key top face (inset) — lighter, shows the LED color */
        double inset = 1.5 * fmin(sx, sy);
        if (inset < 1.0) inset = 1.0;
        double tx = x + inset, ty = y + inset;
        double tw = w - 2 * inset, th = h - 2 * inset;
        if (tw > 0 && th > 0) {
            draw_rounded_rect(cr, tx, ty, tw, th, radius * 0.7);
            if (r == 0 && g == 0 && b == 0) {
                cairo_set_source_rgb(cr, 0.25, 0.25, 0.30);
            } else {
                double fr = r / 255.0, fg = g / 255.0, fb = b / 255.0;
                cairo_set_source_rgb(cr, fr, fg, fb);
            }
            cairo_fill(cr);
        }

        /* Key label */
        if (f87_key_layout[i].name && tw > 8) {
            if (r == 0 && g == 0 && b == 0)
                cairo_set_source_rgba(cr, 1, 1, 1, 0.45);
            else
                cairo_set_source_rgba(cr, 1, 1, 1, 0.75);

            double font_size = th * 0.38;
            if (font_size < 5) font_size = 5;
            if (font_size > 11) font_size = 11;
            cairo_set_font_size(cr, font_size);

            char label[5] = {0};
            strncpy(label, f87_key_layout[i].name, 4);

            cairo_text_extents_t ext;
            cairo_text_extents(cr, label, &ext);
            cairo_move_to(cr, tx + (tw - ext.width) / 2,
                          ty + (th + ext.height) / 2);
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
    /* Aspect ratio matches KB.ini reference (689:255 ~ 2.7:1) */
    gtk_widget_set_size_request(GTK_WIDGET(self), 690, 255);
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(self), 690);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(self), 255);
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
