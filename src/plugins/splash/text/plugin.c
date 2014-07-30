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
#include "ply-key-file.h"
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
  ply_boot_splash_mode_t mode;

  ply_window_t *window;

  ply_text_progress_bar_t *progress_bar;

  char *message;

  uint32_t is_animating : 1;
};

static void hide_splash_screen (ply_boot_splash_plugin_t *plugin,
                                ply_event_loop_t         *loop);
static void add_handlers (ply_boot_splash_plugin_t *plugin);
static void remove_handlers (ply_boot_splash_plugin_t *plugin);

static ply_boot_splash_plugin_t *
create_plugin (ply_key_file_t *key_file)
{
  ply_boot_splash_plugin_t *plugin;

  ply_trace ("creating plugin");

  plugin = calloc (1, sizeof (ply_boot_splash_plugin_t));
  plugin->progress_bar = ply_text_progress_bar_new ();
  plugin->message = NULL;

  return plugin;
}

static void
detach_from_event_loop (ply_boot_splash_plugin_t *plugin)
{
  plugin->loop = NULL;

  ply_trace ("detaching from event loop");
}

static void
destroy_plugin (ply_boot_splash_plugin_t *plugin)
{
  ply_trace ("destroying plugin");

  if (plugin == NULL)
    return;

  remove_handlers (plugin);

  /* It doesn't ever make sense to keep this plugin on screen
   * after exit
   */
  hide_splash_screen (plugin, plugin->loop);

  ply_text_progress_bar_free (plugin->progress_bar);
  if (plugin->message != NULL)
    free (plugin->message);

  free (plugin);
}

static void
show_message (ply_boot_splash_plugin_t *plugin)
{
      int window_width, window_height;

      window_width = ply_window_get_number_of_text_columns (plugin->window);
      window_height = ply_window_get_number_of_text_rows (plugin->window);

      ply_window_set_text_cursor_position (plugin->window,
                                           0, window_height / 2);
      ply_window_clear_text_line (plugin->window);
      ply_window_set_text_cursor_position (plugin->window,
                                           (window_width - strlen (plugin->message)) / 2,
                                           window_height / 2);

      write (STDOUT_FILENO, plugin->message, strlen (plugin->message));
}

static void
start_animation (ply_boot_splash_plugin_t *plugin)
{

  assert (plugin != NULL);
  assert (plugin->loop != NULL);

  if (plugin->message != NULL)
    show_message (plugin);

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

  if (plugin->mode == PLY_BOOT_SPLASH_MODE_SHUTDOWN)
    {
      ply_text_progress_bar_hide (plugin->progress_bar);
      return;
    }

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

static void
on_draw (ply_boot_splash_plugin_t *plugin,
         int                       x,
         int                       y,
         int                       width,
         int                       height)
{
  ply_window_set_background_color (plugin->window, PLY_WINDOW_COLOR_BLUE);
  ply_window_clear_screen (plugin->window);
}

static void
on_erase (ply_boot_splash_plugin_t *plugin,
          int                       x,
          int                       y,
          int                       width,
          int                       height)
{
  ply_window_set_background_color (plugin->window, PLY_WINDOW_COLOR_BLUE);
  ply_window_clear_screen (plugin->window);
}

static void
add_handlers (ply_boot_splash_plugin_t *plugin)
{
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

  ply_window_set_draw_handler (plugin->window, NULL, NULL);
  ply_window_set_erase_handler (plugin->window, NULL, NULL);

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

  ply_window_hide_text_cursor (plugin->window);
  ply_window_set_text_cursor_position (plugin->window, 0, 0);

  plugin->loop = loop;
  plugin->mode = mode;
  ply_event_loop_watch_for_exit (loop, (ply_event_loop_exit_handler_t)
                                 detach_from_event_loop,
                                 plugin);

  ply_show_new_kernel_messages (false);
  start_animation (plugin);

  return true;
}

static void
update_status (ply_boot_splash_plugin_t *plugin,
               const char               *status)
{
  assert (plugin != NULL);

  ply_trace ("status update");
}

static void
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

static void
hide_splash_screen (ply_boot_splash_plugin_t *plugin,
                    ply_event_loop_t         *loop)
{
  assert (plugin != NULL);

  ply_trace ("hiding splash screen");

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
      remove_handlers (plugin);

      ply_window_set_background_color (plugin->window, PLY_WINDOW_COLOR_DEFAULT);
      ply_window_clear_screen (plugin->window);
      ply_window_show_text_cursor (plugin->window);
      ply_window_reset_colors (plugin->window);
    }

  ply_show_new_kernel_messages (true);
}

static void
display_normal (ply_boot_splash_plugin_t *plugin)
{
  start_animation(plugin);
}

static void
display_message (ply_boot_splash_plugin_t *plugin,
                 const char               *message)
{
  if (plugin->message != NULL)
    free (plugin->message);

  plugin->message = strdup (message);
  start_animation (plugin);
}

static void
display_password (ply_boot_splash_plugin_t *plugin,
                  const char               *prompt,
                  int                       bullets)
{
      int window_width, window_height;
      int i;
      stop_animation (plugin);
      ply_window_set_background_color (plugin->window, PLY_WINDOW_COLOR_DEFAULT);
      ply_window_clear_screen (plugin->window);

      window_width = ply_window_get_number_of_text_columns (plugin->window);
      window_height = ply_window_get_number_of_text_rows (plugin->window);
      
      if (!prompt)
        prompt = "Password";
      
      ply_window_set_text_cursor_position (plugin->window, 0, window_height / 2);
      
      for (i=0; i < window_width; i++)
        {
          write (STDOUT_FILENO, " ", strlen (" "));
        }
      ply_window_set_text_cursor_position (plugin->window,
                                        window_width / 2 - (strlen (prompt)),
                                        window_height / 2);
      write (STDOUT_FILENO, prompt, strlen (prompt));
      write (STDOUT_FILENO, ":", strlen (":"));
      
      for (i=0; i < bullets; i++)
        {
          write (STDOUT_FILENO, "*", strlen ("*"));
        }
      ply_window_show_text_cursor (plugin->window);
}

static void
display_question (ply_boot_splash_plugin_t *plugin,
                  const char               *prompt,
                  const char               *entry_text)
{
      int window_width, window_height;
      int i;
      stop_animation (plugin);
      ply_window_set_background_color (plugin->window, PLY_WINDOW_COLOR_DEFAULT);
      ply_window_clear_screen (plugin->window);

      window_width = ply_window_get_number_of_text_columns (plugin->window);
      window_height = ply_window_get_number_of_text_rows (plugin->window);
      
      if (!prompt)
        prompt = "";
      
      ply_window_set_text_cursor_position (plugin->window,
                                        0, window_height / 2);
      
      for (i=0; i < window_width; i++)
        {
          write (STDOUT_FILENO, " ", strlen (" "));
        }
      ply_window_set_text_cursor_position (plugin->window,
                                        window_width / 2 - (strlen (prompt)),
                                        window_height / 2);
      write (STDOUT_FILENO, prompt, strlen (prompt));
      write (STDOUT_FILENO, ":", strlen (":"));
      
      write (STDOUT_FILENO, entry_text, strlen (entry_text));
      ply_window_show_text_cursor (plugin->window);
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
      .display_normal = display_normal,
      .display_message = display_message,
      .display_password = display_password,
      .display_question = display_question,      
    };

  return &plugin_interface;
}

/* vim: set ts=4 sw=4 expandtab autoindent cindent cino={.5s,(0: */
