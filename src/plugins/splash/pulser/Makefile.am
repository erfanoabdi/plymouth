INCLUDES = -I$(top_srcdir)                                                    \
           -I$(srcdir)/../../../libply                                        \
           -I$(srcdir)/../../../libplybootsplash                              \
           -I$(srcdir)/../../..                                               \
           -I$(srcdir)/../..                                                  \
           -I$(srcdir)/..                                                     \
           -I$(srcdir)

plugindir = $(libdir)/plymouth
plugin_LTLIBRARIES = pulser.la

pulser_la_CFLAGS = $(PLYMOUTH_CFLAGS)                                           \
                 -DPLYMOUTH_BACKGROUND_COLOR=$(background_color)              \
                 -DPLYMOUTH_BACKGROUND_END_COLOR=$(background_end_color)      \
                 -DPLYMOUTH_BACKGROUND_START_COLOR=$(background_start_color)

pulser_la_LDFLAGS = -module -avoid-version -export-dynamic
pulser_la_LIBADD = $(PLYMOUTH_LIBS)                                            \
                 ../../../libply/libply.la                                   \
                 ../../../libplybootsplash/libplybootsplash.la
pulser_la_SOURCES = $(srcdir)/plugin.c

MAINTAINERCLEANFILES = Makefile.in
