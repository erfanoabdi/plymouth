/* ply-boot-splash.h - APIs for putting up a splash screen
 *
 * Copyright (C) 2007 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Written by: Ray Strode <rstrode@redhat.com>
 *             Soeren Sandmann <sandmann@redhat.com>
 */
#include "config.h"
#include "ply-boot-splash.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <wchar.h>

#include "ply-boot-splash-plugin.h"
#include "ply-event-loop.h"
#include "ply-list.h"
#include "ply-logger.h"
#include "ply-trigger.h"
#include "ply-utils.h"
#include "ply-progress.h"
#include "ply-key-file.h"

#ifndef UPDATES_PER_SECOND
#define UPDATES_PER_SECOND 30
#endif


struct _ply_boot_splash
{
  ply_event_loop_t *loop;
  ply_module_handle_t *module_handle;
  const ply_boot_splash_plugin_interface_t *plugin_interface;
  ply_boot_splash_plugin_t *plugin;
  ply_buffer_t *boot_buffer;
  ply_trigger_t *idle_trigger;

  char *theme_path;
  char *plugin_dir;
  char *status;

  ply_progress_t *progress;
  ply_boot_splash_on_idle_handler_t idle_handler;
  void *idle_handler_user_data;

  uint32_t is_loaded : 1;
  uint32_t is_shown : 1;
};

typedef const ply_boot_splash_plugin_interface_t *
        (* get_plugin_interface_function_t) (void);

static void ply_boot_splash_update_progress (ply_boot_splash_t *splash);
static void ply_boot_splash_detach_from_event_loop (ply_boot_splash_t *splash);

ply_boot_splash_t *
ply_boot_splash_new (const char   *theme_path,
                     const char   *plugin_dir,
                     ply_buffer_t *boot_buffer)
{
  ply_boot_splash_t *splash;

  assert (theme_path != NULL);

  splash = calloc (1, sizeof (ply_boot_splash_t));
  splash->loop = NULL;
  splash->theme_path = strdup (theme_path);
  splash->plugin_dir = strdup (plugin_dir);
  splash->module_handle = NULL;
  splash->is_shown = false;

  splash->boot_buffer = boot_buffer;

  return splash;
}

void
ply_boot_splash_add_window (ply_boot_splash_t *splash,
                            ply_window_t      *window)
{
  splash->plugin_interface->add_window (splash->plugin, window);
}

void
ply_boot_splash_remove_window (ply_boot_splash_t *splash,
                               ply_window_t      *window)
{
  splash->plugin_interface->remove_window (splash->plugin, window);
}

bool
ply_boot_splash_load (ply_boot_splash_t *splash)
{
  ply_key_file_t *key_file;
  char *module_name;
  char *module_path;

  assert (splash != NULL);

  get_plugin_interface_function_t get_boot_splash_plugin_interface;

  key_file = ply_key_file_new (splash->theme_path);

  if (!ply_key_file_load (key_file))
    return false;

  module_name = ply_key_file_get_value (key_file, "Plymouth Theme", "ModuleName");

  asprintf (&module_path, "%s%s.so",
            splash->plugin_dir, module_name);
  free (module_name);

  splash->module_handle = ply_open_module (module_path);

  free (module_path);

  if (splash->module_handle == NULL)
    {
      ply_key_file_free (key_file);
      return false;
    }

  get_boot_splash_plugin_interface = (get_plugin_interface_function_t)
      ply_module_look_up_function (splash->module_handle,
                                   "ply_boot_splash_plugin_get_interface");

  if (get_boot_splash_plugin_interface == NULL)
    {
      ply_save_errno ();
      ply_close_module (splash->module_handle);
      splash->module_handle = NULL;
      ply_key_file_free (key_file);
      ply_restore_errno ();
      return false;
    }

  splash->plugin_interface = get_boot_splash_plugin_interface ();

  if (splash->plugin_interface == NULL)
    {
      ply_save_errno ();
      ply_close_module (splash->module_handle);
      splash->module_handle = NULL;
      ply_key_file_free (key_file);
      ply_restore_errno ();
      return false;
    }

  splash->plugin = splash->plugin_interface->create_plugin (key_file);

  ply_key_file_free (key_file);

  assert (splash->plugin != NULL);

  splash->is_loaded = true;

  return true;
}

