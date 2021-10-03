AM_CPPFLAGS = -I$(top_srcdir)                                                 \
           -I$(srcdir)/../../../libply                                        \
           -I$(srcdir)/../../../libply-splash-core                            \
           -I$(srcdir)/../../..                                               \
           -I$(srcdir)/../..                                                  \
           -I$(srcdir)/..                                                     \
           -I$(srcdir)                                                        \
           -I$(srcdir)/core

plugindir = $(libdir)/plymouth/renderers
plugin_LTLIBRARIES = minui.la

minui_la_CFLAGS = $(PLYMOUTH_CFLAGS) $(DRM_CFLAGS) $(IMAGE_CFLAGS)

minui_la_LDFLAGS = -module -avoid-version -export-dynamic
minui_la_LIBADD = $(PLYMOUTH_LIBS) $(DRM_LIBS) $(IMAGE_LIBS)           \
                  ../../../libply/libply.la                            \
                  ../../../libply-splash-core/libply-splash-core.la
minui_la_SOURCES = $(srcdir)/plugin.c                                  \
                   $(srcdir)/core/graphics.c                           \
                   $(srcdir)/core/graphics_fbdev.c                     \
                   $(srcdir)/core/events.c                             \
                   $(srcdir)/core/resources.c                          \
                   $(srcdir)/core/graphics_drm.c

MAINTAINERCLEANFILES = Makefile.in
