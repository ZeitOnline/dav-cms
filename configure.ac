# -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.
#
# Copyright 2003-2007, 2010 R. Mattes <rm@seid-online.de>
#
# Licence: GPL
#

AC_PREREQ(2.57) 
AC_INIT([mod_dav_cms],[0.2], [rm (at) seid-online (dot) de])
AC_CONFIG_SRCDIR([mod_dav_cms.c])
# AC_CONFIG_HEADER([config.h])

# Checks for programs.
AC_PROG_CC
AC_PROG_MAKE_SET
# AC_PROG_INSTALL

# Checks for libraries.

# Checks for header files.

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST

# Checks for helper programs
AC_ARG_WITH(apxs,
    AS_HELP_STRING(--with-apxs=PATH,/path/to/apxs),
    [ AC_PATH_PROGS(APXS, [apxs2 apxs], "notfound", "$withval:$PATH") ], 
    [ AC_PATH_PROGS(APXS, [apxs2 apxs], "notfound", "/usr/local/apache/bin:/usr/local/bin:/usr/sbin:$PATH")])

if test "x$APXS" = "xnotfound" ; then
    AC_MSG_ERROR([*** Sorry, could not find apxs ***])
fi

AC_ARG_WITH(apr-config,
    AS_HELP_STRING(--with-apr-config=PATH,/path/to/apr-config),
    [ AC_PATH_PROGS(APRCONFIG, [apr-config], "notfound", "$withval:$PATH") ], 
    [ AC_PATH_PROGS(APRCONFIG, [apr-config], "notfound", "/usr/local/apache/bin:/usr/local/bin:/usr/sbin:$PATH")])

if test "x$APRCONFIG" = "xnotfound" ; then
    AC_MSG_ERROR([*** Sorry, could not find ap-config ***])
fi

AC_ARG_WITH(apachectl,
    AS_HELP_STRING(--with-apachectl=PATH,/path/to/apachectl),
    [ AC_PATH_PROGS(APACHECTL, [apache2ctl apachectl], "notfound", "$withval:$PATH") ], 
    [ AC_PATH_PROGS(APACHECTL, [apache2ctl apachectl], "notfound", "/usr/local/apache/bin:/usr/local/bin:/usr/sbin:$PATH")])

AC_ARG_WITH(pg_config,
    AS_HELP_STRING(--with-pg_config=PATH,/path/to/pg_config),
    [ AC_PATH_PROGS(PGCONFIG, [pg_config], "notfound", "$withval:$PATH") ], 
    [ AC_PATH_PROGS(PGCONFIG, [pg_config], "notfound", "/usr/local/apache/bin:/usr/local/bin:/usr/sbin:$PATH")])

if test "x$PGCONFIG" = "xnotfound" ; then
    AC_MSG_ERROR([*** Sorry, could not find pg_config ***])
fi


PKG_CHECK_MODULES([DAV_CMS], [uuid])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