void
ply_boot_splash_unload (ply_boot_splash_t *splash)
{
  assert (splash != NULL);
  assert (splash->plugin != NULL);
  assert (splash->plugin_interface != NULL);
  assert (splash->module_handle != NULL);

  splash->plugin_interface->destroy_plugin (splash->plugin);
  splash->plugin = NULL;

  ply_close_module (splash->module_handle);
  splash->plugin_interface = NULL;
  splash->module_handle = NULL;

  splash->is_loaded = false;
}

void
ply_boot_splash_free (ply_boot_splash_t *splash)
{
  ply_trace ("freeing splash");
  if (splash == NULL)
    return;

  if (splash->loop != NULL)
    {
      if (splash->plugin_interface->on_boot_progress != NULL)
        {
          ply_event_loop_stop_watching_for_timeout (splash->loop,
                                                    (ply_event_loop_timeout_handler_t)
                                                    ply_boot_splash_update_progress, splash);
        }

      ply_event_loop_stop_watching_for_exit (splash->loop, (ply_event_loop_exit_handler_t)
                                             ply_boot_splash_detach_from_event_loop,
                                             splash);
    }

  if (splash->module_handle != NULL)
    ply_boot_splash_unload (splash);

  if (splash->idle_trigger != NULL)
    ply_trigger_free (splash->idle_trigger);

  free (splash->theme_path);
  free (splash->plugin_dir);
  free (splash);
}

static void
ply_boot_splash_update_progress (ply_boot_splash_t *splash)
{
  double percentage=0.0;
  double time=0.0;

  assert (splash != NULL);

  if (splash->progress)
    {
      percentage = ply_progress_get_percentage(splash->progress);
      time = ply_progress_get_time(splash->progress);
    }

  if (splash->plugin_interface->on_boot_progress != NULL)
    splash->plugin_interface->on_boot_progress (splash->plugin,
                                                time,
                                                percentage);

  ply_event_loop_watch_for_timeout (splash->loop,
                                   1.0 / UPDATES_PER_SECOND,
                                   (ply_event_loop_timeout_handler_t)
                                   ply_boot_splash_update_progress, splash);
}

void
ply_boot_splash_attach_progress (ply_boot_splash_t *splash,
                                      ply_progress_t    *progress)
{
  assert (splash != NULL);
  assert (progress != NULL);
  assert (splash->progress == NULL);
  splash->progress = progress;
}


bool
ply_boot_splash_show (ply_boot_splash_t *splash,
                      ply_boot_splash_mode_t mode)
{
  assert (splash != NULL);
  assert (splash->module_handle != NULL);
  assert (splash->loop != NULL);

  if (splash->is_shown)
    return true;

  assert (splash->plugin_interface != NULL);
  assert (splash->plugin != NULL);
  assert (splash->plugin_interface->show_splash_screen != NULL);

  ply_trace ("showing splash screen\n");
  if (!splash->plugin_interface->show_splash_screen (splash->plugin,
                                                     splash->loop,
                                                     splash->boot_buffer,
                                                     mode))
    {

      ply_save_errno ();
      ply_trace ("can't show splash: %m");
      ply_restore_errno ();
      return false;
    }

  if (splash->plugin_interface->on_boot_progress != NULL)
    {
      ply_boot_splash_update_progress (splash);
    }

  splash->is_shown = true;
  return true;
}

void
ply_boot_splash_update_status (ply_boot_splash_t *splash,
                               const char        *status)
{
  assert (splash != NULL);
  assert (status != NULL);
  assert (splash->plugin_interface != NULL);
  assert (splash->plugin != NULL);
  assert (splash->plugin_interface->update_status != NULL);
  assert (splash->is_shown);

  splash->plugin_interface->update_status (splash->plugin, status);
}

