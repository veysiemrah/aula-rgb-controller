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

#endif /* F87_KEYBOARD_VIEW_H */
