/* fade-throbber.c - boot splash plugin
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
#include "ply-list.h"
#include "ply-logger.h"
#include "ply-frame-buffer.h"
#include "ply-image.h"
#include "ply-key-file.h"
#include "ply-trigger.h"
#include "ply-utils.h"
#include "ply-window.h"

#include <linux/kd.h>

#ifndef FRAMES_PER_SECOND
#define FRAMES_PER_SECOND 30
#endif

typedef enum {
   PLY_BOOT_SPLASH_DISPLAY_NORMAL,
   PLY_BOOT_SPLASH_DISPLAY_QUESTION_ENTRY,
   PLY_BOOT_SPLASH_DISPLAY_PASSWORD_ENTRY
} ply_boot_splash_display_type_t;

typedef struct
{
  unsigned int x;
  unsigned int y;
  double start_time;
  double speed;
} star_t;

struct _ply_boot_splash_plugin
{
  ply_event_loop_t *loop;
  ply_boot_splash_mode_t mode;
  ply_frame_buffer_t *frame_buffer;
  ply_image_t *logo_image;
  ply_image_t *star_image;
  ply_image_t *lock_image;
  ply_list_t *stars;
  ply_window_t *window;

  ply_entry_t *entry;
  ply_boot_splash_display_type_t state;

  double start_time;
  double now;

  uint32_t is_animating : 1;
};

static ply_boot_splash_plugin_t *
create_plugin (ply_key_file_t *key_file)
{
  ply_boot_splash_plugin_t *plugin;
  char *image_dir, *image_path;

  srand ((int) ply_get_timestamp ());
  plugin = calloc (1, sizeof (ply_boot_splash_plugin_t));
  plugin->start_time = 0.0;

  plugin->logo_image = ply_image_new (PLYMOUTH_LOGO_FILE);
  image_dir = ply_key_file_get_value (key_file, "fade-throbber", "ImageDir");

  asprintf (&image_path, "%s/star.png", image_dir);
  plugin->star_image = ply_image_new (image_path);
  free (image_path);

  asprintf (&image_path, "%s/lock.png", image_dir);
  plugin->lock_image = ply_image_new (image_path);
  free (image_path);

  plugin->entry = ply_entry_new (image_dir);
  free (image_dir);

  plugin->state = PLY_BOOT_SPLASH_DISPLAY_NORMAL;
  plugin->stars = ply_list_new ();

  return plugin;
}

star_t *
star_new (int    x,
          int    y,
          double speed)
{
  star_t *star;

  star = calloc (1, sizeof (star_t));
  star->x = x;
  star->y = y;
  star->speed = speed;
  star->start_time = ply_get_timestamp ();

  return star;
}

static void
star_free (star_t *star)
{
  free (star);
}

static void
free_stars (ply_boot_splash_plugin_t *plugin)
{
  ply_list_node_t *node;

  assert (plugin != NULL);

  node = ply_list_get_first_node (plugin->stars);
  while (node != NULL)
    {
      ply_list_node_t *next_node;
      star_t *star;

      star = (star_t *) ply_list_node_get_data (node);

      next_node = ply_list_get_next_node (plugin->stars, node);

      star_free (star);
      node = next_node;
    }

  ply_list_free (plugin->stars);
  plugin->stars = NULL;
}

static void add_handlers (ply_boot_splash_plugin_t *plugin);
static void remove_handlers (ply_boot_splash_plugin_t *plugin);

static void detach_from_event_loop (ply_boot_splash_plugin_t *plugin);

static void
destroy_plugin (ply_boot_splash_plugin_t *plugin)
{
  if (plugin == NULL)
    return;

  remove_handlers (plugin);

  if (plugin->loop != NULL)
    {
      ply_event_loop_stop_watching_for_exit (plugin->loop, (ply_event_loop_exit_handler_t)
                                             detach_from_event_loop,
                                             plugin);
      detach_from_event_loop (plugin);
    }

  free_stars (plugin);
  ply_image_free (plugin->logo_image);
  ply_image_free (plugin->star_image);
  ply_image_free (plugin->lock_image);
  ply_entry_free (plugin->entry);
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
animate_at_time (ply_boot_splash_plugin_t *plugin,
                 double                    time)
{
  ply_list_node_t *node;
  ply_frame_buffer_area_t logo_area, star_area;
  uint32_t *logo_data, *star_data;
  long width, height;
  static double last_opacity = 0.0;
  double opacity = 0.0;

  ply_window_set_mode (plugin->window, PLY_WINDOW_MODE_GRAPHICS);

  ply_frame_buffer_pause_updates (plugin->frame_buffer);

  width = ply_image_get_width (plugin->logo_image);
  height = ply_image_get_height (plugin->logo_image);
  logo_data = ply_image_get_data (plugin->logo_image);
  ply_frame_buffer_get_size (plugin->frame_buffer, &logo_area);
  logo_area.x = (logo_area.width / 2) - (width / 2);
  logo_area.y = (logo_area.height / 2) - (height / 2);
  logo_area.width = width;
  logo_area.height = height;

  star_data = ply_image_get_data (plugin->star_image);
  star_area.width = ply_image_get_width (plugin->star_image);
  star_area.height = ply_image_get_height (plugin->star_image);

  node = ply_list_get_first_node (plugin->stars);
  while (node != NULL)
    {
      ply_list_node_t *next_node;
      star_t *star;

      star = (star_t *) ply_list_node_get_data (node);
      next_node = ply_list_get_next_node (plugin->stars, node);

      star_area.x = star->x;
      star_area.y = star->y;

      opacity = .5 * sin (((plugin->now - star->start_time) / star->speed) * (2 * M_PI)) + .5;
      opacity = CLAMP (opacity, 0, 1.0);

      draw_background (plugin, &star_area);
      ply_frame_buffer_fill_with_argb32_data_at_opacity (plugin->frame_buffer, 
                                                         &star_area, 0, 0, 
                                                         star_data, opacity);

      node = next_node;
    }

  opacity = .5 * sin ((time / 5) * (2 * M_PI)) + .8;
  opacity = CLAMP (opacity, 0, 1.0);

  if (plugin->mode == PLY_BOOT_SPLASH_MODE_SHUTDOWN)
    opacity = 1.0;

  if (fabs (opacity - last_opacity) <= DBL_MIN)
    {
      ply_frame_buffer_unpause_updates (plugin->frame_buffer);
      return;
    }

  last_opacity = opacity;

  draw_background (plugin, &logo_area);
  ply_frame_buffer_fill_with_argb32_data_at_opacity (plugin->frame_buffer,
                                                     &logo_area, 0, 0,
                                                     logo_data, opacity);
  ply_frame_buffer_unpause_updates (plugin->frame_buffer);
}

static void
on_timeout (ply_boot_splash_plugin_t *plugin)
{
  double sleep_time;

  ply_window_set_mode (plugin->window, PLY_WINDOW_MODE_GRAPHICS);
  plugin->now = ply_get_timestamp ();

  /* The choice below is between
   *
   * 1) keeping a constant animation speed, and dropping
   * frames when necessary
   * 2) showing every frame, but slowing down the animation
   * when a frame would be otherwise dropped.
   *
   * It turns out there are parts of boot up where the animation
   * can get sort of choppy.  By default we choose 2, since the
   * nature of this animation means it looks natural even when it
   * is slowed down
   */
