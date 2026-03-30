#include "i18n.h"
#include <stdlib.h>

#ifndef LOCALEDIR
#define LOCALEDIR "/usr/local/share/locale"
#endif

#define GETTEXT_PACKAGE "f87control"

void f87_i18n_init(void)
{
    setlocale(LC_ALL, "");
    const char *dir = getenv("LOCALEDIR");
    if (!dir) dir = LOCALEDIR;
    bindtextdomain(GETTEXT_PACKAGE, dir);
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain(GETTEXT_PACKAGE);
}
