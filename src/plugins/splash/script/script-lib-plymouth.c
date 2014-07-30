/* script-lib-plymouth.c - script library for interacting with plymouth
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
#include "script-lib-plymouth.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"

#include "script-lib-plymouth.script.h"

static script_return_t plymouth_set_function (script_state_t *state,
                                              void           *user_data)
{
  script_obj_t **script_func = user_data;
  script_obj_t *obj = script_obj_hash_get_element (state->local, "function");

  script_obj_deref (&obj);
  script_obj_unref (*script_func);
  
  if (script_obj_is_function (obj))
    *script_func = obj;
  else
    {
      *script_func = NULL;
      script_obj_unref (obj);
    }
  return script_return_obj_null ();
}

script_lib_plymouth_data_t *script_lib_plymouth_setup (script_state_t *state)
{
  script_lib_plymouth_data_t *data = malloc (sizeof (script_lib_plymouth_data_t));

  data->script_refresh_func = script_obj_new_null ();
  data->script_boot_progress_func = script_obj_new_null ();
  data->script_root_mounted_func = script_obj_new_null ();
  data->script_keyboard_input_func = script_obj_new_null ();
  data->script_update_status_func = script_obj_new_null ();
  data->script_display_normal_func = script_obj_new_null ();
  data->script_display_password_func = script_obj_new_null ();
  data->script_display_question_func = script_obj_new_null ();
  data->script_message_func = script_obj_new_null ();

  script_add_native_function (state->global,
                              "PlymouthSetRefreshFunction",
                              plymouth_set_function,
                              &data->script_refresh_func,
                              "function",
                              NULL);
  script_add_native_function (state->global,
                              "PlymouthSetBootProgressFunction",
                              plymouth_set_function,
                              &data->script_boot_progress_func,
                              "function",
                              NULL);
  script_add_native_function (state->global,
                              "PlymouthSetRootMountedFunction",
                              plymouth_set_function,
                              &data->script_root_mounted_func,
                              "function",
                              NULL);
  script_add_native_function (state->global,
                              "PlymouthSetKeyboardInputFunction",
                              plymouth_set_function,
                              &data->script_keyboard_input_func,
                              "function",
                              NULL);
  script_add_native_function (state->global,
                              "PlymouthSetUpdateStatusFunction",
                              plymouth_set_function,
                              &data->script_update_status_func,
                              "function",
                              NULL);
  script_add_native_function (state->global,
                              "PlymouthSetDisplayNormalFunction",
                              plymouth_set_function,
                              &data->script_display_normal_func,
                              "function",
                              NULL);
  script_add_native_function (state->global,
                              "PlymouthSetDisplayPasswordFunction",
                              plymouth_set_function,
                              &data->script_display_password_func,
                              "function",
                              NULL);
  script_add_native_function (state->global,
                              "PlymouthSetDisplayQuestionFunction",
                              plymouth_set_function,
                              &data->script_display_question_func,
                              "function",
                              NULL);
  script_add_native_function (state->global,
                              "PlymouthSetMessageFunction",
                              plymouth_set_function,
                              &data->script_message_func,
                              "function",
                              NULL);
  data->script_main_op = script_parse_string (script_lib_plymouth_string);
  script_return_t ret = script_execute (state, data->script_main_op);
  script_obj_unref (ret.object);                /* Throw anything sent back away */

  return data;
}

void script_lib_plymouth_destroy (script_lib_plymouth_data_t *data)
{
  script_parse_op_free (data->script_main_op);
  script_obj_unref (data->script_refresh_func);
  script_obj_unref (data->script_boot_progress_func);
  script_obj_unref (data->script_root_mounted_func);
  script_obj_unref (data->script_keyboard_input_func);
  script_obj_unref (data->script_update_status_func);
  script_obj_unref (data->script_display_normal_func);
  script_obj_unref (data->script_display_password_func);
  script_obj_unref (data->script_display_question_func);
  script_obj_unref (data->script_message_func);
  free (data);
}

void script_lib_plymouth_on_refresh (script_state_t             *state,
                                     script_lib_plymouth_data_t *data)
{
  script_function_t *function = script_obj_as_function (data->script_refresh_func);
  if (function)
    {
      script_return_t ret = script_execute_function (state,
                                                     function,
                                                     NULL);
      script_obj_unref (ret.object);
    }
}

