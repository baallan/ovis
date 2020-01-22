
check_PROGRAMS = testwatch
testwatch_SOURCES = test.c test_log.c test_progress.c ldms_watch.c
ldmscoreincludedir = $(includedir)/ldms
ldmscoreinclude_HEADERS = ldms_watch.h

DOCDEPS= \
	$(srcdir)/test.c \
	$(srcdir)/test_log.c \
       	$(srcdir)/test_progress.c \
       	$(srcdir)/ldms_watch.c \
       	$(srcdir)/ldms_watch.h

OPV=-$(PACKAGE_VERSION)-watch

doxygen-out: $(DOCDEPS)
	doxygen ldms_watch.doxy
	touch doxygen-out

install-doxygen: doxygen-out
	$(MKDIR_P) $(DESTDIR)$(docdir)$(OPV)/html/search
	for f in doxygen/html/* ; do \
		$(INSTALL_DATA) $$f $(DESTDIR)$(docdir)$(OPV)/html; \
	done
	if [ -d doxygen/html/search ]; then \
	for f in doxygen/html/search/*; do \
		$(INSTALL_DATA) $$f $(DESTDIR)$(docdir)$(OPV)/html/search; \
	done \
	fi

PHONY_dox = install-dox

if ENABLE_DOC_HTML
install-dox: install-doxygen
else
install-dox:
endif

.PHONY: $(PHONY_dox)

install-data-local: install-dox
