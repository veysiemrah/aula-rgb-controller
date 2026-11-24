#include "keyboard_view.h"
#include "protocol.h"
#include "i18n.h"
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

/* Indicator LED between ESC and F1: a thin upright bar (centre and
 * half-size in ref space; keys are 24 x 26) */
#define IND_X   73.0
#define IND_Y   34.0
#define IND_HW   2.0
#define IND_HH  10.0

struct _F87KeyboardView {
    GtkDrawingArea parent;
    uint8_t colors[F87_KEY_COUNT][3];

    /* Paint mode */
    gboolean paint_mode;
    F87KeyPaintCallback paint_cb;
    gpointer paint_data;
    gboolean paint_dragging;
    int last_painted_key;

    /* Sensor overlays */
    f87_key_overlay_t overlays[F87_KEY_COUNT];
    int overlay_count;

    /* Sensor click mode (separate from paint mode) */
    gboolean click_mode;
    F87KeyClickCallback click_cb;
    gpointer click_data;

    /* Indicator LED */
    f87_indicator_style_t ind_style;
    uint8_t ind_color[3];
    gboolean ind_hover;
    GtkWidget *ind_popover;
    F87IndicatorOpenCallback ind_open_cb;
    gpointer ind_open_data;
    gboolean ind_hint;
    guint ind_hint_tick;
    double ind_hint_phase;  /* 0..1, drives the pulse */
    GtkWidget *ind_callout;
    guint ind_callout_timer;
    F87IndicatorOpenCallback ind_dismiss_cb;
    gpointer ind_dismiss_data;
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

static gboolean indicator_hit(double px, double py, int width, int height)
{
    double sx = (double)width / REF_W, sy = (double)height / REF_H;
    double pad = 5.0;  /* generous click target around the thin bar */
    return px >= (IND_X - IND_HW - pad) * sx && px <= (IND_X + IND_HW + pad) * sx &&
           py >= (IND_Y - IND_HH - pad) * sy && py <= (IND_Y + IND_HH + pad) * sy;
}

static void draw_indicator(F87KeyboardView *self, cairo_t *cr,
                           double sx, double sy)
{
    double s = fmin(sx, sy);
    double x = (IND_X - IND_HW) * sx, y = (IND_Y - IND_HH) * sy;
    double w = 2 * IND_HW * sx, h = 2 * IND_HH * sy;
    double r = w / 2;

    draw_rounded_rect(cr, x, y, w, h, r);
    if (self->ind_style == F87_INDICATOR_RAINBOW) {
        cairo_pattern_t *pat = cairo_pattern_create_linear(0, y, 0, y + h);
        static const double rainbow[6][3] = {
            {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 1, 1}, {0, 0, 1}, {1, 0, 1},
        };
        for (int i = 0; i < 6; i++)
            cairo_pattern_add_color_stop_rgb(pat, i / 5.0, rainbow[i][0],
                                             rainbow[i][1], rainbow[i][2]);
        cairo_set_source(cr, pat);
        cairo_fill(cr);
        cairo_pattern_destroy(pat);
    } else {
        if (self->ind_style == F87_INDICATOR_SOLID)
            cairo_set_source_rgb(cr, self->ind_color[0] / 255.0,
                                 self->ind_color[1] / 255.0,
                                 self->ind_color[2] / 255.0);
        else
            cairo_set_source_rgb(cr, 0.06, 0.06, 0.07);
        cairo_fill(cr);
    }

    /* New-user hint: pulsing glow (the callout bubble does the talking) */
    if (self->ind_hint) {
        double a = 0.25 + 0.45 * (0.5 + 0.5 * sin(self->ind_hint_phase * 2 * G_PI));
        double g = (3.0 + 2.0 * a) * s;
        draw_rounded_rect(cr, x - g, y - g, w + 2 * g, h + 2 * g, r + g);
        cairo_set_source_rgba(cr, 0.35, 0.65, 1.0, a);
        cairo_set_line_width(cr, 2.0 * s);
        cairo_stroke(cr);

    }

    /* Rim, brighter when hovered so it reads as clickable */
    double m = 1.0 * s;
    draw_rounded_rect(cr, x - m, y - m, w + 2 * m, h + 2 * m, r + m);
    cairo_set_source_rgba(cr, 1, 1, 1, self->ind_hover ? 0.9 : 0.35);
    cairo_set_line_width(cr, (self->ind_hover ? 1.5 : 1.0) * s);
    cairo_stroke(cr);
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

        /* Key base */
        draw_rounded_rect(cr, x, y, w, h, radius);
        {
            double fr = r / 255.0, fg = g / 255.0, fb = b / 255.0;
            cairo_set_source_rgb(cr, fr * 0.3 + 0.05,
                                     fg * 0.3 + 0.05,
                                     fb * 0.3 + 0.05);
        }
        cairo_fill(cr);

        /* Key top face (inset) — shows the LED color */
        double inset = 1.5 * fmin(sx, sy);
        if (inset < 1.0) inset = 1.0;
        double tx = x + inset, ty = y + inset;
        double tw = w - 2 * inset, th = h - 2 * inset;
        if (tw > 0 && th > 0) {
            draw_rounded_rect(cr, tx, ty, tw, th, radius * 0.7);
            {
                double fr = r / 255.0, fg = g / 255.0, fb = b / 255.0;
                cairo_set_source_rgb(cr, fr, fg, fb);
            }
            cairo_fill(cr);
        }

        /* Key label */
        if (f87_key_layout[i].name && tw > 8) {
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

        /* Sensor overlay: colored border and value label */
        for (int ov = 0; ov < self->overlay_count; ov++) {
            if (self->overlays[ov].key_id != i) continue;

            uint8_t or_ = self->overlays[ov].color[0];
            uint8_t og  = self->overlays[ov].color[1];
            uint8_t ob  = self->overlays[ov].color[2];

            double bw = 2.0 * fmin(sx, sy);
            draw_rounded_rect(cr, x + bw / 2, y + bw / 2, w - bw, h - bw, radius);
            cairo_set_source_rgba(cr, or_ / 255.0, og / 255.0, ob / 255.0, 0.8);
            cairo_set_line_width(cr, bw);
            cairo_stroke(cr);

            if (self->overlays[ov].label[0] && tw > 12) {
                double lbl_size = th * 0.28;
                if (lbl_size < 4) lbl_size = 4;
                if (lbl_size > 9) lbl_size = 9;
                cairo_set_font_size(cr, lbl_size);
                cairo_set_source_rgba(cr, or_ / 255.0, og / 255.0, ob / 255.0, 0.9);

                cairo_text_extents_t oext;
                cairo_text_extents(cr, self->overlays[ov].label, &oext);
                cairo_move_to(cr, tx + (tw - oext.width) / 2,
                              ty + th - lbl_size * 0.3);
                cairo_show_text(cr, self->overlays[ov].label);
            }
            break;
        }
    }

    draw_indicator(self, cr, sx, sy);
}

static GdkRectangle indicator_rect(F87KeyboardView *self);
static void hint_dismiss(F87KeyboardView *self);

static void on_key_pressed(GtkGestureClick *gesture, int n_press, double x, double y,
                            gpointer user_data)
{
    (void)gesture; (void)n_press;
    F87KeyboardView *self = user_data;

    /* Indicator LED opens its menu, whatever mode the view is in */
    if (self->ind_popover &&
        indicator_hit(x, y, gtk_widget_get_width(GTK_WIDGET(self)),
                      gtk_widget_get_height(GTK_WIDGET(self)))) {
        hint_dismiss(self);
        if (self->ind_open_cb)
            self->ind_open_cb(self->ind_open_data);
        GdkRectangle rect = indicator_rect(self);
        gtk_popover_set_pointing_to(GTK_POPOVER(self->ind_popover), &rect);
        gtk_popover_popup(GTK_POPOVER(self->ind_popover));
        return;
    }

    /* Sensor click mode — single click, no drag */
    if (self->click_mode) {
        int w = gtk_widget_get_width(GTK_WIDGET(self));
        int h = gtk_widget_get_height(GTK_WIDGET(self));
        int key_id = hit_test((int)x, (int)y, w, h);
        if (key_id >= 0 && self->click_cb)
            self->click_cb(key_id, self->click_data);
        return;
    }

    if (!self->paint_mode) return;

    self->paint_dragging = TRUE;
    self->last_painted_key = -1;

    int w = gtk_widget_get_width(GTK_WIDGET(self));
    int h = gtk_widget_get_height(GTK_WIDGET(self));
    int key_id = hit_test((int)x, (int)y, w, h);
    if (key_id >= 0) {
        self->last_painted_key = key_id;
        if (self->paint_cb)
            self->paint_cb(key_id, self->paint_data);
    }
}

static void on_key_released(GtkGestureClick *gesture, int n_press, double x, double y,
                             gpointer user_data)
{
    (void)gesture; (void)n_press; (void)x; (void)y;
    F87KeyboardView *self = user_data;
    self->paint_dragging = FALSE;
    self->last_painted_key = -1;
}

static GdkRectangle indicator_rect(F87KeyboardView *self)
{
    double sx = gtk_widget_get_width(GTK_WIDGET(self)) / REF_W;
    double sy = gtk_widget_get_height(GTK_WIDGET(self)) / REF_H;
    GdkRectangle rect = {
        (int)((IND_X - IND_HW) * sx), (int)((IND_Y - IND_HH) * sy),
        (int)(2 * IND_HW * sx) + 1, (int)(2 * IND_HH * sy) + 1,
    };
    return rect;
}

static void hint_dismiss(F87KeyboardView *self)
{
    if (!self->ind_hint) return;
    F87IndicatorOpenCallback cb = self->ind_dismiss_cb;
    gpointer data = self->ind_dismiss_data;
    f87_keyboard_view_set_indicator_hint(self, FALSE, NULL, NULL);
    if (cb) cb(data);
}

static void on_callout_got_it(GtkButton *btn, gpointer data)
{
    (void)btn;
    hint_dismiss(data);
}

static gboolean callout_show(gpointer data)
{
    F87KeyboardView *self = data;
    self->ind_callout_timer = 0;
    if (self->ind_hint && self->ind_callout && gtk_widget_get_mapped(GTK_WIDGET(self))) {
        GdkRectangle rect = indicator_rect(self);
        gtk_popover_set_pointing_to(GTK_POPOVER(self->ind_callout), &rect);
        gtk_popover_popup(GTK_POPOVER(self->ind_callout));
    }
    return G_SOURCE_REMOVE;
}

static GtkWidget *callout_new(F87KeyboardView *self)
{
    GtkBox *box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10));
    gtk_widget_set_margin_start(GTK_WIDGET(box), 6);
    gtk_widget_set_margin_end(GTK_WIDGET(box), 4);
    gtk_widget_set_margin_top(GTK_WIDGET(box), 4);
    gtk_widget_set_margin_bottom(GTK_WIDGET(box), 4);

    GtkWidget *icon = gtk_image_new_from_icon_name("starred-symbolic");
    gtk_widget_add_css_class(icon, "accent");
    gtk_box_append(box, icon);

    GtkLabel *label = GTK_LABEL(gtk_label_new(NULL));
    char *markup = g_markup_printf_escaped("<b>%s</b> %s", _("New:"),
        _("click the LED to change its mode, colour and speed"));
    gtk_label_set_markup(label, markup);
    g_free(markup);
    gtk_label_set_wrap(label, TRUE);
    gtk_label_set_max_width_chars(label, 30);
    gtk_label_set_xalign(label, 0);
    gtk_box_append(box, GTK_WIDGET(label));

    GtkWidget *btn = gtk_button_new_with_label(_("Got it"));
    gtk_widget_add_css_class(btn, "suggested-action");
    gtk_widget_add_css_class(btn, "pill");
    gtk_widget_set_valign(btn, GTK_ALIGN_CENTER);
    g_signal_connect(btn, "clicked", G_CALLBACK(on_callout_got_it), self);
    gtk_box_append(box, btn);

    GtkWidget *pop = gtk_popover_new();
    gtk_popover_set_child(GTK_POPOVER(pop), GTK_WIDGET(box));
    gtk_popover_set_autohide(GTK_POPOVER(pop), FALSE);  /* stays until dismissed */
    gtk_popover_set_position(GTK_POPOVER(pop), GTK_POS_RIGHT);
    gtk_widget_set_parent(pop, GTK_WIDGET(self));
    return pop;
}

