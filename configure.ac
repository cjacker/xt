AC_INIT([xt],[0.2],[cjacker@foxmail.com])

AM_INIT_AUTOMAKE([foreign dist-xz no-dist-gzip tar-pax])

AC_PROG_CC

AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_LIBTOOL

AC_CONFIG_MACRO_DIR([m4])

PKG_CHECK_MODULES(XT, gtk+-3.0 >= 3.0.0 vte-2.91)
AC_SUBST(XT_CFLAGS)
AC_SUBST(XT_LIBS)

AC_OUTPUT([
Makefile
src/Makefile
])
