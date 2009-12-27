/* plugin.c - boot script plugin
 *
 * Copyright (C) 2007, 2008 Red Hat, Inc.
 *               2008, 2009 Charlie Brej <cbrej@cs.man.ac.uk>
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
 * Written by: Charlie Brej <cbrej@cs.man.ac.uk>
 *             Ray Strode <rstrode@redhat.com>
 */
#include "config.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <values.h>
#include <unistd.h>
#include <wchar.h>

#include "ply-boot-splash-plugin.h"
#include "ply-buffer.h"
#include "ply-entry.h"
#include "ply-event-loop.h"
#include "ply-key-file.h"
#include "ply-label.h"
#include "ply-list.h"
#include "ply-logger.h"
#include "ply-frame-buffer.h"
#include "ply-image.h"
#include "ply-trigger.h"
#include "ply-utils.h"
#include "ply-window.h"

#include "script.h"
#include "script-parse.h"
#include "script-object.h"
#include "script-execute.h"
#include "script-lib-image.h"
#include "script-lib-sprite.h"
#include "script-lib-plymouth.h"
#include "script-lib-math.h"

#include <linux/kd.h>

#ifndef FRAMES_PER_SECOND
#define FRAMES_PER_SECOND 50
#endif

struct _ply_boot_splash_plugin
{
  ply_event_loop_t      *loop;
  ply_boot_splash_mode_t mode;
  ply_frame_buffer_t    *frame_buffer;
  ply_window_t          *window;

  char *script_filename;
  char *image_dir;

  script_state_t                *script_state;
  script_op_t                   *script_main_op;
  script_lib_sprite_data_t      *script_sprite_lib;
  script_lib_image_data_t       *script_image_lib;
  script_lib_plymouth_data_t    *script_plymouth_lib;
  script_lib_math_data_t        *script_math_lib;

  uint32_t is_animating : 1;
};

static void add_handlers (ply_boot_splash_plugin_t *plugin);
static void remove_handlers (ply_boot_splash_plugin_t *plugin);
static void detach_from_event_loop (ply_boot_splash_plugin_t *plugin);
static void stop_animation (ply_boot_splash_plugin_t *plugin);
ply_boot_splash_plugin_interface_t *ply_boot_splash_plugin_get_interface (void);

static ply_boot_splash_plugin_t *
create_plugin (ply_key_file_t *key_file)
{
  ply_boot_splash_plugin_t *plugin;
  plugin = calloc (1, sizeof (ply_boot_splash_plugin_t));
  plugin->image_dir = ply_key_file_get_value (key_file, "script", "ImageDir");
  plugin->script_filename = ply_key_file_get_value (key_file,
                                                    "script",
                                                    "ScriptFile");
  return plugin;
}

static void
destroy_plugin (ply_boot_splash_plugin_t *plugin)
{
  if (plugin == NULL)
    return;
  remove_handlers (plugin);

  if (plugin->loop != NULL)
    {
      stop_animation (plugin);
      ply_event_loop_stop_watching_for_exit (plugin->loop,
                                             (ply_event_loop_exit_handler_t)
                                             detach_from_event_loop,
                                             plugin);
      detach_from_event_loop (plugin);
    }
  free (plugin->script_filename);
  free (plugin->image_dir);
  free (plugin);
}

static void
on_timeout (ply_boot_splash_plugin_t *plugin)
{
  double sleep_time;

  script_lib_plymouth_on_refresh (plugin->script_state,
                                  plugin->script_plymouth_lib);
  script_lib_sprite_refresh (plugin->script_sprite_lib);

  sleep_time = 1.0 / FRAMES_PER_SECOND;
  ply_event_loop_watch_for_timeout (plugin->loop,
                                    sleep_time,
                                    (ply_event_loop_timeout_handler_t)
                                    on_timeout, plugin);
}

static void
on_boot_progress (ply_boot_splash_plugin_t *plugin,
                  double                    duration,
                  double                    percent_done)
{
  script_lib_plymouth_on_boot_progress (plugin->script_state,
                                        plugin->script_plymouth_lib,
                                        duration,
                                        percent_done);
}