static void on_view_map(GtkWidget *widget, gpointer data)
{
    (void)data;
    F87KeyboardView *self = F87_KEYBOARD_VIEW(widget);
    /* Give the window a moment to settle before pointing at the LED */
    if (self->ind_hint && !self->ind_callout_timer)
        self->ind_callout_timer = g_timeout_add(600, callout_show, self);
}

static gboolean hint_tick(GtkWidget *widget, GdkFrameClock *clock, gpointer data)
{
    (void)data;
    F87KeyboardView *self = F87_KEYBOARD_VIEW(widget);
    self->ind_hint_phase = fmod(gdk_frame_clock_get_frame_time(clock) / 1.6e6, 1.0);
    gtk_widget_queue_draw(widget);
    return G_SOURCE_CONTINUE;
}

static gboolean on_query_tooltip(GtkWidget *widget, int x, int y,
                                 gboolean keyboard_mode, GtkTooltip *tooltip,
                                 gpointer data)
{
    (void)keyboard_mode; (void)data;
    F87KeyboardView *self = F87_KEYBOARD_VIEW(widget);
    if (!self->ind_popover ||
        !indicator_hit(x, y, gtk_widget_get_width(widget), gtk_widget_get_height(widget)))
        return FALSE;
    gtk_tooltip_set_text(tooltip, _("ESC indicator LED — click to change"));
    return TRUE;
}

