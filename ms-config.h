/* Option for Visual Studio and WDK compilation  */
/* For MinGW, use "./configure <option>" instead */

#if defined(_MSC_VER)

/* Embed FreeDOS files and allow FreeDOS support */
#define WITH_FREEDOS

/* SysLinux support, for ISO -> bootable USB */
#define WITH_SYSLINUX

#endif /* _MSC_VER */
