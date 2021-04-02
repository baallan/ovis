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
COMMON_LIBADD = -lsampler_base -lldms -lovis_util -lcoll \
                @LDFLAGS_GETTIME@

# with proper rdc present:
#rdcinc = @ROCM_RDC_INC_INCDIR_FLAG@ 
#rdclib = -lrdc_bootstrap
#rdcldflags = @ROCM_RDC_INC_LIBDIR_FLAG@ @ROCM_SMI_INC_LIBDIR_FLAG@

# with just headers, no runtime
rdcinc = -I/scratch1/baallan/rdc/include
rdclib = 
rdcldflags = 

pkglib_LTLIBRARIES += librdc_sampler.la
librdc_sampler_la_SOURCES = rdc_plugin.c rdcinfo.c rdcinfo.h
librdc_sampler_la_CFLAGS  = $(SAMPLER_CFLAGS) $(rdcinc)
#librdc_sampler_la_LIBADD  = $(SAMPLER_LIBADD) -lrdc_bootstrap
librdc_sampler_la_LIBADD  = $(SAMPLER_LIBADD) $(COMMON_LIBADD)
#librdc_sampler_la_LDFLAGS = $(SAMPLER_LDFLAGS) @ROCM_RDC_INC_LIBDIR_FLAG@ @ROCM_SMI_INC_LIBDIR_FLAG@
librdc_sampler_la_LDFLAGS = $(SAMPLER_LDFLAGS) 

bin_PROGRAMS += ldms_rdc_schema_name
dist_man7_MANS += Plugin_rdc_sampler.man
dist_man1_MANS += ldms_rdc_schema_name.man

ldms_rdc_schema_name_SOURCES = rdcinfo.c
ldms_rdc_schema_name_LDADD = $(top_builddir)/ldms/src/ldmsd/libldmsd_plugattr.la \
                $(top_builddir)/ldms/src/core/libldms.la \
                @LDFLAGS_GETTIME@ \
                $(top_builddir)/lib/src/ovis_util/libovis_util.la \
                $(top_builddir)/lib/src/coll/libcoll.la
ldms_rdc_schema_name_LDFLAGS = $(AM_LDFLAGS) -lldms -lovis_util  -pthread
ldms_rdc_schema_name_CFLAGS = $(AM_CFLAGS) -DMAIN $(rdcinc)