void script_lib_plymouth_on_boot_progress (script_state_t             *state,
                                           script_lib_plymouth_data_t *data,
                                           float                       duration,
                                           float                       progress)
{
  script_function_t *function = script_obj_as_function (data->script_boot_progress_func);
  if (function)
    {
      script_obj_t *duration_obj = script_obj_new_float (duration);
      script_obj_t *progress_obj = script_obj_new_float (progress);
      script_return_t ret = script_execute_function (state,
                                                     function,
                                                     duration_obj,
                                                     progress_obj,
                                                     NULL);
      script_obj_unref (ret.object);
      script_obj_unref (duration_obj);
      script_obj_unref (progress_obj);
    }
}

void script_lib_plymouth_on_root_mounted (script_state_t             *state,
                                          script_lib_plymouth_data_t *data)
{
  script_function_t *function = script_obj_as_function (data->script_root_mounted_func);
  if (function)
    {
      script_return_t ret = script_execute_function (state,
                                                     function,
                                                     NULL);
      script_obj_unref (ret.object);
    }
}

void script_lib_plymouth_on_keyboard_input (script_state_t             *state,
                                            script_lib_plymouth_data_t *data,
                                            const char                 *keyboard_input)
{
  script_function_t *function = script_obj_as_function (data->script_keyboard_input_func);
  if (function)
    {
      script_obj_t *keyboard_input_obj = script_obj_new_string (keyboard_input);
      script_return_t ret = script_execute_function (state,
                                                     function,
                                                     keyboard_input_obj,
                                                     NULL);
      script_obj_unref (keyboard_input_obj);
      script_obj_unref (ret.object);
    }
}

void script_lib_plymouth_on_update_status (script_state_t             *state,
                                           script_lib_plymouth_data_t *data,
                                           const char                 *new_status)
{
  script_function_t *function = script_obj_as_function (data->script_update_status_func);
  if (function)
    {
      script_obj_t *new_status_obj = script_obj_new_string (new_status);
      script_return_t ret = script_execute_function (state,
                                                     function,
                                                     new_status_obj,
                                                     NULL);
      script_obj_unref (new_status_obj);
      script_obj_unref (ret.object);
    }
}

void script_lib_plymouth_on_display_normal (script_state_t             *state,
                                            script_lib_plymouth_data_t *data)
{
  script_function_t *function = script_obj_as_function (data->script_display_normal_func);
  if (function)
    {
      script_return_t ret = script_execute_function (state,
                                                     function,
                                                     NULL);
      script_obj_unref (ret.object);
    }
}

void script_lib_plymouth_on_display_password (script_state_t             *state,
                                              script_lib_plymouth_data_t *data,
                                              const char                 *prompt,
                                              int                         bullets)
{
  script_function_t *function = script_obj_as_function (data->script_display_password_func);
  if (function)
    {
      script_obj_t *prompt_obj = script_obj_new_string (prompt);
      script_obj_t *bullets_obj = script_obj_new_int (bullets);
      script_return_t ret = script_execute_function (state,
                                                     function,
                                                     prompt_obj,
                                                     bullets_obj,
                                                     NULL);
      script_obj_unref (prompt_obj);
      script_obj_unref (bullets_obj);
      script_obj_unref (ret.object);
    }
}

void script_lib_plymouth_on_display_question (script_state_t             *state,
                                              script_lib_plymouth_data_t *data,
                                              const char                 *prompt,
                                              const char                 *entry_text)
{
  script_function_t *function = script_obj_as_function (data->script_display_question_func);
  if (function)
    {
      script_obj_t *prompt_obj = script_obj_new_string (prompt);
      script_obj_t *entry_text_obj = script_obj_new_string (entry_text);
      script_return_t ret = script_execute_function (state,
                                                     function,
                                                     prompt_obj,
                                                     entry_text_obj,
                                                     NULL);
      script_obj_unref (prompt_obj);
      script_obj_unref (entry_text_obj);
      script_obj_unref (ret.object);
    }
}

void script_lib_plymouth_on_message (script_state_t             *state,
                                     script_lib_plymouth_data_t *data,
                                     const char                 *message)
{
  script_function_t *function = script_obj_as_function (data->script_message_func);
  if (function)
    {
      script_obj_t *new_message_obj = script_obj_new_string (message);
      script_return_t ret = script_execute_function (state,
                                                     function,
                                                     new_message_obj,
                                                     NULL);
      script_obj_unref (new_message_obj);
      script_obj_unref (ret.object);
    }
}
