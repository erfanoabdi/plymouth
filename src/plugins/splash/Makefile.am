SUBDIRS = throbgress fade-throbber text details space-flares two-step script
MAINTAINERCLEANFILES = Makefile.in

if ADD_DEFAULT_PLUGIN_LINK
install-data-hook:
	(cd $(DESTDIR)$(libdir)/plymouth; ln -sf $(default_plugin_name).so default.so)
endif
