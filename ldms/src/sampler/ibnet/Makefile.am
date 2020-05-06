ACLOCAL_AMFLAGS = -I m4
pkglib_LTLIBRARIES =
lib_LTLIBRARIES =
bin_SCRIPTS=
bin_PROGRAMS=
dist_man7_MANS =
dist_man1_MANS =
EXTRA_DIST = 

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = $(top_builddir)/ldms/src/sampler/libsampler_base.la \
		$(top_builddir)/ldms/src/ldmsd/libldmsd_plugattr.la \
		$(top_builddir)/ldms/src/core/libldms.la \
		@LDFLAGS_GETTIME@ \
		$(top_builddir)/lib/src/ovis_util/libovis_util.la \
		$(top_builddir)/lib/src/coll/libcoll.la


if ENABLE_IBNET
#libscibfabric_la_SOURCES = scibfabric.c
#libscibfabric_la_LIBADD = $(COMMON_LIBADD) $(JOBID_LIBFLAGS)
#libscibfabric_la_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/$(SAMPLER) -I/usr/include/infiniband
#libscibfabric_la_LDFLAGS = $(AM_LDFLAGS) -libmad -libumad
#pkglib_LTLIBRARIES += libscibfabric.la

libscibfabric2_la_SOURCES = scibfabric2.c
libscibfabric2_la_LIBADD = $(COMMON_LIBADD)
libscibfabric2_la_CFLAGS = $(AM_CFLAGS) -I/usr/include/infiniband
libscibfabric2_la_LDFLAGS = $(AM_LDFLAGS) -libmad -libumad -losmcomp -pthread
dist_man7_MANS += Plugin_scibfabric2.man
pkglib_LTLIBRARIES += libscibfabric2.la

libibnet_la_SOURCES = ibnet_plugin.c ibnet_data.c ibnet_data.h
libibnet_la_LIBADD = $(COMMON_LIBADD)
libibnet_la_LDFLAGS = $(AM_LDFLAGS) -libmad -libumad -losmcomp -pthread
libibnet_la_CFLAGS = $(AM_CFLAGS) -I/usr/include/infiniband
pkglib_LTLIBRARIES += libibnet.la

#libibfabric_la_SOURCES = ibfabric.c
#libibfabric_la_LIBADD = $(COMMON_LIBADD)
#libibfabric_la_CFLAGS = $(AM_CFLAGS) -I/usr/include/infiniband
#pkglib_LTLIBRARIES += libibfabric.la

# ldms_delegate_ports_SOURCES = delegate_ports.c
# ldms_delegate_ports_CFLAGS = $(AM_CFLAGS) -I/usr/include/infiniband
# ldms_delegate_ports_LDADD = $(COMMON_LIBADD)
# ldms_delegate_ports_LDFLAGS = $(AM_LDFLAGS) -libmad -libumad -libnetdisc -pthread -losmcomp
# bin_PROGRAMS += ldms_delegate_ports
endif
