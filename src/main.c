/* main.c - boot messages monitor
 *
 * Copyright (C) 2007 Red Hat, Inc
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this; see the file COPYING.  If not, write to the Free
 * Software Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Written by: Ray Strode <rstrode@redhat.com>
 */
#include "config.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <limits.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <sysexits.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <linux/kd.h>

#include "ply-boot-server.h"
#include "ply-boot-splash.h"
#include "ply-event-loop.h"
#include "ply-list.h"
#include "ply-logger.h"
#include "ply-terminal-session.h"
#include "ply-trigger.h"
#include "ply-utils.h"
#include "ply-progress.h"

#ifndef PLY_MAX_COMMAND_LINE_SIZE
#define PLY_MAX_COMMAND_LINE_SIZE 512
#endif

typedef struct
{
  ply_event_loop_t *loop;
  ply_boot_server_t *boot_server;
  ply_list_t *windows;
  ply_boot_splash_t *boot_splash;
  ply_terminal_session_t *session;
  ply_buffer_t *boot_buffer;
  ply_progress_t *progress;
  long ptmx;

  char kernel_command_line[PLY_MAX_COMMAND_LINE_SIZE];
  uint32_t no_boot_log : 1;
  uint32_t showing_details : 1;
  uint32_t system_initialized : 1;
  uint32_t is_redirected : 1;
  uint32_t is_attached : 1;

  char *console;

  int number_of_errors;
} state_t;

static ply_boot_splash_t *start_boot_splash (state_t    *state,
                                             const char *module_path);

static ply_window_t *create_window (state_t    *state,
                                    const char *tty_name);

static bool attach_to_running_session (state_t *state);
static void on_escape_pressed (state_t *state);
static void dump_details_and_quit_splash (state_t *state);

static void
on_session_output (state_t    *state,
                   const char *output,
                   size_t      size)
{
  ply_buffer_append_bytes (state->boot_buffer, output, size);
  if (state->boot_splash != NULL)
    ply_boot_splash_update_output (state->boot_splash,
                                   output, size);
}

static void
on_session_finished (state_t *state)
{
  ply_trace ("got hang up on terminal session fd");
}

static void
on_update (state_t     *state,
           const char  *status)
{
  ply_trace ("updating status to '%s'", status);
  ply_progress_status_update (state->progress,
                               status);
  if (state->boot_splash != NULL)
    ply_boot_splash_update_status (state->boot_splash,
                                   status);
}

static void
show_detailed_splash (state_t *state)
{
  ply_trace ("Showing detailed splash screen");
  state->boot_splash = start_boot_splash (state,
                                          PLYMOUTH_PLUGIN_PATH "details.so");

  if (state->boot_splash == NULL)
    {
      ply_trace ("Could not start detailed splash screen, exiting");
      exit (1);
    }
}

static void
show_default_splash (state_t *state)
{
  ply_trace ("Showing splash screen");
  state->boot_splash = start_boot_splash (state,
                                          PLYMOUTH_PLUGIN_PATH "default.so");

  if (state->boot_splash == NULL)
    {
      ply_trace ("Could not start graphical splash screen,"
                 "showing text splash screen");
      state->boot_splash = start_boot_splash (state,
                                              PLYMOUTH_PLUGIN_PATH "text.so");
    }

  if (state->boot_splash == NULL)
    ply_error ("could not start boot splash: %m");
}

static void
on_ask_for_password (state_t      *state,
                     const char   *prompt,
                     ply_trigger_t *answer)
{
  if (state->boot_splash == NULL)
    {
      show_detailed_splash (state);
      if (state->boot_splash == NULL)
        ply_trigger_pull (answer, "");
      return;
    }

  ply_boot_splash_ask_for_password (state->boot_splash,
                                    prompt, answer);
}

