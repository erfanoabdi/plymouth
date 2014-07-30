/* ply-label.h - label control
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
#ifndef PLY_LABEL_H
#define PLY_LABEL_H

#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>

#include "ply-event-loop.h"
#include "ply-window.h"

typedef struct _ply_label ply_label_t;

#ifndef PLY_HIDE_FUNCTION_DECLARATIONS
ply_label_t *ply_label_new (void);
void ply_label_free (ply_label_t *label);

bool ply_label_show (ply_label_t      *label,
                     ply_window_t     *window,
                     long              x,
                     long              y);
void ply_label_draw (ply_label_t      *label);
bool ply_label_is_hidden (ply_label_t *label);

void ply_label_set_text (ply_label_t *label,
                         const char  *text);

long ply_label_get_width (ply_label_t *label);
long ply_label_get_height (ply_label_t *label);
#endif

#endif /* PLY_LABEL_H */
/* vim: set ts=4 sw=4 expandtab autoindent cindent cino={.5s,(0: */