static void restore_cursor(F87KeyboardView *self)
{
    if (self->paint_mode)
        gtk_widget_set_cursor_from_name(GTK_WIDGET(self), "cell");
    else if (self->click_mode)
        gtk_widget_set_cursor_from_name(GTK_WIDGET(self), "crosshair");
    else
        gtk_widget_set_cursor(GTK_WIDGET(self), NULL);
}

static void set_indicator_hover(F87KeyboardView *self, gboolean hover)
{
    if (hover == self->ind_hover) return;
    self->ind_hover = hover;
    if (hover)
        gtk_widget_set_cursor_from_name(GTK_WIDGET(self), "pointer");
    else
        restore_cursor(self);
    gtk_widget_queue_draw(GTK_WIDGET(self));
}

static void on_motion_leave(GtkEventControllerMotion *motion, gpointer data)
{
    (void)motion;
    set_indicator_hover(data, FALSE);
}

static void on_paint_motion(GtkEventControllerMotion *motion,
                             double x, double y, gpointer data)
{
    (void)motion;
    F87KeyboardView *self = data;

    if (self->ind_popover)
        set_indicator_hover(self, indicator_hit(x, y,
                            gtk_widget_get_width(GTK_WIDGET(self)),
                            gtk_widget_get_height(GTK_WIDGET(self))));

    if (!self->paint_mode || !self->paint_dragging) return;

    int w = gtk_widget_get_width(GTK_WIDGET(self));
    int h = gtk_widget_get_height(GTK_WIDGET(self));
    int key_id = hit_test((int)x, (int)y, w, h);
    if (key_id >= 0 && key_id != self->last_painted_key) {
        self->last_painted_key = key_id;
        if (self->paint_cb)
            self->paint_cb(key_id, self->paint_data);
    }
}

