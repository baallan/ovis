ACLOCAL_AMFLAGS = -I m4
SUBDIRS =

# init script
#   init.d is currently unmaintained
# SUBDIRS += init.d

# logrotate
#   logrotate.d is currently unmaintained
# SUBDIRS += logrotate.d

# systemd config files
SUBDIRS += systemd

do_subst = @LDMS_SUBST_RULE@

EXTRA_DIST = ldmsauth.conf \
	     ldmsplugin.sampler.conf \
	     ldmsplugin.aggregator.conf \
	     ldmsd.aggregator.env.in \
	     ldmsd.sampler.env.in \
	     ldms.cfg

ENV_FILES = $(builddir)/ldmsd.sampler.env \
	    $(builddir)/ldmsd.aggregator.env

$(builddir)/%.env: $(srcdir)/%.env.in
	$(do_subst) < $< > $@

CONF_DIR = $(DESTDIR)$(sysconfdir)/ldms

# These are ldms-specific config
install-data-local: $(ENV_FILES)
	$(MKDIR_P) $(CONF_DIR)
	$(INSTALL_DATA) $(ENV_FILES) $(CONF_DIR)/
	$(INSTALL) -m 600 $(srcdir)/ldmsauth.conf $(CONF_DIR)
	$(INSTALL_DATA) $(srcdir)/ldms.cfg $(CONF_DIR)
	$(INSTALL_DATA) $(srcdir)/ldmsplugin.sampler.conf $(CONF_DIR)
	$(INSTALL_DATA) $(srcdir)/ldmsplugin.aggregator.conf $(CONF_DIR)

uninstall-local:
	rm -f $(CONF_DIR)/ldmsd.aggregator.env
	rm -f $(CONF_DIR)/ldmsd.sampler.env
	rm -f $(CONF_DIR)/ldmsauth.conf
	rm -f $(CONF_DIR)/ldmsplugin.sampler.conf
	rm -f $(CONF_DIR)/ldmsplugin.aggregator.conf
	rm -f $(CONF_DIR)/ldms.cfg