static bool
start_animation (ply_boot_splash_plugin_t *plugin)
{
  ply_frame_buffer_area_t area;

  assert (plugin != NULL);
  assert (plugin->loop != NULL);

  if (plugin->is_animating)
    return true;
  ply_frame_buffer_get_size (plugin->frame_buffer, &area);

  ply_trace ("parsing script file");
  plugin->script_main_op = script_parse_file (plugin->script_filename);
  plugin->script_state = script_state_new (plugin);
  plugin->script_image_lib = script_lib_image_setup (plugin->script_state,
                                                     plugin->image_dir);
  plugin->script_sprite_lib = script_lib_sprite_setup (plugin->script_state,
                                                       plugin->window);
  plugin->script_plymouth_lib = script_lib_plymouth_setup (plugin->script_state);
  plugin->script_math_lib = script_lib_math_setup (plugin->script_state);

  ply_trace ("executing script file");
  script_return_t ret = script_execute (plugin->script_state,
                                        plugin->script_main_op);
  script_obj_unref (ret.object);
  on_timeout (plugin);

  plugin->is_animating = true;
  return true;
}

static void
stop_animation (ply_boot_splash_plugin_t *plugin)
{
  assert (plugin != NULL);
  assert (plugin->loop != NULL);

  if (!plugin->is_animating)
    return;
  plugin->is_animating = false;
  if (plugin->loop != NULL)
    ply_event_loop_stop_watching_for_timeout (plugin->loop,
                                              (ply_event_loop_timeout_handler_t)
                                              on_timeout, plugin);
  script_state_destroy (plugin->script_state);
  script_lib_sprite_destroy (plugin->script_sprite_lib);
  script_lib_image_destroy (plugin->script_image_lib);
  script_lib_plymouth_destroy (plugin->script_plymouth_lib);
  script_lib_math_destroy (plugin->script_math_lib);
  script_parse_op_free (plugin->script_main_op);
}

static void
on_interrupt (ply_boot_splash_plugin_t *plugin)
{
  ply_event_loop_exit (plugin->loop, 1);
  stop_animation (plugin);
  ply_window_set_mode (plugin->window, PLY_WINDOW_MODE_TEXT);
}

static void
detach_from_event_loop (ply_boot_splash_plugin_t *plugin)
{
  plugin->loop = NULL;
}

static void
on_keyboard_input (ply_boot_splash_plugin_t *plugin,
                   const char               *keyboard_input,
                   size_t                    character_size)
{
  char keyboard_string[character_size + 1];

  memcpy (keyboard_string, keyboard_input, character_size);
  keyboard_string[character_size] = '\0';

  script_lib_plymouth_on_keyboard_input (plugin->script_state,
                                         plugin->script_plymouth_lib,
                                         keyboard_string);
}

static void
on_backspace (ply_boot_splash_plugin_t *plugin)
{}

static void
on_enter (ply_boot_splash_plugin_t *plugin,
          const char               *text)
{}

static void
on_draw (ply_boot_splash_plugin_t *plugin,
         int                       x,
         int                       y,
         int                       width,
         int                       height)
{}

static void
on_erase (ply_boot_splash_plugin_t *plugin,
          int                       x,
          int                       y,
          int                       width,
          int                       height)
{}

static void
add_handlers (ply_boot_splash_plugin_t *plugin)
{
  ply_window_add_keyboard_input_handler (plugin->window,
                                         (ply_window_keyboard_input_handler_t)
                                         on_keyboard_input, plugin);
  ply_window_add_backspace_handler (plugin->window,
                                    (ply_window_backspace_handler_t)
                                    on_backspace, plugin);
  ply_window_add_enter_handler (plugin->window,
                                (ply_window_enter_handler_t)
                                on_enter, plugin);
  ply_window_set_draw_handler (plugin->window,
                               (ply_window_draw_handler_t)
                               on_draw, plugin);
  ply_window_set_erase_handler (plugin->window,
                                (ply_window_erase_handler_t)
                                on_erase, plugin);
}

static void
remove_handlers (ply_boot_splash_plugin_t *plugin)
{
  ply_window_remove_keyboard_input_handler (plugin->window, (ply_window_keyboard_input_handler_t) on_keyboard_input);
  ply_window_remove_backspace_handler (plugin->window, (ply_window_backspace_handler_t) on_backspace);
  ply_window_remove_enter_handler (plugin->window, (ply_window_enter_handler_t) on_enter);
}

