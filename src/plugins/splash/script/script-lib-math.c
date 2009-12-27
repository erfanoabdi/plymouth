/* script-lib-math.c - math script functions library
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
#include "ply-utils.h"
#include "script.h"
#include "script-parse.h"
#include "script-execute.h"
#include "script-object.h"
#include "script-lib-math.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "config.h"

#include "script-lib-math.script.h"

static script_return_t script_lib_math_float_from_float_function (script_state_t *state,
                                                                  void           *user_data)
{
  float (*function)(float) = user_data;
  float value = script_obj_hash_get_float (state->local, "value");
  float reply_float = function (value);
  return script_return_obj (script_obj_new_float (reply_float));
}


static script_return_t script_lib_math_float_from_float_float_function (script_state_t *state,
                                                                  void           *user_data)
{
  float (*function)(float, float) = user_data;
  float value1 = script_obj_hash_get_float (state->local, "value_a");
  float value2 = script_obj_hash_get_float (state->local, "value_b");
  float reply_float = function (value1, value2);
  return script_return_obj (script_obj_new_float (reply_float));
}

static script_return_t script_lib_math_int_from_float_function (script_state_t *state,
                                                                void           *user_data)
{
  int (*function)(float) = user_data;
  float value = script_obj_hash_get_float (state->local, "value");
  int reply_int = function (value);
  return script_return_obj (script_obj_new_int (reply_int));
}

static int float_to_int (float value)
{
  return (int) value;
}

script_lib_math_data_t *script_lib_math_setup (script_state_t *state)
{
  script_lib_math_data_t *data = malloc (sizeof (script_lib_math_data_t));

  script_add_native_function (state->global,
                              "MathCos",
                              script_lib_math_float_from_float_function,
                              cosf,
                              "value",
                              NULL);
  script_add_native_function (state->global,
                              "MathSin",
                              script_lib_math_float_from_float_function,
                              sinf,
                              "value",
                              NULL);
  script_add_native_function (state->global,
                              "MathTan",
                              script_lib_math_float_from_float_function,
                              tanf,
                              "value",
                              NULL);
  script_add_native_function (state->global,
                              "MathATan2",
                              script_lib_math_float_from_float_float_function,
                              atan2f,
                              "value_a",
                              "value_b",
                              NULL);
  script_add_native_function (state->global,
                              "MathSqrt",
                              script_lib_math_float_from_float_function,
                              sqrtf,
                              "value",
                              NULL);
  script_add_native_function (state->global,
                              "MathInt",
                              script_lib_math_int_from_float_function,
                              float_to_int,
                              "value",
                              NULL);

  data->script_main_op = script_parse_string (script_lib_math_string);
  script_return_t ret = script_execute (state, data->script_main_op);
  script_obj_unref (ret.object);

  return data;
}

void script_lib_math_destroy (script_lib_math_data_t *data)
{
  script_parse_op_free (data->script_main_op);
  free (data);
}

