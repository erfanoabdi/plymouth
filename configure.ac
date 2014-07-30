AC_INIT(plymouth, 0.6.0, "halfline@gmail.com")
AC_CONFIG_SRCDIR(src/main.c)
AC_CONFIG_HEADER(config.h)
AC_CONFIG_AUX_DIR(build-tools)

AC_GNU_SOURCE
AC_PROG_AWK
AC_PROG_CC
AM_PROG_CC_C_O
AC_HEADER_STDC
AC_C_CONST

AM_INIT_AUTOMAKE([dist-bzip2 no-dist-gzip])
AM_MAINTAINER_MODE

PKG_PROG_PKG_CONFIG
AM_PROG_LIBTOOL

## increment if the interface has additions, changes, removals.
LT_CURRENT=2

## increment any time the source changes; set to
##  0 if you increment CURRENT
LT_REVISION=0

## increment if any interfaces have been added; set to 0
## if any interfaces have been changed or removed. removal has
## precedence over adding, so set to 0 if both happened.
LT_AGE=0

AC_SUBST(LT_CURRENT)
AC_SUBST(LT_REVISION)
AC_SUBST(LT_AGE)

PKG_CHECK_MODULES(IMAGE, [libpng >= 1.2.16 ]) 
AC_SUBST(IMAGE_CFLAGS)
AC_SUBST(IMAGE_LIBS)

PLYMOUTH_CFLAGS=""
PLYMOUTH_LIBS="-lm -lrt -ldl"

AC_SUBST(PLYMOUTH_CFLAGS)
AC_SUBST(PLYMOUTH_LIBS)

AC_ARG_ENABLE(pango, AS_HELP_STRING([--enable-pango],[enable building with pango, disabled there is no encryption prompts]),enable_pango=$enableval,enable_pango=yes)
AM_CONDITIONAL(ENABLE_PANGO,  [test "$enable_pango" = yes])

if test x$enable_pango = xyes; then
  PKG_CHECK_MODULES(PANGO, [pangocairo >= 1.21.0 ])
  AC_SUBST(PANGO_CFLAGS)
  AC_SUBST(PANGO_LIBS)
fi

PKG_CHECK_MODULES(GTK, [gtk+-2.0 >= 2.12.0 ])
AC_SUBST(GTK_CFLAGS)
AC_SUBST(GTK_LIBS)

AC_ARG_ENABLE(tracing, AS_HELP_STRING([--enable-tracing],[enable verbose tracing code]),enable_tracing=$enableval,enable_tracing=yes)

if test x$enable_tracing = xyes; then
  AC_DEFINE(PLY_ENABLE_TRACING, 1, [Build in verbose debug tracing spew])
fi

AC_ARG_ENABLE(tests, AS_HELP_STRING([--enable-tests],[build tests]),enable_tests=$enableval,enable_tests=yes)

AM_CONDITIONAL(ENABLE_TESTS,  [test "$enable_tests" = yes])

AC_ARG_ENABLE(gdm-transition, AS_HELP_STRING([--enable-gdm-transition],[enable smooth transition to gdm]),enable_gdm_transition=$enableval,enable_gdm_transition=no)

if test x$enable_gdm_transition = xyes; then
  AC_DEFINE(PLY_ENABLE_GDM_TRANSITION, 1, [Enable smooth transition to GDM])
fi

AC_ARG_WITH(system-root-install, AC_HELP_STRING([--with-system-root-install], [Install client in /bin and daemon in /sbin]),with_system_root_install=${withval},with_system_root_install=yes)
AM_CONDITIONAL(WITH_SYSTEM_ROOT_INSTALL,  [test "$with_system_root_install" = yes])

if test x$with_system_root_install = xyes; then
  plymouthclientdir=/bin
  plymouthdaemondir=/sbin
else
  plymouthclientdir=$bindir
  plymouthdaemondir=$sbindir
fi
AC_SUBST(plymouthclientdir)
AC_SUBST(plymouthdaemondir)

AC_ARG_WITH(rhgb-compat-link, AC_HELP_STRING([--with-rhgb-compat-link], [Install /usr/bin/rhgb-client compatability symlink]),with_rhgb_compat_link=${withval},with_rhgb_compat_link=yes)
AM_CONDITIONAL(WITH_RHGB_COMPAT_LINK,  [test "$with_rhgb_compat_link" = yes])

AC_ARG_WITH(gdm-autostart-file, AS_HELP_STRING([--with-gdm-autostart-file],[start log viewer from gdm on boot errors]),with_gdm_autostart_file=$withval,with_gdm_autostart_file=yes)

AM_CONDITIONAL(START_FROM_GDM,  [test "$with_gdm_autostart_file" = yes])

AC_ARG_WITH(logo, AC_HELP_STRING([--with-logo], [logo used by boot splash plugins]),logofile=${withval},logofile=$datadir/plymouth/bizcom.png)
AC_SUBST(logofile)

AM_CONDITIONAL(INSTALL_FALLBACK_LOGO,  [test "$logofile" = $datadir/plymouth/bizcom.png])

AC_ARG_WITH(background-color, AC_HELP_STRING([--with-background-color], [background color used by boot splash plugins]),background_color=${withval},background_color=0x5d5950)
AC_SUBST(background_color)

AC_ARG_WITH(background-start-color-stop, AC_HELP_STRING([--with-background-start-color], [first color stop in background gradients used by boot splash plugins]),background_start_color=${withval},background_start_color=0x807c71)
AC_SUBST(background_start_color)

AC_ARG_WITH(background-end-color-stop, AC_HELP_STRING([--with-background-end-color], [first color end in background gradients used by boot splash plugins]),background_end_color=${withval},background_end_color=0x3a362f)
AC_SUBST(background_end_color)

AC_ARG_WITH(default-plugin, AC_HELP_STRING([--with-default-plugin=fade-in], [Plugin to use by default]),default_plugin_name=${withval},default_plugin_name=fade-in)
AM_CONDITIONAL(ADD_DEFAULT_PLUGIN_LINK,
               [test "$default_plugin_name" = "spinfinity" \
                  -o "$default_plugin_name" = "fade-in"    \
                  -o "$default_plugin_name" = "pulser"     \
                  -o "$default_plugin_name" = "text"       \
                  -o "$default_plugin_name" = "details"])
AC_SUBST(default_plugin_name)

AC_OUTPUT([Makefile
           src/libply/Makefile
           src/libplybootsplash/Makefile
           src/plymouth-1.pc
           src/plugins/Makefile
           src/plugins/splash/Makefile
           src/plugins/splash/spinfinity/Makefile
           src/plugins/splash/fade-in/Makefile
           src/plugins/splash/text/Makefile
           src/plugins/splash/pulser/Makefile
           src/plugins/splash/details/Makefile
           src/plugins/splash/solar/Makefile
           src/plugins/controls/Makefile
           src/plugins/controls/label/Makefile
           src/Makefile
           src/client/Makefile
           src/viewer/Makefile
           src/tests/Makefile
           src/libply/tests/Makefile
           src/client/tests/Makefile
           images/Makefile
           scripts/Makefile
])
