INCLUDES = -I$(top_srcdir)                                                    \
           -I$(srcdir)/../../../libply                                        \
           -I$(srcdir)/../../../libplybootsplash                              \
           -I$(srcdir)/../../..                                               \
           -I$(srcdir)/../..                                                  \
           -I$(srcdir)/..                                                     \
           -I$(srcdir)

plugindir = $(libdir)/plymouth
plugin_LTLIBRARIES = label.la

label_la_CFLAGS = $(PLYMOUTH_CFLAGS) $(PANGO_CFLAGS)

label_la_LDFLAGS = -module -avoid-version -export-dynamic
label_la_LIBADD = $(PLYMOUTH_LIBS) $(PANGO_LIBS)                           \
                  ../../../libply/libply.la                                \
                  ../../../libplybootsplash/libplybootsplash.la
label_la_SOURCES = $(srcdir)/plugin.c

MAINTAINERCLEANFILES = Makefile.in
