/*
 *
 * Copyright (C) 2009 Red Hat, Inc.
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
 * Written by: William Jon McCann
 *
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
#include "ply-logger.h"
#include "ply-frame-buffer.h"
#include "ply-image.h"
#include "ply-key-file.h"
#include "ply-trigger.h"
#include "ply-utils.h"
#include "ply-window.h"

#include "ply-animation.h"
#include "ply-progress-animation.h"

#include <linux/kd.h>

#ifndef FRAMES_PER_SECOND
#define FRAMES_PER_SECOND 30
#endif

#ifndef SHOW_ANIMATION_PERCENT
#define SHOW_ANIMATION_PERCENT 0.9
#endif

typedef enum {
   PLY_BOOT_SPLASH_DISPLAY_NORMAL,
   PLY_BOOT_SPLASH_DISPLAY_QUESTION_ENTRY,
   PLY_BOOT_SPLASH_DISPLAY_PASSWORD_ENTRY
} ply_boot_splash_display_type_t;

struct _ply_boot_splash_plugin
{
  ply_event_loop_t *loop;
  ply_boot_splash_mode_t mode;
  ply_frame_buffer_t *frame_buffer;
  ply_frame_buffer_area_t box_area, lock_area;
  ply_image_t *lock_image;
  ply_image_t *box_image;
  ply_image_t *corner_image;
  ply_window_t *window;

  ply_entry_t *entry;
  ply_animation_t *animation;
  ply_progress_animation_t *progress_animation;
  ply_label_t *label;
  ply_boot_splash_display_type_t state;

  double animation_horizontal_alignment;
  double animation_vertical_alignment;
  char *animation_dir;

  ply_progress_animation_transition_t transition;
  double transition_duration;

  uint32_t background_start_color;
  uint32_t background_end_color;

  ply_trigger_t *idle_trigger;
  ply_trigger_t *stop_trigger;

  uint32_t root_is_mounted : 1;
  uint32_t is_visible : 1;
  uint32_t is_animating : 1;
  uint32_t is_idle : 1;
};

static void add_handlers (ply_boot_splash_plugin_t *plugin);
static void remove_handlers (ply_boot_splash_plugin_t *plugin);
static void stop_animation (ply_boot_splash_plugin_t *plugin);

static void detach_from_event_loop (ply_boot_splash_plugin_t *plugin);

static ply_boot_splash_plugin_t *
create_plugin (ply_key_file_t *key_file)
{
  ply_boot_splash_plugin_t *plugin;
  char *image_dir, *image_path;
  char *alignment;
  char *transition;
  char *transition_duration;
  char *color;

  srand ((int) ply_get_timestamp ());
  plugin = calloc (1, sizeof (ply_boot_splash_plugin_t));

  image_dir = ply_key_file_get_value (key_file, "two-step", "ImageDir");

  asprintf (&image_path, "%s/lock.png", image_dir);
  plugin->lock_image = ply_image_new (image_path);
  free (image_path);

  asprintf (&image_path, "%s/box.png", image_dir);
  plugin->box_image = ply_image_new (image_path);
  free (image_path);

  asprintf (&image_path, "%s/corner-image.png", image_dir);
  plugin->corner_image = ply_image_new (image_path);
  free (image_path);

  plugin->entry = ply_entry_new (image_dir);
  plugin->label = ply_label_new ();
  plugin->animation_dir = image_dir;

  alignment = ply_key_file_get_value (key_file, "two-step", "HorizontalAlignment");
  if (alignment != NULL)
    plugin->animation_horizontal_alignment = strtod (alignment, NULL);
  else
    plugin->animation_horizontal_alignment = .5;
  free (alignment);

  alignment = ply_key_file_get_value (key_file, "two-step", "VerticalAlignment");
  if (alignment != NULL)
    plugin->animation_vertical_alignment = strtod (alignment, NULL);
  else
    plugin->animation_vertical_alignment = .5;
  free (alignment);


  plugin->transition = PLY_PROGRESS_ANIMATION_TRANSITION_NONE;
  transition = ply_key_file_get_value (key_file, "two-step", "Transition");
  if (transition != NULL)
    {
      if (strcmp (transition, "fade-over") == 0)
        plugin->transition = PLY_PROGRESS_ANIMATION_TRANSITION_FADE_OVER;
      else if (strcmp (transition, "cross-fade") == 0)
        plugin->transition = PLY_PROGRESS_ANIMATION_TRANSITION_CROSS_FADE;
      else if (strcmp (transition, "merge-fade") == 0)
        plugin->transition = PLY_PROGRESS_ANIMATION_TRANSITION_MERGE_FADE;
    }
  free (transition);

  transition_duration = ply_key_file_get_value (key_file, "two-step", "TransitionDuration");
  if (transition_duration != NULL)
    plugin->transition_duration = strtod (transition_duration, NULL);
  else
    plugin->transition_duration = 0.0;
  free (transition_duration);

  color = ply_key_file_get_value (key_file, "two-step", "BackgroundStartColor");

  if (color != NULL)
    plugin->background_start_color = strtol (color, NULL, 0);
  else
    plugin->background_start_color = PLYMOUTH_BACKGROUND_START_COLOR;

  free (color);

  color = ply_key_file_get_value (key_file, "two-step", "BackgroundEndColor");

  if (color != NULL)
    plugin->background_end_color = strtol (color, NULL, 0);
  else
    plugin->background_end_color = PLYMOUTH_BACKGROUND_END_COLOR;

  free (color);

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

      ply_event_loop_stop_watching_for_exit (plugin->loop, (ply_event_loop_exit_handler_t)
                                             detach_from_event_loop,
                                             plugin);
      detach_from_event_loop (plugin);
    }

  ply_image_free (plugin->box_image);
  ply_image_free (plugin->lock_image);

  if (plugin->corner_image != NULL)
    ply_image_free (plugin->corner_image);

  ply_entry_free (plugin->entry);
  ply_animation_free (plugin->animation);
  ply_progress_animation_free (plugin->progress_animation);
  ply_label_free (plugin->label);

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
begin_animation (ply_boot_splash_plugin_t *plugin,
                 ply_trigger_t            *trigger)
{
  long width, height;
  ply_frame_buffer_area_t area;
  ply_frame_buffer_get_size (plugin->frame_buffer, &area);
  width = ply_animation_get_width (plugin->animation);
  height = ply_animation_get_height (plugin->animation);
  ply_animation_start (plugin->animation,
                       plugin->loop,
                       plugin->window,
                       trigger,
                       plugin->animation_horizontal_alignment * area.width - width / 2.0,
                       plugin->animation_vertical_alignment * area.height - height / 2.0);
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

  plugin->is_idle = false;

  draw_background (plugin, NULL);

  if (plugin->mode == PLY_BOOT_SPLASH_MODE_SHUTDOWN)
    {
      begin_animation (plugin, NULL);
      return;
    }

  ply_frame_buffer_get_size (plugin->frame_buffer, &area);

  width = ply_progress_animation_get_width (plugin->progress_animation);
  height = ply_progress_animation_get_height (plugin->progress_animation);
  ply_progress_animation_show (plugin->progress_animation,
                               plugin->window,
                               plugin->animation_horizontal_alignment * area.width - width / 2.0,
                               plugin->animation_vertical_alignment * area.height - height / 2.0);

  plugin->is_animating = true;

  ply_window_draw_area (plugin->window, 0, 0, area.width, area.height);
}

static void
stop_animation (ply_boot_splash_plugin_t *plugin)
{
  assert (plugin != NULL);
  assert (plugin->loop != NULL);

  if (!plugin->is_animating)
     return;

  plugin->is_animating = false;

  ply_progress_animation_hide (plugin->progress_animation);
  ply_animation_stop (plugin->animation);

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
  stop_animation (plugin);
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
}

void
on_backspace (ply_boot_splash_plugin_t *plugin)
{
}

void
on_enter (ply_boot_splash_plugin_t *plugin,
          const char               *text)
{
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

  if (plugin->state == PLY_BOOT_SPLASH_DISPLAY_QUESTION_ENTRY ||
      plugin->state == PLY_BOOT_SPLASH_DISPLAY_PASSWORD_ENTRY  )
    {
      ply_entry_draw (plugin->entry);
      ply_label_draw (plugin->label);
    }
  else
    {
      ply_progress_animation_draw (plugin->progress_animation);

      if (plugin->corner_image != NULL)
        {
          ply_frame_buffer_area_t screen_area;
          ply_frame_buffer_area_t image_area;

          ply_frame_buffer_get_size (plugin->frame_buffer, &screen_area);

          image_area.width = ply_image_get_width (plugin->corner_image);
          image_area.height = ply_image_get_height (plugin->corner_image);
          image_area.x = screen_area.width - image_area.width - 20;
          image_area.y = screen_area.height - image_area.height - 20;

          ply_frame_buffer_fill_with_argb32_data (plugin->frame_buffer, &image_area, 0, 0, ply_image_get_data (plugin->corner_image));

        }
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

  if (plugin->background_start_color != plugin->background_end_color)
    ply_frame_buffer_fill_with_gradient (plugin->frame_buffer, &area,
                                         plugin->background_start_color,
                                         plugin->background_end_color);
  else
    ply_frame_buffer_fill_with_hex_color (plugin->frame_buffer, &area,
                                          plugin->background_start_color);
}

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

  plugin->loop = loop;
  plugin->mode = mode;

  plugin->animation = ply_animation_new (plugin->animation_dir,
                                         "throbber-");
  plugin->progress_animation = ply_progress_animation_new (plugin->animation_dir,
                                                           "progress-");
  ply_progress_animation_set_transition (plugin->progress_animation,
                                         plugin->transition,
                                         plugin->transition_duration);

  ply_trace ("loading lock image");
  if (!ply_image_load (plugin->lock_image))
    return false;

  ply_trace ("loading box image");
  if (!ply_image_load (plugin->box_image))
    return false;

  if (plugin->corner_image != NULL)
    {
      ply_trace ("loading corner image");

      if (!ply_image_load (plugin->corner_image))
        {
          ply_image_free (plugin->corner_image);
          plugin->corner_image = NULL;
        }
    }

  ply_trace ("loading entry");
  if (!ply_entry_load (plugin->entry))
    return false;

  ply_trace ("loading animation");
  if (!ply_animation_load (plugin->animation))
    return false;

  ply_trace ("loading progress animation");
  if (!ply_progress_animation_load (plugin->progress_animation))
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

static void
update_status (ply_boot_splash_plugin_t *plugin,
               const char               *status)
{
  assert (plugin != NULL);
}

static void
on_animation_stopped (ply_boot_splash_plugin_t *plugin)
{
  if (plugin->idle_trigger != NULL)
    {
      ply_trigger_pull (plugin->idle_trigger, NULL);
      plugin->idle_trigger = NULL;
    }
  plugin->is_idle = true;
}

static void
on_boot_progress (ply_boot_splash_plugin_t *plugin,
                  double                    duration,
                  double                    percent_done)
{


  if (percent_done >= SHOW_ANIMATION_PERCENT)
    {
      if (ply_animation_is_stopped (plugin->animation))
        {
          plugin->stop_trigger = ply_trigger_new (&plugin->stop_trigger);
          ply_trigger_add_handler (plugin->stop_trigger,
                                   (ply_trigger_handler_t)
                                   on_animation_stopped,
                                   plugin);
          ply_progress_animation_hide (plugin->progress_animation);
          begin_animation (plugin, plugin->stop_trigger);
        }
    }
  else
    {
      double total_duration;

      percent_done *= (1 / SHOW_ANIMATION_PERCENT);
      total_duration = duration / percent_done;

      /* Fun made-up smoothing function to make the growth asymptotic:
       * fraction(time,estimate)=1-2^(-(time^1.45)/estimate) */
      percent_done = 1.0 - pow (2.0, -pow (duration, 1.45) / total_duration) * (1.0 - percent_done);

      ply_progress_animation_set_percent_done (plugin->progress_animation,
                                               percent_done);
    }

  ply_progress_animation_draw (plugin->progress_animation);
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

  if (ply_entry_is_hidden (plugin->entry))
    {
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
    }
  else
    {
      ply_entry_draw (plugin->entry);
    }
  if (prompt != NULL)
    {
      int label_width, label_height;

      ply_label_set_text (plugin->label, prompt);
      label_width = ply_label_get_width (plugin->label);
      label_height = ply_label_get_height (plugin->label);

      x = plugin->box_area.x + plugin->lock_area.width / 2;
      y = plugin->box_area.y + plugin->box_area.height;

      ply_label_show (plugin->label, plugin->window, x, y);
    }
}

