/* script-lib-sprite.h - script library controling sprites
 *
 * Copyright (C) 2009 Charlie Brej <cbrej@cs.man.ac.uk>
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
 * Written by: Charlie Brej <cbrej@cs.man.ac.uk>
 */
#ifndef SCRIPT_LIB_SPRITE
#define SCRIPT_LIB_SPRITE

#include "script.h"

typedef struct
{
  ply_window_t              *window;
  ply_list_t                *sprite_list;
  script_obj_native_class_t *class;
  script_op_t               *script_main_op;
  uint32_t                   background_color_start;
  uint32_t                   background_color_end;
  bool                       full_refresh;
} script_lib_sprite_data_t;

typedef struct
{
  int           x;
  int           y;
  int           z;
  float         opacity;
  int           old_x;
  int           old_y;
  int           old_z;
  int           old_width;
  int           old_height;
  float         old_opacity;
  bool          refresh_me;
  bool          remove_me;
  ply_image_t  *image;
  script_obj_t *image_obj;
} sprite_t;

script_lib_sprite_data_t *script_lib_sprite_setup (script_state_t *state,
                                                   ply_window_t   *window);
void script_lib_sprite_refresh (script_lib_sprite_data_t *data);
void script_lib_sprite_destroy (script_lib_sprite_data_t *data);

#endif /* SCRIPT_LIB_SPRITE */
