/* details.c - boot splash plugin
 *
 * Copyright (C) 2008 Red Hat, Inc.
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
#include <termios.h>
#include <unistd.h>
#include <wchar.h>
#include <values.h>

#include "ply-boot-splash-plugin.h"
#include "ply-buffer.h"
#include "ply-event-loop.h"
#include "ply-list.h"
#include "ply-logger.h"
#include "ply-frame-buffer.h"
#include "ply-image.h"
#include "ply-trigger.h"
#include "ply-utils.h"
#include "ply-window.h"

#include <linux/kd.h>

void ask_for_password (ply_boot_splash_plugin_t *plugin,
                       const char               *prompt,
                       ply_trigger_t            *answer);
typedef void (* ply_boot_splash_plugin_window_handler_t) (ply_window_t *window, ply_boot_splash_plugin_t *, void *user_data, void *other_user_data);

ply_boot_splash_plugin_interface_t *ply_boot_splash_plugin_get_interface (void);
struct _ply_boot_splash_plugin
{
  ply_event_loop_t *loop;

  ply_trigger_t *pending_password_answer;
  ply_list_t *windows;

  uint32_t keyboard_input_is_hidden : 1;
};

ply_boot_splash_plugin_t *
create_plugin (void)
{
  ply_boot_splash_plugin_t *plugin;

  ply_trace ("creating plugin");

  plugin = calloc (1, sizeof (ply_boot_splash_plugin_t));
  plugin->windows = ply_list_new ();

  return plugin;
}

void
destroy_plugin (ply_boot_splash_plugin_t *plugin)
{
  ply_trace ("destroying plugin");

  if (plugin == NULL)
    return;

  ply_list_free (plugin->windows);

  free (plugin);
}

static void
detach_from_event_loop (ply_boot_splash_plugin_t *plugin)
{
  plugin->loop = NULL;

  ply_trace ("detaching from event loop");
}


static void
write_text_on_window (ply_window_t             *window,
                      ply_boot_splash_plugin_t *plugin,
                      const char               *text,
                      void                     *user_data);
static void
for_each_window (ply_boot_splash_plugin_t *plugin,
                 ply_boot_splash_plugin_window_handler_t handler,
                 void *user_data,
                 void *other_user_data)
{
  ply_list_node_t *node;

  node = ply_list_get_first_node (plugin->windows);
  while (node != NULL)
    {
      ply_list_node_t *next_node;
      ply_window_t *window;

      next_node = ply_list_get_next_node (plugin->windows, node);

      window = ply_list_node_get_data (node);

      handler (window, plugin, user_data, other_user_data);

      node = next_node;
    }
}

static void
write_text_on_window (ply_window_t             *window,
                      ply_boot_splash_plugin_t *plugin,
                      const char               *text,
                      void                     *user_data)
{
  int fd;
  size_t size;

  size = (size_t) user_data;

  fd = ply_window_get_tty_fd (window);

  write (fd, text, size);
}

void
on_keyboard_input (ply_boot_splash_plugin_t *plugin,
                   const char               *keyboard_input,
                   size_t                    character_size)
{
  if (plugin->keyboard_input_is_hidden)
      for_each_window (plugin,
                       (ply_boot_splash_plugin_window_handler_t)
                       write_text_on_window, (void *) "*",
                       (void *) strlen ("*"));
  else
      for_each_window (plugin,
                       (ply_boot_splash_plugin_window_handler_t)
                       write_text_on_window, (void *) keyboard_input,
                       (void *) character_size);
}

void
on_backspace (ply_boot_splash_plugin_t *plugin)
{
  for_each_window (plugin,
                   (ply_boot_splash_plugin_window_handler_t)
                   ply_window_clear_text_character, NULL, NULL);
}

void
on_enter (ply_boot_splash_plugin_t *plugin,
          const char               *line)
{
  if (plugin->pending_password_answer != NULL)
    {
      ply_trigger_pull (plugin->pending_password_answer, line);
      plugin->keyboard_input_is_hidden = false;
      plugin->pending_password_answer = NULL;

      for_each_window (plugin,
                       (ply_boot_splash_plugin_window_handler_t)
                       ply_window_clear_text_line, NULL, NULL);
    }
}

void
add_window (ply_boot_splash_plugin_t *plugin,
            ply_window_t             *window)
{
  ply_list_append_data (plugin->windows, window);
}

void
remove_window (ply_boot_splash_plugin_t *plugin,
               ply_window_t             *window)
{
  ply_list_remove_data (plugin->windows, window);
}

static void
initialize_window (ply_window_t             *window,
                   ply_boot_splash_plugin_t *plugin)
{
  ply_boot_splash_plugin_interface_t *interface;

  ply_window_set_mode (window, PLY_WINDOW_MODE_TEXT);

  ply_window_set_keyboard_input_handler (window,
                                         (ply_window_keyboard_input_handler_t)
                                         on_keyboard_input, plugin);
  ply_window_set_backspace_handler (window,
                                    (ply_window_backspace_handler_t)
                                    on_backspace, plugin);
  ply_window_set_enter_handler (window,
                                (ply_window_enter_handler_t)
                                on_enter, plugin);

  interface = ply_boot_splash_plugin_get_interface ();

  interface->ask_for_password = ask_for_password;
}

static void
uninitialize_window (ply_window_t             *window,
                     ply_boot_splash_plugin_t *plugin)
{
  ply_window_set_keyboard_input_handler (window, NULL, NULL);
  ply_window_set_backspace_handler (window, NULL, NULL);
  ply_window_set_enter_handler (window, NULL, NULL);
}

bool
show_splash_screen (ply_boot_splash_plugin_t *plugin,
                    ply_event_loop_t         *loop,
                    ply_buffer_t             *boot_buffer)
{
  size_t size;

  assert (plugin != NULL);

  for_each_window (plugin,
                   (ply_boot_splash_plugin_window_handler_t)
                   initialize_window, NULL, NULL);
  plugin->loop = loop;

  ply_event_loop_watch_for_exit (loop, (ply_event_loop_exit_handler_t)
                                 detach_from_event_loop,
                                 plugin);

  size = ply_buffer_get_size (boot_buffer);

  if (size > 0)
    write (STDOUT_FILENO,
           ply_buffer_get_bytes (boot_buffer),
           size);

  return true;
}

void
update_status (ply_boot_splash_plugin_t *plugin,
               const char               *status)
{
  assert (plugin != NULL);

  ply_trace ("status update");
}

void
on_boot_output (ply_boot_splash_plugin_t *plugin,
                const char               *output,
                size_t                    size)
{
  ply_trace ("writing '%s' to all windows (%d bytes)",
             output, size);
  if (size > 0)
    for_each_window (plugin,
                     (ply_boot_splash_plugin_window_handler_t)
                     write_text_on_window, 
                     (void *) output, (void *) size);
}

void
hide_splash_screen (ply_boot_splash_plugin_t *plugin,
                    ply_event_loop_t         *loop)
{
  assert (plugin != NULL);

  ply_trace ("hiding splash screen");

  for_each_window (plugin,
                   (ply_boot_splash_plugin_window_handler_t)
                   uninitialize_window, NULL, NULL);

  if (plugin->pending_password_answer != NULL)
    {
      ply_trigger_pull (plugin->pending_password_answer, "");
      plugin->pending_password_answer = NULL;
      plugin->keyboard_input_is_hidden = false;
    }

  ply_event_loop_stop_watching_for_exit (plugin->loop,
                                         (ply_event_loop_exit_handler_t)
                                         detach_from_event_loop,
                                         plugin);
  detach_from_event_loop (plugin);
}

static void
ask_for_password_on_window (ply_window_t             *window,
                            ply_boot_splash_plugin_t *plugin,
                            const char               *prompt)
{
  int fd;

  ply_window_set_mode (window, PLY_WINDOW_MODE_TEXT);

  fd = ply_window_get_tty_fd (window);

  if (prompt != NULL)
    {
      write (fd, "\r\n", strlen ("\r\n"));
      write (fd, prompt, strlen (prompt));
    }

  write (fd, "\r\nPassword: ", strlen ("\r\nPassword: "));
  plugin->keyboard_input_is_hidden = true;
}

void
ask_for_password (ply_boot_splash_plugin_t *plugin,
                  const char               *prompt,
                  ply_trigger_t            *answer)
{
  plugin->pending_password_answer = answer;

  for_each_window (plugin,
                   (ply_boot_splash_plugin_window_handler_t)
                   ask_for_password_on_window, (void *) prompt, NULL);
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
      .on_boot_output = on_boot_output,
      .hide_splash_screen = hide_splash_screen,
    };

  return &plugin_interface;
}

/* vim: set ts=4 sw=4 expandtab autoindent cindent cino={.5s,(0: */
