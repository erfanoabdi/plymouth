/* spinfinity.c - boot splash plugin
 *
 * Copyright (C) 2007, 2008 Red Hat, Inc.
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
 *             Will Woods <wwoods@redhat.com>
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
#include "ply-label.h"
#include "ply-list.h"
#include "ply-progress-bar.h"
#include "ply-logger.h"
#include "ply-frame-buffer.h"
#include "ply-image.h"
#include "ply-trigger.h"
#include "ply-utils.h"
#include "ply-window.h"

#include "ply-throbber.h"

#include <linux/kd.h>

#ifndef FRAMES_PER_SECOND
#define FRAMES_PER_SECOND 30
#endif

#ifndef BAR_HEIGHT
#define BAR_HEIGHT 16
#endif

struct _ply_boot_splash_plugin
{
  ply_event_loop_t *loop;
  ply_frame_buffer_t *frame_buffer;
  ply_frame_buffer_area_t box_area, lock_area, logo_area, bar_area;
  ply_image_t *logo_image;
  ply_image_t *lock_image;
  ply_image_t *box_image;
  ply_window_t *window;

  ply_entry_t *entry;
  ply_throbber_t *throbber;
  ply_label_t *label;
  ply_progress_bar_t *progress_bar;

  ply_trigger_t *pending_password_answer;
  ply_trigger_t *idle_trigger;

  uint32_t root_is_mounted : 1;
  uint32_t is_visible : 1;
  uint32_t is_animating : 1;
};

static void detach_from_event_loop (ply_boot_splash_plugin_t *plugin);
ply_boot_splash_plugin_t *
create_plugin (void)
{
  ply_boot_splash_plugin_t *plugin;

  srand ((int) ply_get_timestamp ());
  plugin = calloc (1, sizeof (ply_boot_splash_plugin_t));

  plugin->logo_image = ply_image_new (PLYMOUTH_LOGO_FILE);
  plugin->lock_image = ply_image_new (PLYMOUTH_IMAGE_DIR "spinfinity/lock.png");
  plugin->box_image = ply_image_new (PLYMOUTH_IMAGE_DIR "spinfinity/box.png");

  plugin->entry = ply_entry_new (PLYMOUTH_IMAGE_DIR "spinfinity");
  plugin->throbber = ply_throbber_new (PLYMOUTH_IMAGE_DIR "spinfinity",
                                   "throbber-");
  plugin->label = ply_label_new ();
  plugin->progress_bar = ply_progress_bar_new ();

  return plugin;
}

void
destroy_plugin (ply_boot_splash_plugin_t *plugin)
{
  if (plugin == NULL)
    return;

  if (plugin->loop != NULL)
    {
      ply_event_loop_stop_watching_for_exit (plugin->loop, (ply_event_loop_exit_handler_t)
                                             detach_from_event_loop,
                                             plugin);
      detach_from_event_loop (plugin);
    }

  ply_image_free (plugin->logo_image);
  ply_image_free (plugin->box_image);
  ply_image_free (plugin->lock_image);
  ply_entry_free (plugin->entry);
  ply_throbber_free (plugin->throbber);
  ply_label_free (plugin->label);
  ply_progress_bar_free (plugin->progress_bar);

  free (plugin);
}

static void
draw_background (ply_boot_splash_plugin_t *plugin,
                 ply_frame_buffer_area_t  *area)
{
  ply_frame_buffer_area_t screen_area;

  if (area == NULL)
    {
      ply_frame_buffer_get_size (plugin->frame_buffer, &screen_area);
      area = &screen_area;
    }

  ply_window_erase_area (plugin->window, area->x, area->y,
                         area->width, area->height);
}

static void
draw_logo (ply_boot_splash_plugin_t *plugin)
{
  uint32_t *logo_data;
  long width, height;

  width = ply_image_get_width (plugin->logo_image);
  height = ply_image_get_height (plugin->logo_image);
  logo_data = ply_image_get_data (plugin->logo_image);
  ply_frame_buffer_get_size (plugin->frame_buffer, &plugin->logo_area);
  plugin->logo_area.x = (plugin->logo_area.width / 2) - (width / 2);
  plugin->logo_area.y = (plugin->logo_area.height / 2) - (height / 2);
  plugin->logo_area.width = width;
  plugin->logo_area.height = height;

  ply_frame_buffer_pause_updates (plugin->frame_buffer);
  draw_background (plugin, &plugin->logo_area);
  ply_frame_buffer_fill_with_argb32_data (plugin->frame_buffer, 
                                          &plugin->logo_area, 0, 0,
                                          logo_data);
  ply_frame_buffer_unpause_updates (plugin->frame_buffer);
}

static void
start_animation (ply_boot_splash_plugin_t *plugin)
{

  long width, height;
  ply_frame_buffer_area_t area;
  assert (plugin != NULL);
  assert (plugin->loop != NULL);

  if (plugin->is_animating)
     return;

  draw_background (plugin, NULL);
  draw_logo (plugin);

  ply_frame_buffer_get_size (plugin->frame_buffer, &area);

  width = ply_throbber_get_width (plugin->throbber);
  height = ply_throbber_get_height (plugin->throbber);
  ply_throbber_start (plugin->throbber,
                  plugin->loop,
                  plugin->window,
                  area.width / 2.0 - width / 2.0,
                  plugin->logo_area.y + plugin->logo_area.height + height / 2);
  ply_progress_bar_show (plugin->progress_bar,
                         plugin->window,
                         0, area.height - ply_progress_bar_get_height (plugin->progress_bar));

  plugin->is_animating = true;
}

static void
stop_animation (ply_boot_splash_plugin_t *plugin,
                ply_trigger_t            *trigger)
{
  int i;

  assert (plugin != NULL);
  assert (plugin->loop != NULL);

  if (!plugin->is_animating)
     return;

  plugin->is_animating = false;

  ply_progress_bar_hide (plugin->progress_bar);
  ply_throbber_stop (plugin->throbber, trigger);

#ifdef ENABLE_FADE_OUT
  for (i = 0; i < 10; i++)
    {
      ply_frame_buffer_fill_with_hex_color_at_opacity (plugin->frame_buffer, NULL,
                                                       PLYMOUTH_BACKGROUND_COLOR,
                                                       .1 + .1 * i);
    }

  ply_frame_buffer_fill_with_hex_color (plugin->frame_buffer, NULL,
                                        PLYMOUTH_BACKGROUND_COLOR);

  for (i = 0; i < 20; i++)
    {
      ply_frame_buffer_fill_with_color (plugin->frame_buffer, NULL,
                                        0.0, 0.0, 0.0, .05 + .05 * i);
    }

  ply_frame_buffer_fill_with_color (plugin->frame_buffer, NULL,
                                    0.0, 0.0, 0.0, 1.0);
#endif
}

static void
on_interrupt (ply_boot_splash_plugin_t *plugin)
{
  ply_event_loop_exit (plugin->loop, 1);
  stop_animation (plugin, NULL);
  ply_window_set_mode (plugin->window, PLY_WINDOW_MODE_TEXT);
}

static void
detach_from_event_loop (ply_boot_splash_plugin_t *plugin)
{
  plugin->loop = NULL;
}

void
on_keyboard_input (ply_boot_splash_plugin_t *plugin,
                   const char               *keyboard_input,
                   size_t                    character_size)
{
  if (plugin->pending_password_answer == NULL)
    return;

  ply_entry_add_bullet (plugin->entry);
}

void
on_backspace (ply_boot_splash_plugin_t *plugin)
{
  ply_entry_remove_bullet (plugin->entry);
}

void
on_enter (ply_boot_splash_plugin_t *plugin,
          const char               *text)
{
  if (plugin->pending_password_answer == NULL)
    return;

  ply_trigger_pull (plugin->pending_password_answer, text);
  plugin->pending_password_answer = NULL;

  ply_entry_hide (plugin->entry);
  ply_entry_remove_all_bullets (plugin->entry);
  start_animation (plugin);
}

void
on_draw (ply_boot_splash_plugin_t *plugin,
         int                       x,
         int                       y,
         int                       width,
         int                       height)
{
  ply_frame_buffer_area_t area;

  area.x = x;
  area.y = y;
  area.width = width;
  area.height = height;

  ply_frame_buffer_pause_updates (plugin->frame_buffer);
  draw_background (plugin, &area);

  if (plugin->pending_password_answer != NULL)
    {
      ply_entry_draw (plugin->entry);
      ply_label_draw (plugin->label);
    }
  else
    {
      draw_logo (plugin);
      ply_progress_bar_draw (plugin->progress_bar);
    }
  ply_frame_buffer_unpause_updates (plugin->frame_buffer);
}

void
on_erase (ply_boot_splash_plugin_t *plugin,
          int                       x,
          int                       y,
          int                       width,
          int                       height)
{
  ply_frame_buffer_area_t area;

  area.x = x;
  area.y = y;
  area.width = width;
  area.height = height;

  ply_frame_buffer_fill_with_gradient (plugin->frame_buffer, &area,
                                       PLYMOUTH_BACKGROUND_START_COLOR,
                                       PLYMOUTH_BACKGROUND_END_COLOR);
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
  assert (plugin->logo_image != NULL);

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

  plugin->loop = loop;

  ply_trace ("loading logo image");
  if (!ply_image_load (plugin->logo_image))
    return false;

  ply_trace ("loading lock image");
  if (!ply_image_load (plugin->lock_image))
    return false;

  ply_trace ("loading box image");
  if (!ply_image_load (plugin->box_image))
    return false;

  ply_trace ("loading entry");
  if (!ply_entry_load (plugin->entry))
    return false;

  ply_trace ("loading throbber");
  if (!ply_throbber_load (plugin->throbber))
    return false;

  ply_trace ("setting graphics mode");
  if (!ply_window_set_mode (plugin->window, PLY_WINDOW_MODE_GRAPHICS))
    return false;

  plugin->frame_buffer = ply_window_get_frame_buffer (plugin->window);

  ply_event_loop_watch_for_exit (loop, (ply_event_loop_exit_handler_t)
                                 detach_from_event_loop,
                                 plugin);

  ply_event_loop_watch_signal (plugin->loop,
                               SIGINT,
                               (ply_event_handler_t) 
                               on_interrupt, plugin);

  ply_window_clear_screen (plugin->window);
  ply_window_hide_text_cursor (plugin->window);

  ply_trace ("starting boot animation");
  start_animation (plugin);

  plugin->is_visible = true;

  return true;
}

void
update_status (ply_boot_splash_plugin_t *plugin,
               const char               *status)
{
  assert (plugin != NULL);
}

void
on_boot_progress (ply_boot_splash_plugin_t *plugin,
                  double                    duration,
                  double                    percent_done)
{
  double total_duration;

  total_duration = duration / percent_done;

  /* Fun made-up smoothing function to make the growth asymptotic:
   * fraction(time,estimate)=1-2^(-(time^1.45)/estimate) */
  percent_done = 1.0 - pow (2.0, -pow (duration, 1.45) / total_duration) * (1.0 - percent_done);

  ply_progress_bar_set_percent_done (plugin->progress_bar, percent_done);
  ply_progress_bar_draw (plugin->progress_bar);
}