static void
on_newroot (state_t    *state,
             const char *root_dir)
{
  ply_trace ("new root mounted at \"%s\", switching to it", root_dir);
  chdir(root_dir);
  chroot(".");
  chdir("/");
  ply_progress_load_cache (state->progress);
  if (state->boot_splash != NULL)
    ply_boot_splash_root_mounted (state->boot_splash);
}

static void
spool_error (state_t *state)
{
  ply_trace ("spooling error for viewer");

  unlink (PLYMOUTH_SPOOL_DIRECTORY "/boot.log");

  ply_create_file_link (PLYMOUTH_LOG_DIRECTORY "/boot.log",
                        PLYMOUTH_SPOOL_DIRECTORY "/boot.log");
}

static void
on_system_initialized (state_t *state)
{
  ply_trace ("system now initialized, opening boot.log");
  state->system_initialized = true;
  ply_terminal_session_open_log (state->session,
                                 PLYMOUTH_LOG_DIRECTORY "/boot.log");

  if (state->number_of_errors > 0)
    spool_error (state);
}

static void
on_error (state_t *state)
{
  ply_trace ("encountered error during boot up");

  if (state->system_initialized && state->number_of_errors == 0)
    spool_error (state);
  else
    ply_trace ("not spooling because number of errors %d", state->number_of_errors);

  state->number_of_errors++;
}

static bool
has_open_window (state_t *state)
{
  ply_list_node_t *node;

  ply_trace ("checking for open windows");

  node = ply_list_get_first_node (state->windows);
  while (node != NULL)
    {
      ply_list_node_t *next_node;
      ply_window_t *window;

      next_node = ply_list_get_next_node (state->windows, node);

      window = ply_list_node_get_data (node);

      if (ply_window_is_open (window))
        {
          int fd;
          const char *name;

          fd = ply_window_get_tty_fd (window);

          if (fd >= 0)
            name = ttyname (fd);
          else
            name = NULL;

          ply_trace ("window %s%sis open",
                     name != NULL? name : "",
                     name != NULL? " " : "");
          return true;
        }

      node = next_node;
    }

  return false;
}

static bool
plymouth_should_ignore_show_splash_calls (state_t *state)
{
  ply_trace ("checking if plymouth should be running");
  return ply_string_has_prefix (state->kernel_command_line, "init=") || strstr (state->kernel_command_line, " init=") != NULL;
}

static bool
plymouth_should_show_default_splash (state_t *state)
{
  ply_trace ("checking if plymouth should show default splash");

  const char const *strings[] = {
      " single ", " single", "^single ",
      " 1 ", " 1", "^1 ",
      NULL
  };
  int i;

  if (state->console != NULL)
    return false;

  if (!has_open_window (state))
    return false;

  for (i = 0; strings[i] != NULL; i++)
    {
      int cmp;
      if (strings[i][0] == '^')
          cmp = strncmp(state->kernel_command_line, strings[i]+1,
                        strlen(strings[i]+1)) == 0;
      else
          cmp = strstr (state->kernel_command_line, strings[i]) != NULL;

      if (cmp)
        {
          ply_trace ("kernel command line has option \"%s\"", strings[i]);
          return false;
        }
    }

  return strstr (state->kernel_command_line, "rhgb") != NULL;
}

static void
open_windows (state_t *state)
{
  ply_list_node_t *node;

  node = ply_list_get_first_node (state->windows);
  while (node != NULL)
    {
      ply_list_node_t *next_node;
      ply_window_t *window;

      next_node = ply_list_get_next_node (state->windows, node);

      window = ply_list_node_get_data (node);

      if (!ply_window_is_open (window))
        ply_window_open (window);

      node = next_node;
    }
}

static void
close_windows (state_t *state)
{
  ply_list_node_t *node;

  node = ply_list_get_first_node (state->windows);
  while (node != NULL)
    {
      ply_list_node_t *next_node;
      ply_window_t *window;

      next_node = ply_list_get_next_node (state->windows, node);

      window = ply_list_node_get_data (node);

      if (ply_window_is_open (window))
        ply_window_close (window);

      node = next_node;
    }
}

