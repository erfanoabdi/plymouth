/* script-lib-image.c - scripting system ply-image wrapper
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
#define _GNU_SOURCE
#include "ply-image.h"
#include "ply-utils.h"
#include "script.h"
#include "script-parse.h"
#include "script-object.h"
#include "script-parse.h"
#include "script-execute.h"
#include "script-lib-image.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"

#include "script-lib-image.script.h"

static void image_free (script_obj_t *obj)
{
  ply_image_t *image = obj->data.native.object_data;

  ply_image_free (image);
}

static script_return_t image_new (script_state_t *state,
                                  void           *user_data)
{
  script_lib_image_data_t *data = user_data;
  script_obj_t *reply;
  char *path_filename;
  char *filename = script_obj_hash_get_string (state->local, "filename");
  char *test_string = filename;
  const char *prefix_string = "special://";

  while (*test_string && *prefix_string && *test_string == *prefix_string)
    {
      test_string++;
      prefix_string++;
    }
  if (!*prefix_string)
    {
      if (strcmp (test_string, "logo") == 0)
        path_filename = strdup (PLYMOUTH_LOGO_FILE);
      else
        path_filename = strdup ("");
    }
  else
    asprintf (&path_filename, "%s/%s", data->image_dir, filename);
  ply_image_t *image = ply_image_new (path_filename);
  if (ply_image_load (image))
    reply = script_obj_new_native (image, data->class);
  else
    {
      ply_image_free (image);
      reply = script_obj_new_null ();
    }
  free (filename);
  free (path_filename);
  return script_return_obj (reply);
}

static script_return_t image_get_width (script_state_t *state,
                                        void           *user_data)
{
  script_lib_image_data_t *data = user_data;
  ply_image_t *image = script_obj_as_native_of_class (state->this, data->class);
  if (image)
    return script_return_obj (script_obj_new_number (ply_image_get_width (image)));
  return script_return_obj_null ();
}

static script_return_t image_get_height (script_state_t *state,
                                         void           *user_data)
{
  script_lib_image_data_t *data = user_data;
  ply_image_t *image = script_obj_as_native_of_class (state->this, data->class);
  if (image)
    return script_return_obj (script_obj_new_number (ply_image_get_height (image)));
  return script_return_obj_null ();
}

static script_return_t image_rotate (script_state_t *state,
                                     void           *user_data)
{
  script_lib_image_data_t *data = user_data;
  ply_image_t *image = script_obj_as_native_of_class (state->this, data->class);
  float angle = script_obj_hash_get_number (state->local, "angle");

  if (image)
    {
      ply_image_t *new_image = ply_image_rotate (image,
                                                 ply_image_get_width (image) / 2,
                                                 ply_image_get_height (image) / 2,
                                                 angle);
      return script_return_obj (script_obj_new_native (new_image, data->class));
    }
  return script_return_obj_null ();
}

static script_return_t image_scale (script_state_t *state,
                                    void           *user_data)
{
  script_lib_image_data_t *data = user_data;
  ply_image_t *image = script_obj_as_native_of_class (state->this, data->class);
  int width = script_obj_hash_get_number (state->local, "width");
  int height = script_obj_hash_get_number (state->local, "height");

  if (image)
    {
      ply_image_t *new_image = ply_image_resize (image, width, height);
      return script_return_obj (script_obj_new_native (new_image, data->class));
    }
  return script_return_obj_null ();
}

script_lib_image_data_t *script_lib_image_setup (script_state_t *state,
                                                 char         *image_dir)
{
  script_lib_image_data_t *data = malloc (sizeof (script_lib_image_data_t));

  data->class = script_obj_native_class_new (image_free, "image", data);
  data->image_dir = strdup (image_dir);

  script_obj_t *image_hash = script_obj_hash_get_element (state->global, "Image");
  
  script_add_native_function (image_hash,
                              "_New",
                              image_new,
                              data,
                              "filename",
                              NULL);
  script_add_native_function (image_hash,
                              "_Rotate",
                              image_rotate,
                              data,
                              "angle",
                              NULL);
  script_add_native_function (image_hash,
                              "_Scale",
                              image_scale,
                              data,
                              "width",
                              "height",
                              NULL);
  script_add_native_function (image_hash,
                              "GetWidth",
                              image_get_width,
                              data,
                              NULL);
  script_add_native_function (image_hash,
                              "GetHeight",
                              image_get_height,
                              data,
                              NULL);

  script_obj_unref (image_hash);
  data->script_main_op = script_parse_string (script_lib_image_string, "script-lib-image.script");
  script_return_t ret = script_execute (state, data->script_main_op);
  script_obj_unref (ret.object);
  return data;
}

void script_lib_image_destroy (script_lib_image_data_t *data)
{
  script_obj_native_class_destroy (data->class);
  free (data->image_dir);
  script_parse_op_free (data->script_main_op);
  free (data);
}

