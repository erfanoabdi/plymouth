INCLUDES =                                                                    \
           -I$(top_srcdir)                                                    \
           -I$(srcdir)/..                                                     \
           -I$(srcdir)
TESTS = 

if ENABLE_TESTS
include $(srcdir)/ply-frame-buffer-test.am
include $(srcdir)/ply-terminal-test.am
include $(srcdir)/ply-terminal-session-test.am
include $(srcdir)/ply-logger-test.am
include $(srcdir)/ply-array-test.am
include $(srcdir)/ply-bitarray-test.am
include $(srcdir)/ply-list-test.am
include $(srcdir)/ply-hashtable-test.am
include $(srcdir)/ply-event-loop-test.am
include $(srcdir)/ply-command-parser-test.am
include $(srcdir)/ply-progress-test.am
endif

noinst_PROGRAMS = $(TESTS)

MAINTAINERCLEANFILES = Makefile.in
