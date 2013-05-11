#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([PkgClip], [1.2.0], [i.am.jack.mail@gmail.com])
AC_CONFIG_SRCDIR([main.c])
AC_CONFIG_HEADERS([config.h])

AC_SYS_LARGEFILE

AM_INIT_AUTOMAKE([-Wall -Werror foreign silent-rules])
AM_SILENT_RULES([yes])

# Checks for programs.
AC_PROG_CC_C99
AC_PROG_INSTALL
AM_PROG_CC_C_O

# Option to use git version
AC_ARG_ENABLE([git-version],
		AS_HELP_STRING([--enable-git-version], [enable the use of git version]),
	[wantgitver=$enableval], [wantgitver=no])

# Checks for libraries.
AC_CHECK_LIB([alpm], [alpm_db_get_pkg], ,
	AC_MSG_ERROR([libalpm is required]))

# Checks for GTK+3
PKG_CHECK_MODULES(GTK, [gtk+-3.0], , AC_MSG_ERROR([GTK+3 is required]))

# Checks for PolicyKit
PKG_CHECK_MODULES(POLKIT, [polkit-gobject-1], ,
	AC_MSG_ERROR([PolicyKit is required]))

# Checks for header files.
AC_CHECK_HEADERS([stdlib.h string.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_OFF_T
AC_TYPE_SIZE_T

# Checks for library functions.
AC_FUNC_REALLOC
AC_CHECK_FUNCS([memmove memset strchr strdup strerror strrchr])

# git version
AC_MSG_CHECKING([if git version must be used])
if test "x$wantgitver" = "xyes"; then
	AC_MSG_RESULT([yes])
	AC_CHECK_PROGS([GIT], [git])
	if test "x$GIT" = "x"; then
		AC_MSG_ERROR([Cannot use git version: git not found])
	fi
	AC_CHECK_FILE([.git/], hasgitdir=yes)
	if test "x$hasgitdir" = "xyes"; then
		AC_DEFINE([USE_GIT_VERSION], , [Use GIT version])
		usegitver=yes
		gitver=-git
	else
		AC_MSG_ERROR([Cannot use git version: .git not found])
	fi
else
	AC_MSG_RESULT([no])
	usegitver=no
	gitver=
fi
AM_CONDITIONAL(USE_GIT_VERSION, test "x$usegitver" = "xyes")

AC_CONFIG_FILES([Makefile])
AC_OUTPUT

echo "
        ${PACKAGE} version ${PACKAGE_VERSION}${gitver}

 Build information:
   source code location         : ${srcdir}
   prefix                       : ${prefix}

 Install paths:
   binaries                     : $(eval echo $(eval echo ${bindir}))
   documentation                : $(eval echo $(eval echo ${docdir}))
   man pages                    : $(eval echo $(eval echo ${mandir}))

"

