#ifndef F87_WINDOW_H
#define F87_WINDOW_H

#include <adwaita.h>

#define F87_TYPE_WINDOW (f87_window_get_type())
G_DECLARE_FINAL_TYPE(F87Window, f87_window, F87, WINDOW, AdwApplicationWindow)

F87Window *f87_window_new(AdwApplication *app);

#endif /* F87_WINDOW_H */
