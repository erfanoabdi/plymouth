/* progress_bar.c - boot progress_bar
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
 *             Will Woods <wwoods@redhat.com>
 */
#include "config.h"

#include <assert.h>
#include <dirent.h>
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

#include "ply-progress-bar.h"
#include "ply-event-loop.h"
#include "ply-array.h"
#include "ply-logger.h"
#include "ply-frame-buffer.h"
#include "ply-image.h"
#include "ply-utils.h"
#include "ply-window.h"

#include <linux/kd.h>

#ifndef FRAMES_PER_SECOND
#define FRAMES_PER_SECOND 30
#endif

#ifndef BAR_HEIGHT
#define BAR_HEIGHT 16
#endif

struct _ply_progress_bar
{
  ply_window_t            *window;
  ply_frame_buffer_t      *frame_buffer;
  ply_frame_buffer_area_t  area;

  double percent_done;

  uint32_t is_hidden : 1;
};

ply_progress_bar_t *
ply_progress_bar_new (void)
{
  ply_progress_bar_t *progress_bar;

  progress_bar = calloc (1, sizeof (ply_progress_bar_t));

  progress_bar->is_hidden = true;
  progress_bar->percent_done = 0.0;
  progress_bar->area.x = 0;
  progress_bar->area.y = 0;
  progress_bar->area.width = 0;
  progress_bar->area.height = BAR_HEIGHT;

  return progress_bar;
}

void
ply_progress_bar_free (ply_progress_bar_t *progress_bar)
{
  if (progress_bar == NULL)
    return;
  free (progress_bar);
}

static void
erase_progress_bar_area (ply_progress_bar_t *progress_bar)
{
  ply_window_erase_area (progress_bar->window,
                         progress_bar->area.x, progress_bar->area.y,
                         progress_bar->area.width, progress_bar->area.height);
}

static void
ply_progress_bar_update_area (ply_progress_bar_t *progress_bar,
                              long                x,
                              long                y)
{

  long width, height;
  double fraction;

  ply_frame_buffer_get_size (progress_bar->frame_buffer, &progress_bar->area);

  progress_bar->area.x = x;
  progress_bar->area.y = y;
  progress_bar->area.height = BAR_HEIGHT;

  progress_bar->area.width = (long) (progress_bar->area.width * progress_bar->percent_done);
}

void
ply_progress_bar_draw (ply_progress_bar_t *progress_bar)
{

  if (progress_bar->is_hidden)
    return;

  ply_frame_buffer_pause_updates (progress_bar->frame_buffer);
  erase_progress_bar_area (progress_bar);
  ply_progress_bar_update_area (progress_bar, progress_bar->area.x, progress_bar->area.y);
  ply_frame_buffer_fill_with_hex_color (progress_bar->frame_buffer,
                                        &progress_bar->area,
                                        0xffffff); /* white */
  ply_frame_buffer_unpause_updates (progress_bar->frame_buffer);
}

void
ply_progress_bar_show (ply_progress_bar_t *progress_bar,
                       ply_window_t       *window,
                       long                x,
                       long                y)
{
  assert (progress_bar != NULL);

  progress_bar->window = window;
  progress_bar->frame_buffer = ply_window_get_frame_buffer (window);;

  ply_progress_bar_update_area (progress_bar, x, y);

  progress_bar->is_hidden = false;
  ply_progress_bar_draw (progress_bar);
}

void
ply_progress_bar_hide (ply_progress_bar_t *progress_bar)
{
  erase_progress_bar_area (progress_bar);

  progress_bar->frame_buffer = NULL;
  progress_bar->window = NULL;

  progress_bar->is_hidden = true;
}

bool
ply_progress_bar_is_hidden (ply_progress_bar_t *progress_bar)
{
  return progress_bar->is_hidden;
}

long
ply_progress_bar_get_width (ply_progress_bar_t *progress_bar)
{
  return progress_bar->area.width;
}

long
ply_progress_bar_get_height (ply_progress_bar_t *progress_bar)
{
  return progress_bar->area.height;
}

void
ply_progress_bar_set_percent_done (ply_progress_bar_t *progress_bar,
                                   double              percent_done)
{
  progress_bar->percent_done = percent_done;
}

double
ply_progress_bar_get_percent_done (ply_progress_bar_t *progress_bar)
{
  return progress_bar->percent_done;
}

/* vim: set ts=4 sw=4 expandtab autoindent cindent cino={.5s,(0: */
