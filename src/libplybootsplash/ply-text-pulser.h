/* ply-text-pulser.h - simple text based pulsing animation
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
 * Written By: Ray Strode <rstrode@redhat.com>
 */
#ifndef PLY_TEXT_PULSER_H
#define PLY_TEXT_PULSER_H

#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>

#include "ply-event-loop.h"
#include "ply-window.h"

typedef struct _ply_text_pulser ply_text_pulser_t;

#ifndef PLY_HIDE_FUNCTION_DECLARATIONS
ply_text_pulser_t *ply_text_pulser_new (void);
void ply_text_pulser_free (ply_text_pulser_t *pulser);

bool ply_text_pulser_load (ply_text_pulser_t *pulser);
bool ply_text_pulser_start (ply_text_pulser_t  *pulser,
                            ply_event_loop_t   *loop,
                            ply_window_t       *window,
                            int                 column,
                            int                 row);
void ply_text_pulser_stop (ply_text_pulser_t *pulser);

int ply_text_pulser_get_number_of_rows (ply_text_pulser_t *pulser);
int ply_text_pulser_get_number_of_columns (ply_text_pulser_t *pulser);
#endif

#endif /* PLY_TEXT_PULSER_H */
/* vim: set ts=4 sw=4 expandtab autoindent cindent cino={.5s,(0: */
