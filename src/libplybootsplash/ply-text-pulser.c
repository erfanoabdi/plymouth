/* ply-text-pulser.c -  simple text based pulsing animation
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

#include "ply-text-pulser.h"
#include "ply-event-loop.h"
#include "ply-array.h"
#include "ply-logger.h"
#include "ply-utils.h"
#include "ply-window.h"

#include <linux/kd.h>

#ifndef FRAMES_PER_SECOND
#define FRAMES_PER_SECOND 10
#endif

#define NUMBER_OF_INDICATOR_COLUMNS 6

struct _ply_text_pulser
{
  ply_event_loop_t *loop;

  ply_window_t            *window;

  int column, row;
  int number_of_rows;
  int number_of_columns;
  int spinner_position;
  double start_time, now;
};

ply_text_pulser_t *
ply_text_pulser_new (void)
{
  ply_text_pulser_t *pulser;

  pulser = calloc (1, sizeof (ply_text_pulser_t));

  pulser->number_of_rows = 0;
  pulser->number_of_columns = 0;
  pulser->row = 0;
  pulser->column = 0;
  pulser->spinner_position = 0;
  pulser->number_of_columns = 40;
  pulser->number_of_rows = 1;

  return pulser;
}

void
ply_text_pulser_free (ply_text_pulser_t *pulser)
{
  if (pulser == NULL)
    return;

  free (pulser);
}

static void
draw_trough (ply_text_pulser_t *pulser,
             int                column,
             int                row)
{
  char *bytes;

  ply_window_set_text_cursor_position (pulser->window,
                                       column,
                                       row);
  ply_window_set_background_color (pulser->window, PLY_WINDOW_COLOR_BROWN);
  bytes = malloc (pulser->number_of_columns);
  memset (bytes, ' ', pulser->number_of_columns);
  write (STDOUT_FILENO, bytes, pulser->number_of_columns);
  free (bytes);
}

static void
animate_at_time (ply_text_pulser_t *pulser,
                 double             time)
{
  ply_window_set_mode (pulser->window, PLY_WINDOW_MODE_TEXT);

  draw_trough (pulser, pulser->column, pulser->row);

  ply_window_set_text_cursor_position (pulser->window,
                                       pulser->column + pulser->spinner_position,
                                       pulser->row);
  pulser->spinner_position = (pulser->number_of_columns - strlen ("      ") + 1)  * (.5 * sin (time) + .5);
  ply_window_set_text_cursor_position (pulser->window,
                                       pulser->column + pulser->spinner_position,
                                       pulser->row);

  ply_window_set_background_color (pulser->window, PLY_WINDOW_COLOR_GREEN);
  write (STDOUT_FILENO, "      ", strlen ("      "));
  ply_window_set_background_color (pulser->window, PLY_WINDOW_COLOR_DEFAULT);
}

static void
on_timeout (ply_text_pulser_t *pulser)
{
  double sleep_time;
  pulser->now = ply_get_timestamp ();

#ifdef REAL_TIME_ANIMATION
  animate_at_time (pulser,
                   pulser->now - pulser->start_time);
#else
  static double time = 0.0;
  time += 1.0 / FRAMES_PER_SECOND;
  animate_at_time (pulser, time);
#endif

  sleep_time = 1.0 / FRAMES_PER_SECOND;
  sleep_time = MAX (sleep_time - (ply_get_timestamp () - pulser->now),
                    0.005);

  ply_event_loop_watch_for_timeout (pulser->loop,
                                    sleep_time,
                                    (ply_event_loop_timeout_handler_t)
                                    on_timeout, pulser);
}

bool
ply_text_pulser_start (ply_text_pulser_t  *pulser,
                       ply_event_loop_t   *loop,
                       ply_window_t       *window,
                       int                 column,
                       int                 row)
{
  assert (pulser != NULL);
  assert (pulser->loop == NULL);

  pulser->loop = loop;
  pulser->window = window;

  pulser->row = row;
  pulser->column = column;

  pulser->start_time = ply_get_timestamp ();

  ply_event_loop_watch_for_timeout (pulser->loop,
                                    1.0 / FRAMES_PER_SECOND,
                                    (ply_event_loop_timeout_handler_t)
                                    on_timeout, pulser);

  return true;
}

void
ply_text_pulser_stop (ply_text_pulser_t *pulser)
{
  pulser->window = NULL;

  if (pulser->loop != NULL)
    {
      ply_event_loop_stop_watching_for_timeout (pulser->loop,
                                                (ply_event_loop_timeout_handler_t)
                                                on_timeout, pulser);
      pulser->loop = NULL;
    }
}

int
ply_text_pulser_get_number_of_columns (ply_text_pulser_t *pulser)
{
  return pulser->number_of_columns;
}

int
ply_text_pulser_get_number_of_rows (ply_text_pulser_t *pulser)
{
  return pulser->number_of_rows;
}

/* vim: set ts=4 sw=4 expandtab autoindent cindent cino={.5s,(0: */
