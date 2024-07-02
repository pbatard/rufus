#ifdef DECLSPEC_IMPORT
#undef DECLSPEC_IMPORT
#endif 

#ifndef DECLSPEC_IMPORT
#if (defined (__i386__) || defined (__ia64__) || defined (__x86_64__) || defined (__arm__) || defined(__aarch64__)) && !defined (__WIDL__)
#ifdef __GNUC__
#define DECLSPEC_IMPORT __attribute__((visibility ("hidden")))
#else
#define DECLSPEC_IMPORT __declspec(dllimport)
#endif
#else
#define DECLSPEC_IMPORT
#endif
#endif