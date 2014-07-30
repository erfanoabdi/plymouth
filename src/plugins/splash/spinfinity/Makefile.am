INCLUDES = -I$(top_srcdir)                                                    \
           -I$(srcdir)/../../../libply                                        \
           -I$(srcdir)/../../../libplybootsplash                              \
           -I$(srcdir)/../../..                                               \
           -I$(srcdir)/../..                                                  \
           -I$(srcdir)/..                                                     \
           -I$(srcdir)

plugindir = $(libdir)/plymouth
plugin_LTLIBRARIES = spinfinity.la

spinfinity_la_CFLAGS = $(PLYMOUTH_CFLAGS)                                    \
                       -DPLYMOUTH_IMAGE_DIR=\"$(datadir)/plymouth/\"         \
                       -DPLYMOUTH_TIME_DIR=\"$(localstatedir)/lib/plymouth/\"\
                       -DPLYMOUTH_LOGO_FILE=\"$(logofile)\"                  \
                       -DPLYMOUTH_BACKGROUND_COLOR=$(background_color)       \
                       -DPLYMOUTH_BACKGROUND_END_COLOR=$(background_end_color)   \
                       -DPLYMOUTH_BACKGROUND_START_COLOR=$(background_start_color)

spinfinity_la_LDFLAGS = -module -avoid-version -export-dynamic
spinfinity_la_LIBADD = $(PLYMOUTH_LIBS)                                      \
                       ../../../libply/libply.la                             \
                       ../../../libplybootsplash/libplybootsplash.la
spinfinity_la_SOURCES = $(srcdir)/plugin.c

throbber_frames =                                                            \
                    throbber-00.png                                          \
                    throbber-01.png                                          \
                    throbber-02.png                                          \
                    throbber-03.png                                          \
                    throbber-04.png                                          \
                    throbber-05.png                                          \
                    throbber-06.png                                          \
                    throbber-07.png                                          \
                    throbber-08.png                                          \
                    throbber-09.png                                          \
                    throbber-10.png                                          \
                    throbber-11.png                                          \
                    throbber-12.png                                          \
                    throbber-13.png                                          \
                    throbber-14.png                                          \
                    throbber-15.png                                          \
                    throbber-16.png                                          \
                    throbber-17.png                                          \
                    throbber-18.png                                          \
                    throbber-19.png                                          \
                    throbber-20.png                                          \
                    throbber-21.png                                          \
                    throbber-22.png                                          \
                    throbber-23.png                                          \
                    throbber-24.png                                          \
                    throbber-25.png                                          \
                    throbber-26.png                                          \
                    throbber-27.png                                          \
                    throbber-28.png                                          \
                    throbber-29.png                                          \
                    throbber-30.png                                          \
                    throbber-31.png                                          \
                    throbber-32.png                                          \
                    throbber-33.png

plymouthdir = $(datadir)/plymouth/spinfinity
plymouth_DATA = bullet.png entry.png lock.png box.png $(throbber_frames)

EXTRA_DIST = $(plymouth_DATA)
MAINTAINERCLEANFILES = Makefile.in