static void
on_root_mounted (ply_boot_splash_plugin_t *plugin)
{
  plugin->root_is_mounted = true;
}

static void
become_idle (ply_boot_splash_plugin_t *plugin,
             ply_trigger_t            *idle_trigger)
{
  if (plugin->is_idle)
    {
      ply_trigger_pull (idle_trigger, NULL);
      return;
    }

  plugin->idle_trigger = idle_trigger;

  if (ply_animation_is_stopped (plugin->animation))
    {
      plugin->stop_trigger = ply_trigger_new (&plugin->stop_trigger);
      ply_trigger_add_handler (plugin->stop_trigger,
                               (ply_trigger_handler_t)
                               on_animation_stopped,
                               plugin);
      ply_progress_animation_hide (plugin->progress_animation);
      begin_animation (plugin, plugin->stop_trigger);
    }
}

static void
display_normal (ply_boot_splash_plugin_t *plugin)
{
  if (plugin->state != PLY_BOOT_SPLASH_DISPLAY_NORMAL)
    {
      plugin->state = PLY_BOOT_SPLASH_DISPLAY_NORMAL;
      ply_entry_hide (plugin->entry);
      start_animation (plugin);
    }
}

static void
display_password (ply_boot_splash_plugin_t *plugin,
                  const char               *prompt,
                  int                       bullets)
{
  if (plugin->state == PLY_BOOT_SPLASH_DISPLAY_NORMAL)
    {
      stop_animation (plugin);
    }
  plugin->state = PLY_BOOT_SPLASH_DISPLAY_PASSWORD_ENTRY;
  show_password_prompt (plugin, prompt);
  ply_entry_set_bullet_count (plugin->entry, bullets);
}

static void
display_question (ply_boot_splash_plugin_t *plugin,
                  const char               *prompt,
                  const char               *entry_text)
{
  if (plugin->state == PLY_BOOT_SPLASH_DISPLAY_NORMAL)
    {
      stop_animation (plugin);
    }

  plugin->state = PLY_BOOT_SPLASH_DISPLAY_QUESTION_ENTRY;
  show_password_prompt (plugin, prompt);
  ply_entry_set_text (plugin->entry, entry_text);
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
