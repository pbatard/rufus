/** \file version.h
 *
 *  \brief A file containing the libcdio package version
 *  number (20201) and OS build name.
 */

/*! CDIO_VERSION is a C-Preprocessor macro of a string that shows what
    version is used.  cdio_version_string has the same value, but it is a
    constant variable that can be accessed at run time. */
#define CDIO_VERSION "2.2.1 (Rufus)"
extern const char *cdio_version_string; /**< = CDIO_VERSION */

/*! LIBCDIO_VERSION_NUM is a C-Preprocessor macro that can be used for
    testing in the C preprocessor. libcdio_version_num has the same
    value, but it is a constant variable that can be accessed at run
    time.  */
#define LIBCDIO_VERSION_NUM 20201

extern const unsigned int libcdio_version_num; /**< = LIBCDIO_VERSION_NUM */
