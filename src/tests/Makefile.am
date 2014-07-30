INCLUDES =                                                                    \
           -I$(top_srcdir)                                                    \
           -I$(srcdir)/..                                                     \
           -I$(srcdir)/../libply                                              \
           -I$(srcdir)/../libplybootsplash                                    \
           -I$(srcdir)
TESTS =

if ENABLE_TESTS
include $(srcdir)/ply-boot-server-test.am
include $(srcdir)/ply-boot-splash-test.am
include $(srcdir)/ply-window-test.am
endif

noinst_PROGRAMS = $(TESTS)
MAINTAINERCLEANFILES = Makefile.in
