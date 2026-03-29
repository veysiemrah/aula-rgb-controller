#ifndef F87_SIDEBAR_H
#define F87_SIDEBAR_H

#include <adwaita.h>

typedef void (*F87SidebarCallback)(const char *category, const char *effect_name,
                                    int effect_id, gpointer user_data);

GtkWidget *f87_sidebar_create(F87SidebarCallback callback, gpointer user_data);

#endif /* F87_SIDEBAR_H */
