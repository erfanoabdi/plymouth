SUBDIRS = libply libplybootsplash . plugins client viewer tests
INCLUDES = -I$(top_srcdir)                                                    \
           -I$(srcdir)/libply                                                 \
           -I$(srcdir)/libplybootsplash                                       \
           -I$(srcdir)                                                        \
           -DPLYMOUTH_LOG_DIRECTORY=\"$(localstatedir)/log\"                  \
           -DPLYMOUTH_SPOOL_DIRECTORY=\"$(localstatedir)/spool/plymouth\"     \
           -DPLYMOUTH_LOGO_FILE=\"$(logofile)\"

plymouthdbindir = $(plymouthdaemondir)
plymouthdbin_PROGRAMS = plymouthd

plymouthd_CFLAGS = $(PLYMOUTH_CFLAGS) -DPLYMOUTH_PLUGIN_PATH=\"$(libdir)/plymouth/\"
plymouthd_LDADD = $(PLYMOUTH_LIBS) libply/libply.la libplybootsplash/libplybootsplash.la
plymouthd_SOURCES =                                                            \
                   ply-boot-protocol.h                                        \
                   ply-boot-server.h                                          \
                   ply-boot-server.c                                          \
                   ply-boot-splash.h                                          \
                   ply-boot-splash.c                                          \
                   main.c

plymouthdrundir = $(localstatedir)/run/plymouth
plymouthdspooldir = $(localstatedir)/spool/plymouth
plymouthdtimedir = $(localstatedir)/lib/plymouth

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = plymouth-1.pc

install-data-hook:
	-mkdir -p $(DESTDIR)$(plymouthdrundir)
	-mkdir -p $(DESTDIR)$(plymouthdspooldir)
	-mkdir -p $(DESTDIR)$(plymouthdtimedir)

EXTRA_DIST = plymouth-1.pc.in
MAINTAINERCLEANFILES = Makefile.in