static void
on_show_splash (state_t *state)
{
  bool has_window;

  if (plymouth_should_ignore_show_splash_calls (state))
    {
      dump_details_and_quit_splash (state);
      return;
    }

  open_windows (state);

  has_window = has_open_window (state);

  if (!state->is_attached && state->ptmx >= 0 && has_window)
    state->is_attached = attach_to_running_session (state);

  if (!has_window && state->is_attached)
    {
      ply_trace ("no open windows, detaching session");
      ply_terminal_session_detach (state->session);
      state->is_redirected = false;
      state->is_attached = false;
    }

  if (plymouth_should_show_default_splash (state))
    show_default_splash (state);
  else
    show_detailed_splash (state);
}

static void
quit_splash (state_t *state)
{
  ply_trace ("quiting splash");
  if (state->boot_splash != NULL)
    {
      ply_trace ("freeing splash");
      ply_boot_splash_free (state->boot_splash);
      state->boot_splash = NULL;
    }

  ply_trace ("closing windows");
  close_windows (state);

  if (state->session != NULL)
    {
      ply_trace ("detaching session");
      ply_terminal_session_detach (state->session);
      state->is_redirected = false;
      state->is_attached = false;
    }
}

static void
dump_details_and_quit_splash (state_t *state)
{
  state->showing_details = false;
  on_escape_pressed (state);

  if (state->boot_splash != NULL)
    ply_boot_splash_hide (state->boot_splash);

  quit_splash (state);
}

static void
on_hide_splash (state_t *state)
{
  if (state->boot_splash == NULL)
    return;

  ply_trace ("hiding boot splash");
  dump_details_and_quit_splash (state);
}

#ifdef PLY_ENABLE_GDM_TRANSITION
static void
tell_gdm_to_transition (void)
{
  int fd;

  fd = creat ("/var/spool/gdm/force-display-on-active-vt", 0644);
  close (fd);
}
#endif

static void
on_quit (state_t *state,
         bool     retain_splash)
{
  ply_trace ("time to quit, closing boot.log");
  if (state->session != NULL)
    ply_terminal_session_close_log (state->session);
  ply_trace ("unloading splash");
  if (state->boot_splash != NULL)
    {
      if (!retain_splash)
        {
          if (state->boot_splash != NULL)
              ply_boot_splash_hide (state->boot_splash);
        }

      quit_splash (state);
    }
  ply_trace ("exiting event loop");
  ply_event_loop_exit (state->loop, 0);

#ifdef PLY_ENABLE_GDM_TRANSITION
  if (retain_splash)
    {
      tell_gdm_to_transition ();
    }
#endif
}

static ply_boot_server_t *
start_boot_server (state_t *state)
{
  ply_boot_server_t *server;

  server = ply_boot_server_new ((ply_boot_server_update_handler_t) on_update,
                                (ply_boot_server_ask_for_password_handler_t) on_ask_for_password,
                                (ply_boot_server_show_splash_handler_t) on_show_splash,
                                (ply_boot_server_hide_splash_handler_t) on_hide_splash,
                                (ply_boot_server_newroot_handler_t) on_newroot,
                                (ply_boot_server_system_initialized_handler_t) on_system_initialized,
                                (ply_boot_server_error_handler_t) on_error,
                                (ply_boot_server_quit_handler_t) on_quit,
                                state);

  if (!ply_boot_server_listen (server))
    {
      ply_save_errno ();
      ply_boot_server_free (server);
      ply_restore_errno ();
      return NULL;
    }

  ply_boot_server_attach_to_event_loop (server, state->loop);

  return server;
}

