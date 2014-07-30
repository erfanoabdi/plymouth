/* text.c - boot splash plugin
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
 * Written by: Adam Jackson <ajax@redhat.com>
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
#include <termios.h>
#include <unistd.h>
#include <values.h>
#include <wchar.h>

#include "ply-trigger.h"
#include "ply-boot-splash-plugin.h"
#include "ply-buffer.h"
#include "ply-event-loop.h"
#include "ply-list.h"
#include "ply-logger.h"
#include "ply-frame-buffer.h"
#include "ply-image.h"
#include "ply-text-progress-bar.h"
#include "ply-utils.h"
#include "ply-window.h"

#include <linux/kd.h>

#define CLEAR_LINE_SEQUENCE "\033[2K\r\n"
#define BACKSPACE "\b\033[0K"

struct _ply_boot_splash_plugin
{
  ply_event_loop_t *loop;

  ply_trigger_t *pending_password_answer;
  ply_window_t *window;

  ply_text_progress_bar_t *progress_bar;

  uint32_t keyboard_input_is_hidden : 1;
  uint32_t is_animating : 1;
};
void hide_splash_screen (ply_boot_splash_plugin_t *plugin,
                         ply_event_loop_t         *loop);

ply_boot_splash_plugin_t *
create_plugin (void)
{
  ply_boot_splash_plugin_t *plugin;

  ply_trace ("creating plugin");

  plugin = calloc (1, sizeof (ply_boot_splash_plugin_t));
  plugin->progress_bar = ply_text_progress_bar_new ();

  return plugin;
}

static void
detach_from_event_loop (ply_boot_splash_plugin_t *plugin)
{
  plugin->loop = NULL;

  ply_trace ("detaching from event loop");
}

void
destroy_plugin (ply_boot_splash_plugin_t *plugin)
{
  ply_trace ("destroying plugin");

  if (plugin == NULL)
    return;

  /* It doesn't ever make sense to keep this plugin on screen
   * after exit
   */
  hide_splash_screen (plugin, plugin->loop);

  ply_text_progress_bar_free (plugin->progress_bar);

  free (plugin);
}

static void
start_animation (ply_boot_splash_plugin_t *plugin)
{

  int window_width, window_height;
  int width, height;
  assert (plugin != NULL);
  assert (plugin->loop != NULL);

  if (plugin->is_animating)
     return;

  ply_window_set_color_hex_value (plugin->window,
                                  PLY_WINDOW_COLOR_BLACK,
                                  0x000000);
  ply_window_set_color_hex_value (plugin->window,
                                  PLY_WINDOW_COLOR_WHITE,
                                  0xffffff);
  ply_window_set_color_hex_value (plugin->window,
                                  PLY_WINDOW_COLOR_BLUE,
                                  0x0073B3);
  ply_window_set_color_hex_value (plugin->window,
                                  PLY_WINDOW_COLOR_BROWN,
                                  0x00457E);
#if 0
  ply_window_set_color_hex_value (plugin->window,
                                  PLY_WINDOW_COLOR_BLUE,
                                  PLYMOUTH_BACKGROUND_START_COLOR);
  ply_window_set_color_hex_value (plugin->window,
                                  PLY_WINDOW_COLOR_GREEN,
                                  PLYMOUTH_BACKGROUND_COLOR);
#endif

  ply_window_set_background_color (plugin->window, PLY_WINDOW_COLOR_BLACK);
  ply_window_clear_screen (plugin->window);
  ply_window_hide_text_cursor (plugin->window);

  ply_text_progress_bar_show (plugin->progress_bar,
                              plugin->window);

  plugin->is_animating = true;
}

static void
stop_animation (ply_boot_splash_plugin_t *plugin)
{
  assert (plugin != NULL);
  assert (plugin->loop != NULL);

  if (!plugin->is_animating)
     return;

  plugin->is_animating = false;


  ply_text_progress_bar_hide (plugin->progress_bar);
}

void
on_keyboard_input (ply_boot_splash_plugin_t *plugin,
                   const char               *keyboard_input,
                   size_t                    character_size)
{
  if (plugin->keyboard_input_is_hidden)
    write (STDOUT_FILENO, "•", strlen ("•"));
  else
    write (STDOUT_FILENO, keyboard_input, character_size);
}

void
on_backspace (ply_boot_splash_plugin_t *plugin)
{
  write (STDOUT_FILENO, BACKSPACE, strlen (BACKSPACE));
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

      start_animation (plugin);
    }
}

void
on_draw (ply_boot_splash_plugin_t *plugin,
         int                       x,
         int                       y,
         int                       width,
         int                       height)
{
  ply_window_set_background_color (plugin->window, PLY_WINDOW_COLOR_BLUE);
  ply_window_clear_screen (plugin->window);
}