#ifdef REAL_TIME_ANIMATION
  animate_at_time (plugin,
                   plugin->now - plugin->start_time);
#else
  static double time = 0.0;
  time += 1.0 / FRAMES_PER_SECOND;
  animate_at_time (plugin, time);
#endif

  sleep_time = 1.0 / FRAMES_PER_SECOND;
  sleep_time = MAX (sleep_time - (ply_get_timestamp () - plugin->now),
                    0.005);

  ply_event_loop_watch_for_timeout (plugin->loop, 
                                    sleep_time,
                                    (ply_event_loop_timeout_handler_t)
                                    on_timeout, plugin);
}

static void
start_animation (ply_boot_splash_plugin_t *plugin)
{
  assert (plugin != NULL);
  assert (plugin->loop != NULL);
    
  if (plugin->is_animating)
     return;

  draw_background (plugin, NULL);

  plugin->start_time = ply_get_timestamp ();
  animate_at_time (plugin, plugin->start_time);

  if (plugin->mode == PLY_BOOT_SPLASH_MODE_SHUTDOWN)
    return;

  ply_event_loop_watch_for_timeout (plugin->loop, 
                                    1.0 / FRAMES_PER_SECOND,
                                    (ply_event_loop_timeout_handler_t)
                                    on_timeout, plugin);

  plugin->is_animating = true;
}