static void
on_escape_pressed (state_t *state)
{
  if (state->boot_splash != NULL)
    {
      ply_boot_splash_hide (state->boot_splash);
      ply_boot_splash_free (state->boot_splash);
      state->boot_splash = NULL;
    }

  if (!state->showing_details)
    {
      show_detailed_splash (state);
      state->showing_details = true;
    }
  else
    {
      show_default_splash (state);
      state->showing_details = false;
    }
}

static ply_window_t *
create_window (state_t    *state,
               const char *tty_name)
{
  ply_window_t *window;

  ply_trace ("creating window on %s", tty_name != NULL? tty_name : "active vt");
  window = ply_window_new (tty_name);

  ply_window_attach_to_event_loop (window, state->loop);

  return window;
}

static void
add_windows_to_boot_splash (state_t           *state,
                            ply_boot_splash_t *splash)
{
  ply_list_node_t *node;

  ply_trace ("There are %d windows in list",
             ply_list_get_length (state->windows));
  node = ply_list_get_first_node (state->windows);
  while (node != NULL)
    {
      ply_list_node_t *next_node;
      ply_window_t *window;

      next_node = ply_list_get_next_node (state->windows, node);

      window = ply_list_node_get_data (node);

      if (ply_window_is_open (window))
        {
          ply_trace ("adding window to boot splash");
          ply_boot_splash_add_window (splash, window);
          ply_trace ("listening for escape key");
          ply_window_set_escape_handler (window, (ply_window_escape_handler_t)
                                         on_escape_pressed, state);
        }
      node = next_node;
    }
}

static ply_boot_splash_t *
start_boot_splash (state_t    *state,
                   const char *module_path)
{
  ply_boot_splash_t *splash;

  ply_trace ("Loading boot splash plugin '%s'",
             module_path);

  splash = ply_boot_splash_new (module_path, state->boot_buffer);

  if (!ply_boot_splash_load (splash))
    {
      ply_save_errno ();
      ply_boot_splash_free (splash);
      ply_restore_errno ();
      return NULL;
    }

  ply_trace ("attaching plugin to event loop");
  ply_boot_splash_attach_to_event_loop (splash, state->loop);

  ply_trace ("attaching progress to plugin");
  ply_boot_splash_attach_progress (splash, state->progress);

  ply_trace ("adding windows to boot splash");
  add_windows_to_boot_splash (state, splash);

  ply_trace ("showing plugin");
  if (!ply_boot_splash_show (splash))
    {
      ply_save_errno ();
      ply_boot_splash_free (splash);
      ply_restore_errno ();
      return NULL;
    }

  return splash;
}

static bool
attach_to_running_session (state_t *state)
{
  ply_terminal_session_t *session;
  ply_terminal_session_flags_t flags;
  bool should_be_redirected;

  flags = 0;

  should_be_redirected = !state->no_boot_log;

  if (should_be_redirected)
    flags |= PLY_TERMINAL_SESSION_FLAGS_REDIRECT_CONSOLE;

 if (state->session == NULL)
   {
     ply_trace ("creating new terminal session");
     session = ply_terminal_session_new (NULL);

     ply_terminal_session_attach_to_event_loop (session, state->loop);
   }
 else
   {
     session = state->session;
     ply_trace ("session already created");
   }

  if (!ply_terminal_session_attach (session, flags,
                                 (ply_terminal_session_output_handler_t)
                                 on_session_output,
                                 (ply_terminal_session_done_handler_t)
                                 (should_be_redirected? on_session_finished: NULL),
                                 state->ptmx,
                                 state))
    {
      ply_save_errno ();
      ply_terminal_session_free (session);
      ply_buffer_free (state->boot_buffer);
      state->boot_buffer = NULL;
      ply_restore_errno ();

      state->is_redirected = false;
      state->is_attached = false;
      return false;
    }

  state->is_redirected = should_be_redirected;
  state->is_attached = true;
  state->session = session;

  return true;
}

