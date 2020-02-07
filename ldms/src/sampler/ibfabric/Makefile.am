ACLOCAL_AMFLAGS = -I m4
pkglib_LTLIBRARIES =
bin_PROGRAMS =
CORE = ../../core
SAMPLER= ../../sampler

AM_CFLAGS = -I$(srcdir)/$(CORE) -I$(top_srcdir) @OVIS_LIB_INCDIR_FLAG@
AM_LDFLAGS = @OVIS_LIB_LIB64DIR_FLAG@ @OVIS_LIB_LIBDIR_FLAG@
COMMON_LIBADD = $(CORE)/libldms.la @LDFLAGS_GETTIME@ -lovis_util
dist_man7_MANS=

if ENABLE_JOBID
JOBID_LIBFLAGS = ../libjobid.la
endif

if ENABLE_IBFABRIC
libscibfabric_la_SOURCES = scibfabric.c
libscibfabric_la_LIBADD = $(COMMON_LIBADD) $(JOBID_LIBFLAGS)
libscibfabric_la_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/$(SAMPLER) -I/usr/include/infiniband
libscibfabric_la_LDFLAGS = $(AM_LDFLAGS) -libmad -libumad
pkglib_LTLIBRARIES += libscibfabric.la

libscibfabric2_la_SOURCES = scibfabric2.c
libscibfabric2_la_LIBADD = $(COMMON_LIBADD) $(JOBID_LIBFLAGS)
libscibfabric2_la_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/$(SAMPLER) -I/usr/include/infiniband
libscibfabric2_la_LDFLAGS = $(AM_LDFLAGS) -libmad -libumad
dist_man7_MANS += Plugin_scibfabric2.man
pkglib_LTLIBRARIES += libscibfabric2.la

libibfabric_la_SOURCES = ibfabric.c
libibfabric_la_LIBADD = $(COMMON_LIBADD) $(JOBID_LIBFLAGS)
libibfabric_la_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/$(SAMPLER) -I/usr/include/infiniband
pkglib_LTLIBRARIES += libibfabric.la

ldms_delegate_ports_SOURCES = delegate_ports.c
ldms_delegate_ports_CFLAGS = $(AM_CFLAGS) -I/usr/include/infiniband
ldms_delegate_ports_LDADD = $(COMMON_LIBADD)
ldms_delegate_ports_LDFLAGS = $(AM_LDFLAGS) -libmad -libumad -libnetdisc -pthread -losmcomp
bin_PROGRAMS += ldms_delegate_ports
endif
