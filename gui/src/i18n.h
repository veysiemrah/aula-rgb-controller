#ifndef F87_I18N_H
#define F87_I18N_H

#include <libintl.h>
#include <locale.h>

#define _(str) gettext(str)
#define N_(str) str

void f87_i18n_init(void);

#endif /* F87_I18N_H */
