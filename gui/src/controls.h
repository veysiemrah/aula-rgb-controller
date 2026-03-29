#ifndef F87_CONTROLS_H
#define F87_CONTROLS_H

#include <adwaita.h>
#include "app_state.h"
#include "keyboard_view.h"

typedef struct _F87Controls F87Controls;

typedef void (*F87ControlsStatusCallback)(const char *text, gpointer user_data);

F87Controls *f87_controls_new(f87_app_state_t *state,
                               F87ControlsStatusCallback status_cb,
                               gpointer user_data);

void f87_controls_set_effect(F87Controls *ctrl, const char *category,
                              const char *effect_name, int effect_id);

void f87_controls_set_keyboard(F87Controls *ctrl, F87KeyboardView *keyboard);

GtkWidget *f87_controls_get_widget(F87Controls *ctrl);

#endif /* F87_CONTROLS_H */