void
ply_boot_splash_update_output (ply_boot_splash_t *splash,
                               const char        *output,
                               size_t             size)
{
  assert (splash != NULL);
  assert (output != NULL);

  if (splash->plugin_interface->on_boot_output != NULL)
    splash->plugin_interface->on_boot_output (splash->plugin, output, size);
}

void
ply_boot_splash_root_mounted (ply_boot_splash_t *splash)
{
  assert (splash != NULL);

  if (splash->plugin_interface->on_root_mounted != NULL)
    splash->plugin_interface->on_root_mounted (splash->plugin);
}

static void
ply_boot_splash_detach_from_event_loop (ply_boot_splash_t *splash)
{
  assert (splash != NULL);
  splash->loop = NULL;
}

void
ply_boot_splash_hide (ply_boot_splash_t *splash)
{
  assert (splash != NULL);
  assert (splash->plugin_interface != NULL);
  assert (splash->plugin != NULL);
  assert (splash->plugin_interface->hide_splash_screen != NULL);

  splash->plugin_interface->hide_splash_screen (splash->plugin,
                                                splash->loop);

  splash->is_shown = false;

  if (splash->loop != NULL)
    {
      if (splash->plugin_interface->on_boot_progress != NULL)
        {
          ply_event_loop_stop_watching_for_timeout (splash->loop,
                                                    (ply_event_loop_timeout_handler_t)
                                                    ply_boot_splash_update_progress, splash);
        }

      ply_event_loop_stop_watching_for_exit (splash->loop, (ply_event_loop_exit_handler_t)
                                             ply_boot_splash_detach_from_event_loop,
                                             splash);
    }
}

void ply_boot_splash_display_normal  (ply_boot_splash_t              *splash)
{
  assert (splash != NULL);
  assert (splash->plugin_interface != NULL);
  assert (splash->plugin != NULL);
  if (splash->plugin_interface->display_normal != NULL)
      splash->plugin_interface->display_normal (splash->plugin);
}
void ply_boot_splash_display_message (ply_boot_splash_t             *splash,
                                      const char                    *message)
{
  assert (splash != NULL);
  assert (splash->plugin_interface != NULL);
  assert (splash->plugin != NULL);
  if (splash->plugin_interface->display_message != NULL)
    splash->plugin_interface->display_message (splash->plugin, message);
}
void ply_boot_splash_display_password (ply_boot_splash_t             *splash,
                                       const char                    *prompt,
                                       int                            bullets)
{
  assert (splash != NULL);
  assert (splash->plugin_interface != NULL);
  assert (splash->plugin != NULL);
  if (splash->plugin_interface->display_password != NULL)
      splash->plugin_interface->display_password (splash->plugin, prompt, bullets);
}
void ply_boot_splash_display_question (ply_boot_splash_t             *splash,
                                       const char                    *prompt,
                                       const char                    *entry_text)
{
  assert (splash != NULL);
  assert (splash->plugin_interface != NULL);
  assert (splash->plugin != NULL);
  if (splash->plugin_interface->display_question != NULL)
      splash->plugin_interface->display_question (splash->plugin, prompt, entry_text);
}



void
ply_boot_splash_attach_to_event_loop (ply_boot_splash_t *splash,
                                      ply_event_loop_t  *loop)
{
  assert (splash != NULL);
  assert (loop != NULL);
  assert (splash->loop == NULL);

  splash->loop = loop;

  ply_event_loop_watch_for_exit (loop, (ply_event_loop_exit_handler_t) 
                                 ply_boot_splash_detach_from_event_loop,
                                 splash); 
}

static void
on_idle (ply_boot_splash_t *splash)
{

  ply_trace ("splash now idle");
  ply_event_loop_watch_for_timeout (splash->loop, 0.01,
                                    (ply_event_loop_timeout_handler_t)
                                    splash->idle_handler,
                                    splash->idle_handler_user_data);
  splash->idle_handler = NULL;
  splash->idle_handler_user_data = NULL;
}