static void f87_keyboard_view_init(F87KeyboardView *self)
{
    memset(self->colors, 0, sizeof(self->colors));
    self->last_painted_key = -1;
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(self), draw_func, NULL, NULL);

    /* Responsive: expand horizontally, keep aspect ratio via content size */
    gtk_widget_set_size_request(GTK_WIDGET(self), 500, -1);
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(self), 690);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(self), 255);
    gtk_widget_set_halign(GTK_WIDGET(self), GTK_ALIGN_FILL);
    gtk_widget_set_valign(GTK_WIDGET(self), GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(GTK_WIDGET(self), TRUE);
    gtk_widget_set_vexpand(GTK_WIDGET(self), FALSE);

    /* Click handler for paint mode */
    GtkGesture *click = gtk_gesture_click_new();
    g_signal_connect(click, "pressed", G_CALLBACK(on_key_pressed), self);
    g_signal_connect(click, "released", G_CALLBACK(on_key_released), self);
    gtk_widget_add_controller(GTK_WIDGET(self), GTK_EVENT_CONTROLLER(click));

    /* Motion handler for drag-to-paint */
    GtkEventController *motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion", G_CALLBACK(on_paint_motion), self);
    g_signal_connect(motion, "leave", G_CALLBACK(on_motion_leave), self);
    gtk_widget_add_controller(GTK_WIDGET(self), motion);

    gtk_widget_set_has_tooltip(GTK_WIDGET(self), TRUE);
    g_signal_connect(self, "query-tooltip", G_CALLBACK(on_query_tooltip), NULL);
    g_signal_connect(self, "map", G_CALLBACK(on_view_map), NULL);
}

