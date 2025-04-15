pkglib_LTLIBRARIES =
lib_LTLIBRARIES =
dist_man7_MANS =
dist_man1_MANS =

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = -lsampler_base -lldms -lovis_util -lcoll \
		@LDFLAGS_GETTIME@

if ENABLE_GPFS
libgpfs_sampler_la_SOURCES = gpfs.c gpfs_parse.c
libgpfs_sampler_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libgpfs_sampler.la
# we need a .rst file for the sampler man page
endif
