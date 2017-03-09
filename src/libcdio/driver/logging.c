/*
  Copyright (C) 2003, 2004, 2008, 2011, 2012, 2015
  Rocky Bernstein <rocky@gnu.org>
  Copyright (C) 2000 Herbert Valerio Riedel <hvr@gnu.org>

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
# define __CDIO_CONFIG_H__ 1
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STDARG_H
#include <stdarg.h>
#endif
#ifdef HAVE_STDIO_H
#include <stdio.h>
#endif

#include <cdio/logging.h>
#include "cdio_assert.h"
#include "portable.h"
#include <assert.h>

cdio_log_level_t cdio_loglevel_default = CDIO_LOG_WARN;

extern void
cdio_default_log_handler(cdio_log_level_t level, const char message[])
{
  switch (level)
    {
    case CDIO_LOG_ERROR:
      if (level >= cdio_loglevel_default) {
        fprintf (stderr, "**ERROR: %s\n", message);
        fflush (stderr);
      }
      exit (EXIT_FAILURE);
      break;
    case CDIO_LOG_DEBUG:
      if (level >= cdio_loglevel_default) {
        fprintf (stdout, "--DEBUG: %s\n", message);
      }
      break;
    case CDIO_LOG_WARN:
      if (level >= cdio_loglevel_default) {
        fprintf (stdout, "++ WARN: %s\n", message);
      }
      break;
    case CDIO_LOG_INFO:
      if (level >= cdio_loglevel_default) {
        fprintf (stdout, "   INFO: %s\n", message);
      }
      break;
    case CDIO_LOG_ASSERT:
      if (level >= cdio_loglevel_default) {
        fprintf (stderr, "!ASSERT: %s\n", message);
        fflush (stderr);
      }
      abort ();
      break;
    default:
      cdio_assert_not_reached ();
      break;
    }

  fflush (stdout);
}

cdio_log_handler_t _handler = cdio_default_log_handler;

cdio_log_handler_t
cdio_log_set_handler(cdio_log_handler_t new_handler)
{
  cdio_log_handler_t old_handler = _handler;

  _handler = new_handler;

  return old_handler;
}

static void
cdio_logv(cdio_log_level_t level, const char format[], va_list args)
{
  char buf[1024] = { 0, };

  /* _handler() is user defined and we want to make sure _handler()
  doesn't call us, cdio_logv. in_recursion is used for that, however
  it has a problem in multi-threaded programs. I'm not sure how to
  handle multi-threading and recursion checking both. For now, we'll
  leave in the recursion checking, at the expense of handling
  multi-threaded log calls. To ameliorate this, we'll check the log
  level and handle calls where there is no output, before the
  recursion check.
  */
 static int in_recursion = 0;

  if (level < cdio_loglevel_default) return;

  if (in_recursion) {
    /* Can't use cdio_assert_not_reached() as that may call cdio_logv */
    assert(0);
  }

  in_recursion = 1;

  vsnprintf(buf, sizeof(buf)-1, format, args);

  _handler(level, buf);

  in_recursion = 0;
}

void
cdio_log(cdio_log_level_t level, const char format[], ...)
{
  va_list args;
  va_start (args, format);
  cdio_logv (level, format, args);
  va_end (args);
}

#define CDIO_LOG_TEMPLATE(level, LEVEL) \
void \
cdio_ ## level (const char format[], ...) \
{ \
  va_list args; \
  va_start (args, format); \
  cdio_logv (CDIO_LOG_ ## LEVEL, format, args); \
  va_end (args); \
}

CDIO_LOG_TEMPLATE(debug, DEBUG)
CDIO_LOG_TEMPLATE(info, INFO)
CDIO_LOG_TEMPLATE(warn, WARN)
CDIO_LOG_TEMPLATE(error, ERROR)

#undef CDIO_LOG_TEMPLATE


/*
 * Local variables:
 *  c-file-style: "gnu"
 *  tab-width: 8
 *  indent-tabs-mode: nil
 * End:
 */