static void
stop_animation (ply_boot_splash_plugin_t *plugin)
{
  int i;

  assert (plugin != NULL);
  assert (plugin->loop != NULL);

  if (!plugin->is_animating)
     return;

  plugin->is_animating = false;

  for (i = 0; i < 10; i++)
    {
      ply_frame_buffer_fill_with_hex_color_at_opacity (plugin->frame_buffer,
                                                       NULL,
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

  if (plugin->loop != NULL)
    {
      ply_event_loop_stop_watching_for_timeout (plugin->loop,
                                                (ply_event_loop_timeout_handler_t)
                                                on_timeout, plugin);
    }
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

  ply_window_set_mode (plugin->window, PLY_WINDOW_MODE_TEXT);
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

  draw_background (plugin, &area);

  if (plugin->state == PLY_BOOT_SPLASH_DISPLAY_NORMAL)
    animate_at_time (plugin, plugin->now);
  else
    ply_entry_draw (plugin->entry);
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
  assert (plugin->logo_image != NULL);

  add_handlers (plugin);

  plugin->loop = loop;
  plugin->mode = mode;

  ply_trace ("loading logo image");
  if (!ply_image_load (plugin->logo_image))
    return false;

  ply_trace ("loading star image");
  if (!ply_image_load (plugin->star_image))
    return false;

  ply_trace ("loading lock image");
  if (!ply_image_load (plugin->lock_image))
    return false;

  ply_trace ("loading entry");
  if (!ply_entry_load (plugin->entry))
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

  return true;
}

static void
add_star (ply_boot_splash_plugin_t *plugin)
{
  ply_frame_buffer_area_t area, logo_area;
  star_t *star;
  unsigned int x, y;
  unsigned int width, height;
  ply_list_node_t *node;

  assert (plugin != NULL);

  ply_frame_buffer_get_size (plugin->frame_buffer, &area);
  width = ply_image_get_width (plugin->logo_image);
  height = ply_image_get_height (plugin->logo_image);
  logo_area.x = (area.width / 2) - (width / 2);
  logo_area.y = (area.height / 2) - (height / 2);
  logo_area.width = width;
  logo_area.height = height;

  width = ply_image_get_width (plugin->star_image);
  height = ply_image_get_height (plugin->star_image);

  node = NULL;
  do
    {
      x = rand () % area.width;
      y = rand () % area.height;

      if ((x <= logo_area.x + logo_area.width)
           && (x >= logo_area.x)
          && (y >= logo_area.y)
           && (y <= logo_area.y + logo_area.height))
          continue;

      if ((x + width >= logo_area.x)
           && (x + width <= logo_area.x + logo_area.width)
          && (y + height >= logo_area.y)
           && (y + height <= logo_area.y + logo_area.height))
          continue;

      node = ply_list_get_first_node (plugin->stars);
      while (node != NULL)
        {
          ply_list_node_t *next_node;

          star = (star_t *) ply_list_node_get_data (node);
          next_node = ply_list_get_next_node (plugin->stars, node);

          if ((x <= star->x + width)
               && (x >= star->x)
              && (y >= star->y)
               && (y <= star->y + height))
              break;

          if ((x + width >= star->x)
               && (x + width <= star->x + width)
              && (y + height >= star->y)
               && (y + height <= star->y + height))
              break;

          node = next_node;
        }

    } while (node != NULL);

  star = star_new (x, y, (double) ((rand () % 50) + 1));
  ply_list_append_data (plugin->stars, star);
}

static void
update_status (ply_boot_splash_plugin_t *plugin,
               const char               *status)
{
  assert (plugin != NULL);

  add_star (plugin);
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

  ply_window_set_mode (plugin->window, PLY_WINDOW_MODE_TEXT);
}

static void
show_password_entry (ply_boot_splash_plugin_t *plugin)
{
  ply_frame_buffer_area_t area;
  ply_frame_buffer_area_t lock_area;
  int x, y;
  int entry_width, entry_height;
  uint32_t *lock_data;

  assert (plugin != NULL);

  if (ply_entry_is_hidden (plugin->entry))
    {
      draw_background (plugin, NULL);

      ply_frame_buffer_get_size (plugin->frame_buffer, &area);

      entry_width = ply_entry_get_width (plugin->entry);
      entry_height = ply_entry_get_height (plugin->entry);

      lock_area.width = ply_image_get_width (plugin->lock_image);
      lock_area.height = ply_image_get_height (plugin->lock_image);
      lock_area.x = area.width / 2.0 - (lock_area.width + entry_width) / 2.0;
      lock_area.y = area.height / 2.0 - lock_area.height / 2.0;

      x = area.width / 2.0 - (lock_area.width + entry_width) / 2.0 + lock_area.width;
      y = area.height / 2.0 - entry_height / 2.0;

      ply_entry_show (plugin->entry, plugin->loop, plugin->window, x, y);
      
      lock_data = ply_image_get_data (plugin->lock_image);
      ply_frame_buffer_fill_with_argb32_data (plugin->frame_buffer,
                                              &lock_area, 0, 0,
                                              lock_data);
    }
  else
    {
      ply_entry_draw (plugin->entry);
    }
}

static void
display_normal (ply_boot_splash_plugin_t *plugin)
{
  if (plugin->state ==  PLY_BOOT_SPLASH_DISPLAY_QUESTION_ENTRY ||
      plugin->state ==  PLY_BOOT_SPLASH_DISPLAY_PASSWORD_ENTRY)
    {
      plugin->state = PLY_BOOT_SPLASH_DISPLAY_NORMAL;
      ply_entry_hide (plugin->entry);
      start_animation(plugin);
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
  show_password_entry (plugin);
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
  show_password_entry (plugin);
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
      .hide_splash_screen = hide_splash_screen,
      .display_normal = display_normal,
      .display_password = display_password,
      .display_question = display_question,
    };

  return &plugin_interface;
}

/* vim: set ts=4 sw=4 expandtab autoindent cindent cino={.5s,(0: */
