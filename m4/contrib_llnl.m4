dnl SYNOPSIS: CONTRIB_LLNL
dnl add switches for llnl lustre/nvidia plugins
AC_DEFUN([CONTRIB_LLNL],[
AC_ARG_WITH([dcgm],
  [AS_HELP_STRING([--with-dcgm],
    [Include plugin for Nvidia DCGM @<:@default=check@:>@])],
  [],
  [with_dcgm=check])
LIBDCGM=
AS_IF([test "x$with_dcgm" != xno],
      [AC_CHECK_LIB([dcgm], [dcgmInit],
         [have_dcgm=true],
         [have_dcgm=false
          if test "x$with_dcgm" != xcheck; then
            AC_MSG_FAILURE([--with-dcgm was given, but test for dcgm failed])
          fi
         ])],
      [have_dcgm=false])
AM_CONDITIONAL([HAVE_DCGM], [test x$have_dcgm = xtrue])
])
