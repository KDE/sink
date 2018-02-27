
#ifndef HAWD_EXPORT_H
#define HAWD_EXPORT_H

#ifdef HAWD_STATIC_DEFINE
#  define HAWD_EXPORT
#  define HAWD_NO_EXPORT
#else
#  ifndef HAWD_EXPORT
#    ifdef libhawd_EXPORTS
        /* We are building this library */
#      define HAWD_EXPORT __attribute__((visibility("default")))
#    else
        /* We are using this library */
#      define HAWD_EXPORT __attribute__((visibility("default")))
#    endif
#  endif

#  ifndef HAWD_NO_EXPORT
#    define HAWD_NO_EXPORT __attribute__((visibility("hidden")))
#  endif
#endif

#ifndef HAWD_DEPRECATED
#  define HAWD_DEPRECATED __attribute__ ((__deprecated__))
#endif

#ifndef HAWD_DEPRECATED_EXPORT
#  define HAWD_DEPRECATED_EXPORT HAWD_EXPORT HAWD_DEPRECATED
#endif

#ifndef HAWD_DEPRECATED_NO_EXPORT
#  define HAWD_DEPRECATED_NO_EXPORT HAWD_NO_EXPORT HAWD_DEPRECATED
#endif

#if 0 /* DEFINE_NO_DEPRECATED */
#  ifndef HAWD_NO_DEPRECATED
#    define HAWD_NO_DEPRECATED
#  endif
#endif

#endif
