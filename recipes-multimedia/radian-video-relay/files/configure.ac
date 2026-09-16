AC_INIT([radian-video-relay], [1.0.0], [support@radian-tech.com])
AM_INIT_AUTOMAKE([foreign subdir-objects])
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_HEADERS([config.h])

AC_PROG_CC
PKG_PROG_PKG_CONFIG
PKG_CHECK_MODULES([RELAY], [glib-2.0 gstreamer-1.0])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
