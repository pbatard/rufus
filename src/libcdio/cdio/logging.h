/*
    Copyright (C) 2003, 2004, 2008, 2012 Rocky Bernstein <rocky@gnu.org>
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

/** \file logging.h
 *  \brief Header to control logging and level of detail of output.
 *
 */

#ifndef CDIO_LOGGING_H_
#define CDIO_LOGGING_H_

#include <cdio/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The different log levels supported.
 */
typedef enum {
  CDIO_LOG_DEBUG = 1, /**< Debug-level messages - helps debug what's up. */
  CDIO_LOG_INFO,      /**< Informational - indicates perhaps something of
                           interest. */
  CDIO_LOG_WARN,      /**< Warning conditions - something that looks funny. */
  CDIO_LOG_ERROR,     /**< Error conditions - may terminate program.  */
  CDIO_LOG_ASSERT     /**< Critical conditions - may abort program. */
} cdio_log_level_t;

/**
 * The place to save the preference concerning how much verbosity
 * is desired. This is used by the internal default log handler, but
 * it could be use by applications which provide their own log handler.
 */
extern cdio_log_level_t cdio_loglevel_default;

/**
 * This type defines the signature of a log handler.  For every
 * message being logged, the handler will receive the log level and
 * the message string.
 *
 * @see cdio_log_set_handler
 * @see cdio_log_level_t
 *
 * @param level   The log level.
 * @param message The log message.
 */
typedef void (*cdio_log_handler_t) (cdio_log_level_t level,
                                    const char message[]);

/**
 * The initial or default log handler in effect.
 *
 * @param level   The log level.
 * @param message The log message.
 */
extern void cdio_default_log_handler(cdio_log_level_t level, const char message[]);

/**
 * Set a custom log handler for libcdio.  The return value is the log
 * handler being replaced.  If the provided parameter is NULL, then
 * the handler will be reset to the default handler.
 *
 * @see cdio_log_handler_t
 *
 * @param new_handler The new log handler.
 * @return The previous log handler.
 */
cdio_log_handler_t cdio_log_set_handler (cdio_log_handler_t new_handler);

/**
 * Handle an message with the given log level.
 *
 * @see cdio_debug
 * @see cdio_info
 * @see cdio_warn
 * @see cdio_error

 * @param level   The log level.
 * @param format  printf-style format string
 * @param ...     remaining arguments needed by format string
 */
void cdio_log (cdio_log_level_t level,
               const char format[], ...) GNUC_PRINTF(2, 3);

/**
 * Handle a debugging message.
 *
 * @see cdio_log for a more generic routine
 */
void cdio_debug (const char format[], ...) GNUC_PRINTF(1,2);

/**
 * Handle an informative message.
 *
 * @see cdio_log for a more generic routine
 */
void cdio_info (const char format[], ...) GNUC_PRINTF(1,2);

/**
 * Handle a warning message.
 *
 * @see cdio_log for a more generic routine
 */
void cdio_warn (const char format[], ...) GNUC_PRINTF(1,2);

/**
 * Handle an error message. Execution is terminated.
 *
 * @see cdio_log for a more generic routine.
 */
void cdio_error (const char format[], ...) GNUC_PRINTF(1,2);

#ifdef __cplusplus
}
#endif

#endif /* CDIO_LOGGING_H_ */


/*
 * Local variables:
 *  c-file-style: "gnu"
 *  tab-width: 8
 *  indent-tabs-mode: nil
 * End:
 */
