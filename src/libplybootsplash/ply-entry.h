/* ply-entry.h - simple text entry field
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
#ifndef PLY_ENTRY_H
#define PLY_ENTRY_H

#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>

#include "ply-event-loop.h"
#include "ply-frame-buffer.h"
#include "ply-window.h"

typedef struct _ply_entry ply_entry_t;

#ifndef PLY_HIDE_FUNCTION_DECLARATIONS
ply_entry_t *ply_entry_new (const char *image_dir);
void ply_entry_free (ply_entry_t *entry);
bool ply_entry_load (ply_entry_t *entry);

void ply_entry_show (ply_entry_t         *entry,
                     ply_event_loop_t    *loop,
                     ply_window_t        *window,
                     long                 x,
                     long                 y);
void ply_entry_hide (ply_entry_t *entry);
void ply_entry_draw (ply_entry_t *entry);
bool ply_entry_is_hidden (ply_entry_t *entry);

long ply_entry_get_width (ply_entry_t *entry);
long ply_entry_get_height (ply_entry_t *entry);

void ply_entry_add_bullet (ply_entry_t *entry);
void ply_entry_remove_bullet (ply_entry_t *entry);
void ply_entry_remove_all_bullets (ply_entry_t *entry);
#endif

#endif /* PLY_ENTRY_H */
/* vim: set ts=4 sw=4 expandtab autoindent cindent cino={.5s,(0: */
