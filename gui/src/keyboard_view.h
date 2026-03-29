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

#endif /* F87_KEYBOARD_VIEW_H */
