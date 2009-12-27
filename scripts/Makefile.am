noinst_SCRIPTS = new-object.sh 

initrdscriptdir = $(libexecdir)/plymouth
initrdscript_SCRIPTS = plymouth-update-initrd plymouth-populate-initrd

sbinscriptsdir = $(sbindir)
sbinscripts_SCRIPTS = plymouth-set-default-theme

plymouth-populate-initrd: $(srcdir)/plymouth-populate-initrd.in
	sed -e 's,[@]logofile[@],$(logofile),g'                                \
	    -e 's,[@]plymouthclientdir[@],$(plymouthclientdir),g'              \
	    -e 's,[@]plymouthdaemondir[@],$(plymouthdaemondir),g'              \
	    -e 's,[@]RELEASE_FILE[@],$(RELEASE_FILE),g'              \
	    $(srcdir)/plymouth-populate-initrd.in > plymouth-populate-initrd

plymouth-set-default-theme: $(srcdir)/plymouth-set-default-theme.in
	sed -e 's,[@]logofile[@],$(logofile),g'                                \
	    -e 's,[@]plymouthclientdir[@],$(plymouthclientdir),g'              \
	    -e 's,[@]plymouthdaemondir[@],$(plymouthdaemondir),g'              \
	    $(srcdir)/plymouth-set-default-theme.in > plymouth-set-default-theme

EXTRA_DIST= plymouth-update-initrd                                             \
	    plymouth-populate-initrd.in                                        \
	    plymouth-set-default-theme.in                                     \
	    $(noinst_SCRIPTS)                                                  \
	    default.cfg

CLEANFILES = plymouth-populate-initrd plymouth-set-default-theme
MAINTAINERCLEANFILES = Makefile.in plymouth-populate-initrd plymouth-set-default-theme
