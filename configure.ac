dnl Initialize autotools
AC_INIT([pigdo], [0.0])
AM_INIT_AUTOMAKE([-Wall -Werror foreign subdir-objects])

dnl Check toolchain
AC_PROG_CC
AC_PROG_RANLIB
AM_PROG_AR

dnl > 2GiB file support on 32-bit platforms
AC_SYS_LARGEFILE

dnl Always search /usr/local, for systems where dependency libraries may be
dnl located there by default (e.g. BSDs, where curl is not in the base system)
CPPFLAGS="$CPPFLAGS -I/usr/local/include"
LDFLAGS="$LDFLAGS -L/usr/local/lib"

dnl Check for libraries:
AC_CHECK_LIB([z], [inflate])
AC_CHECK_LIB([bz2], [BZ2_bzDecompress])
AC_CHECK_LIB([curl], [curl_global_init])

dnl Check for functions:
AC_CHECK_FUNCS([posix_fallocate])

dnl Generate files
AC_CONFIG_FILES([Makefile])
AC_CONFIG_HEADERS([config.h])

dnl Set correct flags for pthread support, uses ax_pthread.m4 from the archive
AX_PTHREAD
LIBS="$PTHREAD_LIBS $LIBS"
CFLAGS="$CFLAGS $PTHREAD_CFLAGS -Wall -Werror"
CC="$PTHREAD_CC"

AC_OUTPUT
