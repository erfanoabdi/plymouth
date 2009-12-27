INCLUDES = -I$(top_srcdir)                                                    \
           -I$(srcdir)                                                        \
           -I$(srcdir)/../libply                                              \
           -I$(srcdir)/../plugins/controls

lib_LTLIBRARIES = libplybootsplash.la

libplybootsplashdir = $(includedir)/plymouth-1/plybootsplash
libplybootsplash_HEADERS = ply-entry.h ply-image.h ply-progress-bar.h ply-text-progress-bar.h ply-text-pulser.h ply-throbber.h ply-animation.h ply-progress-animation.h ply-window.h ply-label.h ply-boot-splash-plugin.h ply-label-plugin.h

libplybootsplash_la_CFLAGS = $(PLYMOUTH_CFLAGS)                               \
			     $(IMAGE_CFLAGS)                                  \
                             -DPLYMOUTH_BACKGROUND_COLOR=$(background_color)   \
                       -DPLYMOUTH_BACKGROUND_END_COLOR=$(background_end_color) \
                       -DPLYMOUTH_BACKGROUND_START_COLOR=$(background_start_color) \
                       -DPLYMOUTH_PLUGIN_PATH=\"$(PLYMOUTH_PLUGIN_PATH)\"
libplybootsplash_la_LIBADD = $(PLYMOUTH_LIBS) $(IMAGE_LIBS) ../libply/libply.la
libplybootsplash_la_LDFLAGS = -export-symbols-regex '^[^_].*' \
		    -version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE) \
		    -no-undefined
libplybootsplash_la_SOURCES = \
		    $(libplybootsplash_HEADERS)                              \
		    ply-entry.c                                              \
		    ply-image.c                                              \
		    ply-label.c                                              \
		    ply-progress-bar.c                                       \
		    ply-throbber.c                                           \
		    ply-animation.c                                          \
		    ply-progress-animation.c                                 \
		    ply-text-progress-bar.c                                  \
		    ply-text-pulser.c                                        \
		    ply-window.c

MAINTAINERCLEANFILES = Makefile.in