void
on_erase (ply_boot_splash_plugin_t *plugin,
          int                       x,
          int                       y,
          int                       width,
          int                       height)
{
  ply_window_set_background_color (plugin->window, PLY_WINDOW_COLOR_BLUE);
  ply_window_clear_screen (plugin->window);
}

void
add_window (ply_boot_splash_plugin_t *plugin,
            ply_window_t             *window)
{
  plugin->window = window;
}

void
remove_window (ply_boot_splash_plugin_t *plugin,
               ply_window_t             *window)
{
  plugin->window = NULL;
}

bool
show_splash_screen (ply_boot_splash_plugin_t *plugin,
                    ply_event_loop_t         *loop,
                    ply_buffer_t             *boot_buffer)
{
  assert (plugin != NULL);

  ply_window_set_keyboard_input_handler (plugin->window,
                                         (ply_window_keyboard_input_handler_t)
                                         on_keyboard_input, plugin);
  ply_window_set_backspace_handler (plugin->window,
                                    (ply_window_backspace_handler_t)
                                    on_backspace, plugin);
  ply_window_set_enter_handler (plugin->window,
                                (ply_window_enter_handler_t)
                                on_enter, plugin);
  ply_window_set_draw_handler (plugin->window,
                               (ply_window_draw_handler_t)
                                on_draw, plugin);
  ply_window_set_erase_handler (plugin->window,
                                (ply_window_erase_handler_t)
                                on_erase, plugin);

  ply_window_hide_text_cursor (plugin->window);
  ply_window_set_text_cursor_position (plugin->window, 0, 0);

  plugin->loop = loop;
  ply_event_loop_watch_for_exit (loop, (ply_event_loop_exit_handler_t)
                                 detach_from_event_loop,
                                 plugin);

  ply_show_new_kernel_messages (false);
  start_animation (plugin);

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
on_boot_progress (ply_boot_splash_plugin_t *plugin,
                  double                    duration,
                  double                    percent_done)
{
  double total_duration;

  total_duration = duration / percent_done;

  /* Hi Will! */
  percent_done = 1.0 - pow (2.0, -pow (duration, 1.45) / total_duration) * (1.0 - percent_done);

  ply_text_progress_bar_set_percent_done (plugin->progress_bar, percent_done);
  ply_text_progress_bar_draw (plugin->progress_bar);
}

void
hide_splash_screen (ply_boot_splash_plugin_t *plugin,
                    ply_event_loop_t         *loop)
{
  assert (plugin != NULL);

  ply_trace ("hiding splash screen");

  if (plugin->pending_password_answer != NULL)
    {
      ply_trigger_pull (plugin->pending_password_answer, "");
      plugin->pending_password_answer = NULL;
    }

  if (plugin->loop != NULL)
    {
      stop_animation (plugin);

      ply_event_loop_stop_watching_for_exit (plugin->loop,
                                             (ply_event_loop_exit_handler_t)
                                             detach_from_event_loop,
                                             plugin);
      detach_from_event_loop (plugin);
    }

  if (plugin->window != NULL)
    {
      ply_window_set_keyboard_input_handler (plugin->window, NULL, NULL);
      ply_window_set_backspace_handler (plugin->window, NULL, NULL);
      ply_window_set_enter_handler (plugin->window, NULL, NULL);
      ply_window_set_draw_handler (plugin->window, NULL, NULL);
      ply_window_set_erase_handler (plugin->window, NULL, NULL);

      ply_window_set_background_color (plugin->window, PLY_WINDOW_COLOR_DEFAULT);
      ply_window_clear_screen (plugin->window);
      ply_window_show_text_cursor (plugin->window);
      ply_window_reset_colors (plugin->window);
    }

  ply_show_new_kernel_messages (true);
}

void
ask_for_password (ply_boot_splash_plugin_t *plugin,
                  const char               *prompt,
                  ply_trigger_t            *answer)
{
  int window_width, window_height;

  plugin->pending_password_answer = answer;

  stop_animation (plugin);
  ply_window_set_background_color (plugin->window, PLY_WINDOW_COLOR_DEFAULT);
  ply_window_clear_screen (plugin->window);

  window_width = ply_window_get_number_of_text_columns (plugin->window);
  window_height = ply_window_get_number_of_text_rows (plugin->window);

  if (prompt != NULL)
    {
      ply_window_set_text_cursor_position (plugin->window,
                                           window_width / 2 - strlen (prompt) / 2,
                                           window_height / 2 - 1);
      write (STDOUT_FILENO, prompt, strlen (prompt));
    }
  ply_window_set_text_cursor_position (plugin->window,
                                       window_width / 2 - strlen ("Password:        "),
                                       window_height / 2);
  write (STDOUT_FILENO, "Password: ", strlen ("Password: "));
  ply_window_show_text_cursor (plugin->window);
  plugin->keyboard_input_is_hidden = true;
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
      .ask_for_password = ask_for_password,
    };

  return &plugin_interface;
}

/* vim: set ts=4 sw=4 expandtab autoindent cindent cino={.5s,(0: */
