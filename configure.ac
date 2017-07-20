dnl Initialize autotools
AC_INIT([pigdo], [0.0])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])

dnl Check toolchain
AC_PROG_CC

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