static void
add_window (ply_boot_splash_plugin_t *plugin,
            ply_window_t             *window)
{
  plugin->window = window;
}

static void
remove_window (ply_boot_splash_plugin_t *plugin,
               ply_window_t             *window)
{
  plugin->window = NULL;
}

static bool
show_splash_screen (ply_boot_splash_plugin_t *plugin,
                    ply_event_loop_t         *loop,
                    ply_buffer_t             *boot_buffer,
                    ply_boot_splash_mode_t    mode)
{
  assert (plugin != NULL);

  add_handlers (plugin);

  plugin->loop = loop;
  plugin->mode = mode;

  plugin->frame_buffer = ply_window_get_frame_buffer (plugin->window);

  ply_event_loop_watch_for_exit (loop, (ply_event_loop_exit_handler_t)
                                 detach_from_event_loop,
                                 plugin);

  ply_event_loop_watch_signal (plugin->loop,
                               SIGINT,
                               (ply_event_handler_t)
                               on_interrupt, plugin);

  ply_trace ("setting graphics mode");
  if (!ply_window_set_mode (plugin->window, PLY_WINDOW_MODE_GRAPHICS))
    return false;
  ply_window_clear_screen (plugin->window);
  ply_window_hide_text_cursor (plugin->window);

  ply_trace ("starting boot animation");
  return start_animation (plugin);
}

static void
update_status (ply_boot_splash_plugin_t *plugin,
               const char               *status)
{
  assert (plugin != NULL);
  script_lib_plymouth_on_update_status (plugin->script_state,
                                        plugin->script_plymouth_lib,
                                        status);
}

static void
hide_splash_screen (ply_boot_splash_plugin_t *plugin,
                    ply_event_loop_t         *loop)
{
  assert (plugin != NULL);

  remove_handlers (plugin);

  if (plugin->loop != NULL)
    {
      stop_animation (plugin);

      ply_event_loop_stop_watching_for_exit (plugin->loop,
                                             (ply_event_loop_exit_handler_t)
                                             detach_from_event_loop,
                                             plugin);
      detach_from_event_loop (plugin);
    }
  plugin->frame_buffer = NULL;

  ply_window_set_mode (plugin->window, PLY_WINDOW_MODE_TEXT);
}

static void
on_root_mounted (ply_boot_splash_plugin_t *plugin)
{
  script_lib_plymouth_on_root_mounted (plugin->script_state,
                                       plugin->script_plymouth_lib);
}

static void
become_idle (ply_boot_splash_plugin_t *plugin,
             ply_trigger_t            *idle_trigger)
{
  ply_trigger_pull (idle_trigger, NULL);
}

static void
display_normal (ply_boot_splash_plugin_t *plugin)
{
  script_lib_plymouth_on_display_normal (plugin->script_state,
                                         plugin->script_plymouth_lib);
}

static void
display_password (ply_boot_splash_plugin_t *plugin,
                  const char               *prompt,
                  int                       bullets)
{
  script_lib_plymouth_on_display_password (plugin->script_state,
                                           plugin->script_plymouth_lib,
                                           prompt,
                                           bullets);
}

static void
display_question (ply_boot_splash_plugin_t *plugin,
                  const char               *prompt,
                  const char               *entry_text)
{
  script_lib_plymouth_on_display_question (plugin->script_state,
                                           plugin->script_plymouth_lib,
                                           prompt,
                                           entry_text);
}

ply_boot_splash_plugin_interface_t *
ply_boot_splash_plugin_get_interface (void)
{
  static ply_boot_splash_plugin_interface_t plugin_interface =
  {
    .create_plugin = create_plugin,
    .destroy_plugin = destroy_plugin,
    .add_window = add_window,
    .remove_window = remove_window,
    .show_splash_screen = show_splash_screen,
    .update_status = update_status,
    .on_boot_progress = on_boot_progress,
    .hide_splash_screen = hide_splash_screen,
    .on_root_mounted = on_root_mounted,
    .become_idle = become_idle,
    .display_normal = display_normal,
    .display_password = display_password,
    .display_question = display_question,
  };

  return &plugin_interface;
}

/* vim: set ts=4 sw=4 expandtab autoindent cindent cino={.5s,(0: */