/* A popover parented to this widget must be presented on every allocation */
static void f87_keyboard_view_size_allocate(GtkWidget *widget, int width,
                                             int height, int baseline)
{
    GTK_WIDGET_CLASS(f87_keyboard_view_parent_class)->size_allocate(
        widget, width, height, baseline);
    F87KeyboardView *self = F87_KEYBOARD_VIEW(widget);
    if (self->ind_popover)
        gtk_popover_present(GTK_POPOVER(self->ind_popover));
    if (self->ind_callout) {
        GdkRectangle rect = indicator_rect(self);
        gtk_popover_set_pointing_to(GTK_POPOVER(self->ind_callout), &rect);
        gtk_popover_present(GTK_POPOVER(self->ind_callout));
    }
}

static void f87_keyboard_view_dispose(GObject *obj)
{
    F87KeyboardView *self = F87_KEYBOARD_VIEW(obj);
    g_clear_pointer(&self->ind_popover, gtk_widget_unparent);
    g_clear_pointer(&self->ind_callout, gtk_widget_unparent);
    if (self->ind_callout_timer) {
        g_source_remove(self->ind_callout_timer);
        self->ind_callout_timer = 0;
    }
    G_OBJECT_CLASS(f87_keyboard_view_parent_class)->dispose(obj);
}

static void f87_keyboard_view_class_init(F87KeyboardViewClass *klass)
{
    G_OBJECT_CLASS(klass)->dispose = f87_keyboard_view_dispose;
    GTK_WIDGET_CLASS(klass)->size_allocate = f87_keyboard_view_size_allocate;
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

void f87_keyboard_view_set_overlays(F87KeyboardView *view,
                                     const f87_key_overlay_t *overlays, int count)
{
    if (count > F87_KEY_COUNT) count = F87_KEY_COUNT;
    memcpy(view->overlays, overlays, count * sizeof(f87_key_overlay_t));
    view->overlay_count = count;
    gtk_widget_queue_draw(GTK_WIDGET(view));
}

void f87_keyboard_view_clear_overlays(F87KeyboardView *view)
{
    view->overlay_count = 0;
    gtk_widget_queue_draw(GTK_WIDGET(view));
}

void f87_keyboard_view_set_click_mode(F87KeyboardView *view, gboolean enabled,
                                       F87KeyClickCallback cb, gpointer user_data)
{
    view->click_mode = enabled;
    view->click_cb = cb;
    view->click_data = user_data;

    if (enabled)
        gtk_widget_set_cursor_from_name(GTK_WIDGET(view), "crosshair");
    else if (!view->paint_mode)
        gtk_widget_set_cursor(GTK_WIDGET(view), NULL);
}

void f87_keyboard_view_set_indicator(F87KeyboardView *view,
                                      f87_indicator_style_t style,
                                      uint8_t r, uint8_t g, uint8_t b)
{
    view->ind_style = style;
    view->ind_color[0] = r;
    view->ind_color[1] = g;
    view->ind_color[2] = b;
    gtk_widget_queue_draw(GTK_WIDGET(view));
}

void f87_keyboard_view_set_indicator_hint(F87KeyboardView *view, gboolean hint,
                                           F87IndicatorOpenCallback on_dismiss,
                                           gpointer user_data)
{
    view->ind_hint = hint;
    view->ind_dismiss_cb = on_dismiss;
    view->ind_dismiss_data = user_data;

    if (hint && !view->ind_callout)
        view->ind_callout = callout_new(view);
    else if (!hint && view->ind_callout) {
        gtk_popover_popdown(GTK_POPOVER(view->ind_callout));
        g_clear_pointer(&view->ind_callout, gtk_widget_unparent);
    }
    if (hint && gtk_widget_get_mapped(GTK_WIDGET(view)) && !view->ind_callout_timer)
        view->ind_callout_timer = g_timeout_add(600, callout_show, view);

    if (hint && !view->ind_hint_tick)
        view->ind_hint_tick = gtk_widget_add_tick_callback(GTK_WIDGET(view),
                                                           hint_tick, NULL, NULL);
    else if (!hint && view->ind_hint_tick) {
        gtk_widget_remove_tick_callback(GTK_WIDGET(view), view->ind_hint_tick);
        view->ind_hint_tick = 0;
    }
    gtk_widget_queue_draw(GTK_WIDGET(view));
}

void f87_keyboard_view_set_indicator_popover(F87KeyboardView *view,
                                              GtkWidget *popover,
                                              F87IndicatorOpenCallback on_open,
                                              gpointer user_data)
{
    g_clear_pointer(&view->ind_popover, gtk_widget_unparent);
    view->ind_popover = popover;
    view->ind_open_cb = on_open;
    view->ind_open_data = user_data;
    if (popover)
        gtk_widget_set_parent(popover, GTK_WIDGET(view));
}
