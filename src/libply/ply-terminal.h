/* ply-terminal.h - psuedoterminal abstraction
 *
 * Copyright (C) 2007 Red Hat, Inc.
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
#ifndef PLY_TERMINAL_H
#define PLY_TERMINAL_H

#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>

typedef struct _ply_terminal ply_terminal_t;

#ifndef PLY_HIDE_FUNCTION_DECLARATIONS
ply_terminal_t *ply_terminal_new (void);
void ply_terminal_free (ply_terminal_t *terminal);
bool ply_terminal_create_device (ply_terminal_t *terminal);
bool ply_terminal_has_device (ply_terminal_t *terminal);
void ply_terminal_destroy_device (ply_terminal_t *terminal);
int ply_terminal_get_fd (ply_terminal_t *terminal);
void ply_terminal_set_fd (ply_terminal_t *terminal, int fd);
const char *ply_terminal_get_device_name (ply_terminal_t *terminal);
#endif

#endif /* PLY_TERMINAL_H */
/* vim: set ts=4 sw=4 expandtab autoindent cindent cino={.5s,(0: */
