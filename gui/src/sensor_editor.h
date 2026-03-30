#ifndef F87_SENSOR_EDITOR_H
#define F87_SENSOR_EDITOR_H

#include <adwaita.h>
#include "keyboard_view.h"
#include "app_state.h"

typedef struct _F87SensorEditor F87SensorEditor;

F87SensorEditor *f87_sensor_editor_new(F87KeyboardView *keyboard, f87_app_state_t *state);
void f87_sensor_editor_destroy(F87SensorEditor *editor);

GtkWidget *f87_sensor_editor_get_widget(F87SensorEditor *editor);

void f87_sensor_editor_activate(F87SensorEditor *editor);
void f87_sensor_editor_deactivate(F87SensorEditor *editor);

/* Returns profile name. Sets *config_path to JSON path for custom, NULL for builtins. */
const char *f87_sensor_editor_get_profile(F87SensorEditor *editor,
                                           const char **config_path);

uint8_t f87_sensor_editor_get_brightness(F87SensorEditor *editor);
gboolean f87_sensor_editor_is_custom(F87SensorEditor *editor);

#endif /* F87_SENSOR_EDITOR_H */