static bool
get_kernel_command_line (state_t *state)
{
  int fd;

  ply_trace ("opening /proc/cmdline");
  fd = open ("proc/cmdline", O_RDONLY);

  if (fd < 0)
    {
      ply_trace ("couldn't open it: %m");
      return false;
    }

  ply_trace ("reading kernel command line");
  if (read (fd, state->kernel_command_line, sizeof (state->kernel_command_line)) < 0)
    {
      ply_trace ("couldn't read it: %m");
      return false;
    }

  ply_trace ("Kernel command line is: '%s'", state->kernel_command_line);
  return true;
}

static void
check_verbosity (state_t *state)
{
  ply_trace ("checking if tracing should be enabled");

  if ((strstr (state->kernel_command_line, " plymouth:debug ") != NULL)
     || (strstr (state->kernel_command_line, "plymouth:debug ") != NULL)
     || (strstr (state->kernel_command_line, " plymouth:debug") != NULL))
    {
#ifdef LOG_TO_DEBUG_FILE
      int fd;
#endif

      ply_trace ("tracing should be enabled!");
      if (!ply_is_tracing ())
        ply_toggle_tracing ();

#ifdef LOG_TO_DEBUG_FILE
      fd = open ("/dev/console", O_RDWR);
      ply_logger_set_output_fd (ply_logger_get_error_default (), fd);
#endif
    }
  else
    ply_trace ("tracing shouldn't be enabled!");
}

static void
check_logging (state_t *state)
{
  ply_trace ("checking if console messages should be redirected and logged");

  if ((strstr (state->kernel_command_line, " plymouth:nolog ") != NULL)
     || (strstr (state->kernel_command_line, "plymouth:nolog ") != NULL)
     || (strstr (state->kernel_command_line, " plymouth:nolog") != NULL))
    {
      ply_trace ("logging should be enabled!");
      state->no_boot_log = true;
    }
  else
    ply_trace ("logging shouldn't be enabled!");
}

static void
check_for_consoles (state_t *state)
{
  char *console_key;
  char *remaining_command_line;

  ply_trace ("checking if splash screen should be disabled");

  remaining_command_line = state->kernel_command_line;
  while ((console_key = strstr (remaining_command_line, " console=")) != NULL)
    {
      char *end;
      ply_trace ("serial console found!");

      free (state->console);
      state->console = strdup (console_key + strlen (" console="));

      remaining_command_line = console_key + strlen (" console=");

      end = strpbrk (state->console, " \n\t\v,");

      if (end != NULL)
        {
          *end = '\0';
          remaining_command_line += end - state->console;
        }

      if (strcmp (state->console, "tty0") == 0 || strcmp (state->console, "/dev/tty0") == 0)
        {
          free (state->console);
          state->console = strdup ("tty1");
        }

      ply_list_append_data (state->windows, create_window (state, state->console));
    }

    if (ply_list_get_length (state->windows) == 0)
      ply_list_append_data (state->windows, create_window (state, "tty1"));
}

static bool
redirect_standard_io_to_device (char *device)
{
  int fd;
  char *file;

  ply_trace ("redirecting stdio to %s", device);

  if (strncmp (device, "/dev/", strlen ("/dev/")) == 0)
    file = strdup (device);
  else
    asprintf (&file, "/dev/%s", device);

  fd = open (file, O_RDWR | O_APPEND);

  free (file);

  if (fd < 0)
    return false;

  dup2 (fd, STDIN_FILENO);
  dup2 (fd, STDOUT_FILENO);
  dup2 (fd, STDERR_FILENO);

  return true;
}

static bool
initialize_environment (state_t *state)
{
  ply_trace ("initializing minimal work environment");

  if (!get_kernel_command_line (state))
    return false;

  check_verbosity (state);
  check_logging (state);

  state->windows = ply_list_new ();
  check_for_consoles (state);

  if (state->console != NULL)
    redirect_standard_io_to_device (state->console);
  else
    redirect_standard_io_to_device ("tty1");

  ply_trace ("initialized minimal work environment");
  return true;
}

