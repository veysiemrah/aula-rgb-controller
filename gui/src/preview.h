#ifndef F87_PREVIEW_H
#define F87_PREVIEW_H

#include "keyboard_view.h"
#include <stdint.h>

typedef struct f87_preview f87_preview_t;

f87_preview_t *f87_preview_new(F87KeyboardView *keyboard);
void f87_preview_destroy(f87_preview_t *prev);

/* Start preview animation for given effect. speed: 0-4 */
void f87_preview_start(f87_preview_t *prev, int effect_id, const char *category,
                        uint8_t speed, uint8_t r, uint8_t g, uint8_t b);

/* Update speed/color/colorful while running */
void f87_preview_set_speed(f87_preview_t *prev, uint8_t speed);
void f87_preview_set_color(f87_preview_t *prev, uint8_t r, uint8_t g, uint8_t b);
void f87_preview_set_colorful(f87_preview_t *prev, int colorful);

/* Stop preview */
void f87_preview_stop(f87_preview_t *prev);

/* Inject a keypress into the preview (for reactive effect demo) */
void f87_preview_on_key(f87_preview_t *prev, int key_id);

/* Check if effect is reactive (needs keypress input) */
int f87_preview_is_reactive(int effect_id);

#endif
