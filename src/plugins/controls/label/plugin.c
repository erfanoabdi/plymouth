/* ply-label.c - label control
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
#include <values.h>
#include <unistd.h>
#include <wchar.h>

#include <glib.h>
#include <cairo.h>
#include <pango/pangocairo.h>

#include "ply-frame-buffer.h"
#include "ply-utils.h"
#include "ply-window.h"

#include "ply-label-plugin.h"

struct _ply_label_plugin_control
{
  ply_event_loop_t   *loop;
  ply_window_t       *window;
  ply_frame_buffer_t *frame_buffer;
  ply_frame_buffer_area_t  area;

  PangoLayout        *pango_layout;
  cairo_t            *cairo_context;
  cairo_surface_t    *cairo_surface;
  char               *text;

  uint32_t is_hidden : 1;
};

ply_label_plugin_control_t *
create_control (void)
{
  ply_label_plugin_control_t *label;

  label = calloc (1, sizeof (ply_label_plugin_control_t));

  label->is_hidden = true;

  return label;
}

void
destroy_control (ply_label_plugin_control_t *label)
{
  if (label == NULL)
    return;

  cairo_destroy (label->cairo_context);
  cairo_surface_destroy (label->cairo_surface);
  g_object_unref (label->pango_layout);

  free (label);
}

long
get_width_of_control (ply_label_plugin_control_t *label)
{
  int width;

  pango_layout_get_size (label->pango_layout, &width, NULL);

  return (long) ((double) width / PANGO_SCALE)+1;
}

long
get_height_of_control (ply_label_plugin_control_t *label)
{
  int height;

  pango_layout_get_size (label->pango_layout, NULL, &height);

  return (long) ((double) height / PANGO_SCALE)+1;
}

static void
erase_label_area (ply_label_plugin_control_t *label)
{
  ply_window_erase_area (label->window,
                         label->area.x, label->area.y,
                         label->area.width, label->area.height);
}

void
draw_control (ply_label_plugin_control_t *label)
{

  if (label->is_hidden)
    return;

  ply_frame_buffer_pause_updates (label->frame_buffer);
  erase_label_area (label);
  cairo_move_to (label->cairo_context,
                 label->area.x + 1,
                 label->area.y + 1);
  cairo_set_source_rgba (label->cairo_context, 0.0, 0.0, 0.0, 0.7);
  pango_cairo_show_layout (label->cairo_context,
                           label->pango_layout);
  cairo_move_to (label->cairo_context,
                 label->area.x,
                 label->area.y);
  cairo_set_source_rgb (label->cairo_context, 1.0, 1.0, 1.0);
  pango_cairo_show_layout (label->cairo_context,
                           label->pango_layout);
  cairo_surface_flush (label->cairo_surface);
  ply_frame_buffer_unpause_updates (label->frame_buffer);
}

void
set_text_for_control (ply_label_plugin_control_t *label,
                      const char  *text)
{
  if (label->text != text)
    {
      free (label->text);
      label->text = strdup (text);
    }

  if (label->pango_layout != NULL)
    {
      pango_layout_set_text (label->pango_layout, text, -1);
      pango_cairo_update_layout (label->cairo_context, label->pango_layout);

      label->area.width = get_width_of_control (label);
      label->area.height = get_height_of_control (label);

    }
}

bool
show_control (ply_label_plugin_control_t *label,
              ply_window_t               *window,
              long                        x,
              long                        y)
{
  PangoFontDescription *description;
  ply_frame_buffer_area_t size;
  unsigned char *data;

  label->window = window;
  label->frame_buffer = ply_window_get_frame_buffer (window);
  data = (unsigned char *) ply_frame_buffer_get_bytes (label->frame_buffer);

  label->area.x = x;
  label->area.y = y;

  ply_frame_buffer_get_size (label->frame_buffer, &size);

  label->cairo_surface = cairo_image_surface_create_for_data (data,
                                                              CAIRO_FORMAT_ARGB32,
                                                              size.width,
                                                              size.height,
                                                              size.width * 4);

  label->cairo_context = cairo_create (label->cairo_surface);
  label->pango_layout = pango_cairo_create_layout (label->cairo_context);

  if (label->text != NULL)
    set_text_for_control (label, label->text);

  description = pango_font_description_from_string ("Sans 12");
  pango_layout_set_font_description (label->pango_layout, description);
  pango_font_description_free (description);

  label->is_hidden = false;

  draw_control (label);

  return true;
}

void
hide_control (ply_label_plugin_control_t *label)
{
  erase_label_area (label);

  g_object_unref (label->pango_layout);
  label->pango_layout = NULL;

  cairo_destroy (label->cairo_context);
  label->cairo_context = NULL;

  cairo_surface_destroy (label->cairo_surface);
  label->cairo_surface = NULL;

  label->frame_buffer = NULL;
  label->window = NULL;
  label->loop = NULL;

  label->is_hidden = true;
}

bool
is_control_hidden (ply_label_plugin_control_t *label)
{
  return label->is_hidden;
}

ply_label_plugin_interface_t *
ply_label_plugin_get_interface (void)
{
  static ply_label_plugin_interface_t plugin_interface =
    {
      .create_control = create_control,
      .destroy_control = destroy_control,
      .show_control = show_control,
      .hide_control = hide_control,
      .draw_control = draw_control,
      .is_control_hidden = is_control_hidden,
      .set_text_for_control = set_text_for_control,
      .get_width_of_control = get_width_of_control,
      .get_height_of_control = get_height_of_control
    };

  return &plugin_interface;
}

/* vim: set ts=4 sw=4 expandtab autoindent cindent cino={.5s,(0: */