static void
on_crash (int signum)
{
    int fd;

    fd = open ("/dev/tty1", O_RDWR | O_NOCTTY);

    ioctl (fd, KDSETMODE, KD_TEXT);

    close (fd);

    signal (signum, SIG_DFL);
    raise(signum);
}

int
main (int    argc,
      char **argv)
{
  state_t state = {
      .ptmx = -1,
  };
  int exit_code;
  bool attach_to_session = false;
  ply_daemon_handle_t *daemon_handle;

  if (argc >= 2 && !strcmp(argv[1], "--attach-to-session"))
      attach_to_session = true;

  if (attach_to_session && argc == 3)
    {
      state.ptmx = strtol(argv[2], NULL, 0);
      if ((state.ptmx == LONG_MIN || state.ptmx == LONG_MAX) && errno != 0)
        {
          ply_error ("%s: could not parse ptmx string \"%s\": %m", argv[0], argv[2]);
          return EX_OSERR;
        }
    }

  if ((attach_to_session && argc != 3) || (attach_to_session && state.ptmx == -1))
    {
      ply_error ("%s [--attach-to-session <pty_master_fd>]", argv[0]);
      return EX_USAGE;
    }

  if (geteuid () != 0)
    {
      ply_error ("plymouthd must be run as root user");
      return EX_OSERR;
    }

  chdir ("/");
  signal (SIGPIPE, SIG_IGN);

  daemon_handle = ply_create_daemon ();

  if (daemon_handle == NULL)
    {
      ply_error ("cannot daemonize: %m");
      return EX_UNAVAILABLE;
    }

  signal (SIGABRT, on_crash);
  signal (SIGSEGV, on_crash);

  state.loop = ply_event_loop_new ();

  /* before do anything we need to make sure we have a working
   * environment.
   */
  if (!initialize_environment (&state))
    {
      if (errno == 0)
        {
          ply_detach_daemon (daemon_handle, 0);
          return 0;
        }

      ply_error ("could not setup basic operating environment: %m");
      ply_detach_daemon (daemon_handle, EX_OSERR);
      return EX_OSERR;
    }

  state.boot_buffer = ply_buffer_new ();

  if (attach_to_session)
    {
      if (!attach_to_running_session (&state))
        {
          ply_error ("could not create session: %m");
          ply_detach_daemon (daemon_handle, EX_UNAVAILABLE);
          return EX_UNAVAILABLE;
        }
    }

  state.boot_server = start_boot_server (&state);

  if (state.boot_server == NULL)
    {
      ply_error ("could not log bootup: %m");
      ply_detach_daemon (daemon_handle, EX_UNAVAILABLE);
      return EX_UNAVAILABLE;
    }

  if (!ply_detach_daemon (daemon_handle, 0))
    {
      ply_error ("could not tell parent to exit: %m");
      return EX_UNAVAILABLE;
    }

  state.progress = ply_progress_new ();
  ply_progress_load_cache (state.progress);
  ply_trace ("entering event loop");
  exit_code = ply_event_loop_run (state.loop);
  ply_trace ("exited event loop");

  ply_progress_save_cache (state.progress);
  
  ply_boot_splash_free (state.boot_splash);
  state.boot_splash = NULL;

  ply_list_free (state.windows);

  ply_boot_server_free (state.boot_server);
  state.boot_server = NULL;

  ply_trace ("freeing terminal session");
  ply_terminal_session_free (state.session);

  ply_buffer_free (state.boot_buffer);
  ply_progress_free (state.progress);

  ply_trace ("freeing event loop");
  ply_event_loop_free (state.loop);

  ply_trace ("exiting with code %d", exit_code);

  ply_free_error_log();

  return exit_code;
}
/* vim: set sts=4 ts=4 sw=4 expandtab autoindent cindent cino={.5s,(0: */
