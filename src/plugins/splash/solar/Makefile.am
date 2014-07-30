INCLUDES = -I$(top_srcdir)                                                    \
           -I$(srcdir)/../../../libply                                        \
           -I$(srcdir)/../../../libplybootsplash                              \
           -I$(srcdir)/../../..                                               \
           -I$(srcdir)/../..                                                  \
           -I$(srcdir)/..                                                     \
           -I$(srcdir)

plugindir = $(libdir)/plymouth
plugin_LTLIBRARIES = solar.la

solar_la_CFLAGS =    $(PLYMOUTH_CFLAGS)                                       \
                    -DPLYMOUTH_IMAGE_DIR=\"$(datadir)/plymouth/\"             \
                    -DPLYMOUTH_LOGO_FILE=\"$(logofile)\"                      \
                    -DPLYMOUTH_BACKGROUND_COLOR=$(background_color)           \
                    -DPLYMOUTH_BACKGROUND_END_COLOR=$(background_end_color)   \
                    -DPLYMOUTH_BACKGROUND_START_COLOR=$(background_start_color)
solar_la_LDFLAGS =   -module -avoid-version -export-dynamic
solar_la_LIBADD =    $(PLYMOUTH_LIBS)                                         \
                    ../../../libply/libply.la                                 \
                    ../../../libplybootsplash/libplybootsplash.la
solar_la_SOURCES = $(srcdir)/plugin.c

plymouthdir = $(datadir)/plymouth/solar
plymouth_DATA = bullet.png entry.png lock.png                                 \
                star.png box.png                                              \
                progress_bar.png
#                planet1.png planet2.png planet3.png planet4.png planet5.png

EXTRA_DIST = $(plymouth_DATA)
MAINTAINERCLEANFILES = Makefile.in
