#ifndef F87_KEYBOARD_VIEW_H
#define F87_KEYBOARD_VIEW_H

#include <adwaita.h>
#include <stdint.h>

#define F87_TYPE_KEYBOARD_VIEW (f87_keyboard_view_get_type())
G_DECLARE_FINAL_TYPE(F87KeyboardView, f87_keyboard_view, F87, KEYBOARD_VIEW, GtkDrawingArea)

F87KeyboardView *f87_keyboard_view_new(void);
void f87_keyboard_view_set_color(F87KeyboardView *view, uint8_t r, uint8_t g, uint8_t b);
void f87_keyboard_view_set_key(F87KeyboardView *view, int key_id,
                                uint8_t r, uint8_t g, uint8_t b);
void f87_keyboard_view_clear(F87KeyboardView *view);
void f87_keyboard_view_set_all_keys(F87KeyboardView *view,
                                     const uint8_t colors[][3], int count);

/* Interactive paint mode for Custom effect */
typedef void (*F87KeyPaintCallback)(int key_id, gpointer user_data);
void f87_keyboard_view_set_paint_mode(F87KeyboardView *view, gboolean enabled,
                                       F87KeyPaintCallback cb, gpointer user_data);

/* Get per-key color array (88x3 RGB) — for sending to daemon */
const uint8_t (*f87_keyboard_view_get_colors(F87KeyboardView *view))[3];

/* Sensor overlay — shows colored borders and labels on assigned keys */
typedef struct {
    int key_id;
    uint8_t color[3];
    char label[16];
} f87_key_overlay_t;

void f87_keyboard_view_set_overlays(F87KeyboardView *view,
                                     const f87_key_overlay_t *overlays, int count);
void f87_keyboard_view_clear_overlays(F87KeyboardView *view);

/* Sensor placement mode — single-click callback (no drag) */
typedef void (*F87KeyClickCallback)(int key_id, gpointer user_data);
void f87_keyboard_view_set_click_mode(F87KeyboardView *view, gboolean enabled,
                                       F87KeyClickCallback cb, gpointer user_data);

/* Indicator LED between ESC and F1 (the "battery light") */
typedef enum {
    F87_INDICATOR_OFF,
    F87_INDICATOR_SOLID,    /* r, g, b */
    F87_INDICATOR_RAINBOW,  /* rainbow / colour-cycle modes */
} f87_indicator_style_t;

void f87_keyboard_view_set_indicator(F87KeyboardView *view,
                                      f87_indicator_style_t style,
                                      uint8_t r, uint8_t g, uint8_t b);

/* Popover shown when the indicator LED is clicked. The view becomes its
 * parent. on_open runs just before it pops up (e.g. to refresh its state). */
typedef void (*F87IndicatorOpenCallback)(gpointer user_data);

/* New-user hint: pulse the LED and show a callout bubble pointing at it.
 * on_dismiss runs once when the user clicks "Got it" or opens the LED menu. */
void f87_keyboard_view_set_indicator_hint(F87KeyboardView *view, gboolean hint,
                                           F87IndicatorOpenCallback on_dismiss,
                                           gpointer user_data);
void f87_keyboard_view_set_indicator_popover(F87KeyboardView *view,
                                              GtkWidget *popover,
                                              F87IndicatorOpenCallback on_open,
                                              gpointer user_data);

#endif /* F87_KEYBOARD_VIEW_H */