void
ply_boot_splash_become_idle (ply_boot_splash_t                  *splash,
                             ply_boot_splash_on_idle_handler_t  idle_handler,
                             void                              *user_data)
{
  assert (splash->idle_trigger == NULL);

  ply_trace ("telling splash to become idle");
  if (splash->plugin_interface->become_idle == NULL)
    {
      ply_event_loop_watch_for_timeout (splash->loop, 0.01,
                                        (ply_event_loop_timeout_handler_t)
                                        idle_handler,
                                        user_data);

      return;
    }

  splash->idle_handler = idle_handler;
  splash->idle_handler_user_data = user_data;

  splash->idle_trigger = ply_trigger_new (&splash->idle_trigger);
  ply_trigger_add_handler (splash->idle_trigger,
                           (ply_trigger_handler_t) on_idle,
                           splash);

  splash->plugin_interface->become_idle (splash->plugin, splash->idle_trigger);
}

#ifdef PLY_BOOT_SPLASH_ENABLE_TEST

#include <stdio.h>

#include "ply-event-loop.h"
#include "ply-boot-splash.h"

typedef struct test_state test_state_t;
struct test_state {
  ply_event_loop_t *loop;
  ply_boot_splash_t *splash;
  ply_window_t *window;
  ply_buffer_t *buffer;
};

static void
on_timeout (ply_boot_splash_t *splash)
{
  ply_boot_splash_update_status (splash, "foo");
  ply_event_loop_watch_for_timeout (splash->loop, 
                                    5.0,
                                   (ply_event_loop_timeout_handler_t)
                                   on_timeout,
                                   splash);
}

static void
on_quit (test_state_t *state)
{
    ply_boot_splash_hide (state->splash);
    ply_event_loop_exit (state->loop, 0);
}

int
main (int    argc,
      char **argv)
{
  int exit_code;
  test_state_t state;
  char *tty_name;
  const char *theme_path;

  exit_code = 0;

  state.loop = ply_event_loop_new ();

  if (argc > 1)
    theme_path = argv[1];
  else
    theme_path = PLYMOUTH_THEME_PATH "/fade-in/fade-in.plymouth";

  if (argc > 2)
    asprintf(&tty_name, "tty%s", argv[2]);
  else
    tty_name = strdup("tty0");

  state.window = ply_window_new (tty_name);
  free(tty_name);
  ply_window_attach_to_event_loop (state.window, state.loop);

  if (!ply_window_open (state.window))
    {
      perror ("could not open terminal");
      return errno;
    }

  ply_window_attach_to_event_loop (state.window, state.loop);
  ply_window_add_escape_handler (state.window,
                                 (ply_window_escape_handler_t) on_quit, &state);

  state.buffer = ply_buffer_new ();
  state.splash = ply_boot_splash_new (theme_path, PLYMOUTH_PLUGIN_PATH, state.buffer);
  if (!ply_boot_splash_load (state.splash))
    {
      perror ("could not load splash screen");
      return errno;
    }

  ply_boot_splash_add_window (state.splash, state.window);
  ply_boot_splash_attach_to_event_loop (state.splash, state.loop);

  if (!ply_boot_splash_show (state.splash, PLY_BOOT_SPLASH_MODE_BOOT_UP))
    {
      perror ("could not show splash screen");
      return errno;
    }

  ply_event_loop_watch_for_timeout (state.loop, 
                                    1.0,
                                   (ply_event_loop_timeout_handler_t)
                                   on_timeout,
                                   state.splash);
  exit_code = ply_event_loop_run (state.loop);
  ply_window_free (state.window);
  ply_boot_splash_free (state.splash);
  ply_buffer_free (state.buffer);

  return exit_code;
}

#endif /* PLY_BOOT_SPLASH_ENABLE_TEST */
/* vim: set ts=4 sw=4 expandtab autoindent cindent cino={.5s,(0: */