void
hide_splash_screen (ply_boot_splash_plugin_t *plugin,
                    ply_event_loop_t         *loop)
{
  assert (plugin != NULL);

  if (plugin->pending_password_answer != NULL)
    {
      ply_trigger_pull (plugin->pending_password_answer, "");
      plugin->pending_password_answer = NULL;
    }

  ply_window_set_keyboard_input_handler (plugin->window, NULL, NULL);
  ply_window_set_backspace_handler (plugin->window, NULL, NULL);
  ply_window_set_enter_handler (plugin->window, NULL, NULL);
  ply_window_set_draw_handler (plugin->window, NULL, NULL);
  ply_window_set_erase_handler (plugin->window, NULL, NULL);

  if (plugin->loop != NULL)
    {
      stop_animation (plugin, NULL);

      ply_event_loop_stop_watching_for_exit (plugin->loop, (ply_event_loop_exit_handler_t)
                                             detach_from_event_loop,
                                             plugin);
      detach_from_event_loop (plugin);
    }

  plugin->frame_buffer = NULL;
  plugin->is_visible = false;

  ply_window_set_mode (plugin->window, PLY_WINDOW_MODE_TEXT);
}

static void
show_password_prompt (ply_boot_splash_plugin_t *plugin,
                      const char               *prompt)
{
  ply_frame_buffer_area_t area;
  int x, y;
  int entry_width, entry_height;

  uint32_t *box_data, *lock_data;

  assert (plugin != NULL);

  draw_background (plugin, NULL);

  ply_frame_buffer_get_size (plugin->frame_buffer, &area);
  plugin->box_area.width = ply_image_get_width (plugin->box_image);
  plugin->box_area.height = ply_image_get_height (plugin->box_image);
  plugin->box_area.x = area.width / 2.0 - plugin->box_area.width / 2.0;
  plugin->box_area.y = area.height / 2.0 - plugin->box_area.height / 2.0;

  plugin->lock_area.width = ply_image_get_width (plugin->lock_image);
  plugin->lock_area.height = ply_image_get_height (plugin->lock_image);

  entry_width = ply_entry_get_width (plugin->entry);
  entry_height = ply_entry_get_height (plugin->entry);

  x = area.width / 2.0 - (plugin->lock_area.width + entry_width) / 2.0 + plugin->lock_area.width;
  y = area.height / 2.0 - entry_height / 2.0;

  plugin->lock_area.x = area.width / 2.0 - (plugin->lock_area.width + entry_width) / 2.0;
  plugin->lock_area.y = area.height / 2.0 - plugin->lock_area.height / 2.0;

  box_data = ply_image_get_data (plugin->box_image);
  ply_frame_buffer_fill_with_argb32_data (plugin->frame_buffer,
                                          &plugin->box_area, 0, 0,
                                          box_data);

  ply_entry_show (plugin->entry, plugin->loop, plugin->window, x, y);

  lock_data = ply_image_get_data (plugin->lock_image);
  ply_frame_buffer_fill_with_argb32_data (plugin->frame_buffer,
                                          &plugin->lock_area, 0, 0,
                                          lock_data);

  if (prompt != NULL)
    {
      int label_width, label_height;

      ply_label_set_text (plugin->label, prompt);
      label_width = ply_label_get_width (plugin->label);
      label_height = ply_label_get_height (plugin->label);

      x = plugin->box_area.x + plugin->lock_area.width / 2;
      y = plugin->box_area.y + plugin->box_area.height + label_height;

      ply_label_show (plugin->label, plugin->window, x, y);
    }

}

void
ask_for_password (ply_boot_splash_plugin_t *plugin,
                  const char               *prompt,
                  ply_trigger_t            *answer)
{
  plugin->pending_password_answer = answer;

  if (ply_entry_is_hidden (plugin->entry))
    {
      stop_animation (plugin, NULL);
      show_password_prompt (plugin, prompt);
    }
  else
    {
      ply_entry_draw (plugin->entry);
      ply_label_draw (plugin->label);
    }
}

void
on_root_mounted (ply_boot_splash_plugin_t *plugin)
{
  plugin->root_is_mounted = true;
}

void
become_idle (ply_boot_splash_plugin_t *plugin,
             ply_trigger_t            *idle_trigger)
{
  stop_animation (plugin, idle_trigger);
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
      .on_root_mounted = on_root_mounted,
      .become_idle = become_idle,
    };

  return &plugin_interface;
}

/* vim: set ts=4 sw=4 expandtab autoindent cindent cino={.5s,(0: */
