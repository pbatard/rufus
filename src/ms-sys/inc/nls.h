#ifndef NLS_H
#define NLS_H

#include <libintl.h>

#ifdef NO_LIBINTL_OR_GETTEXT
#define _(String) (String)
#else
#define _(String) gettext(String)
#endif
#define gettext_noop(String) (String)
#define N_(String) gettext_noop(String)

/* Init Native language support */
void nls_init(void);

#endif
