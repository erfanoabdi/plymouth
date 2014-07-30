/* script-object.c - functions to work with script objects
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
#include "ply-hashtable.h"
#include "ply-list.h"
#include "ply-bitarray.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <values.h>

#include "script.h"
#include "script-object.h"

void script_obj_reset (script_obj_t *obj);

void script_obj_free (script_obj_t *obj)
{
  assert (!obj->refcount);
  script_obj_reset (obj);
  free (obj);
}

void script_obj_ref (script_obj_t *obj)
{
  obj->refcount++;
}

void script_obj_unref (script_obj_t *obj)
{
  if (!obj) return;
  assert (obj->refcount > 0);
  obj->refcount--;
  if (obj->refcount <= 0)
    script_obj_free (obj);
}

static void foreach_free_variable (void *key,
                                   void *data,
                                   void *user_data)
{
  script_variable_t *variable = data;

  script_obj_unref (variable->object);
  free (variable->name);
  free (variable);
}

void script_obj_reset (script_obj_t *obj)
{
  switch (obj->type)
    {
      case SCRIPT_OBJ_TYPE_REF:
        script_obj_unref (obj->data.obj);
        break;

      case SCRIPT_OBJ_TYPE_INT:
        break;

      case SCRIPT_OBJ_TYPE_FLOAT:
        break;

      case SCRIPT_OBJ_TYPE_STRING:
        free (obj->data.string);
        break;

      case SCRIPT_OBJ_TYPE_HASH:                /* FIXME nightmare */
        ply_hashtable_foreach (obj->data.hash, foreach_free_variable, NULL);
        ply_hashtable_free (obj->data.hash);
        break;

      case SCRIPT_OBJ_TYPE_FUNCTION:
        {
          if (obj->data.function->freeable)
            {
              ply_list_node_t *node;
              for (node =
                     ply_list_get_first_node (obj->data.function->parameters);
                   node;
                   node =
                     ply_list_get_next_node (obj->data.function->parameters,
                                             node))
                {
                  char *operand = ply_list_node_get_data (node);
                  free (operand);
                }
              ply_list_free (obj->data.function->parameters);
              free (obj->data.function);
            }
        }
        break;

      case SCRIPT_OBJ_TYPE_NATIVE:
        if (obj->data.native.class->free_func)
          obj->data.native.class->free_func (obj);
        break;

      case SCRIPT_OBJ_TYPE_NULL:
        break;
    }
  obj->type = SCRIPT_OBJ_TYPE_NULL;
}

script_obj_t *script_obj_deref_direct (script_obj_t *obj)
{
  while (obj->type == SCRIPT_OBJ_TYPE_REF)
    obj = obj->data.obj;
  return obj;
}

void script_obj_deref (script_obj_t **obj_ptr)
{
  script_obj_t *obj = *obj_ptr;

  obj = script_obj_deref_direct (obj);
  script_obj_ref (obj);
  script_obj_unref (*obj_ptr);
  *obj_ptr = obj;
}

script_obj_t *script_obj_new_null (void)
{
  script_obj_t *obj = malloc (sizeof (script_obj_t));

  obj->type = SCRIPT_OBJ_TYPE_NULL;
  obj->refcount = 1;
  return obj;
}

script_obj_t *script_obj_new_int (int number)
{
  script_obj_t *obj = malloc (sizeof (script_obj_t));

  obj->type = SCRIPT_OBJ_TYPE_INT;
  obj->refcount = 1;
  obj->data.integer = number;
  return obj;
}

script_obj_t *script_obj_new_float (float number)
{
  if (isnan (number)) return script_obj_new_null ();
  script_obj_t *obj = malloc (sizeof (script_obj_t));
  obj->type = SCRIPT_OBJ_TYPE_FLOAT;
  obj->refcount = 1;
  obj->data.floatpoint = number;
  return obj;
}

script_obj_t *script_obj_new_string (const char *string)
{
  if (!string) return script_obj_new_null ();
  script_obj_t *obj = malloc (sizeof (script_obj_t));
  obj->type = SCRIPT_OBJ_TYPE_STRING;
  obj->refcount = 1;
  obj->data.string = strdup (string);
  return obj;
}

script_obj_t *script_obj_new_hash (void)
{
  script_obj_t *obj = malloc (sizeof (script_obj_t));

  obj->type = SCRIPT_OBJ_TYPE_HASH;
  obj->data.hash = ply_hashtable_new (ply_hashtable_string_hash,
                                      ply_hashtable_string_compare);
  obj->refcount = 1;
  return obj;
}

script_obj_t *script_obj_new_function (script_function_t *function)
{
  script_obj_t *obj = malloc (sizeof (script_obj_t));

  obj->type = SCRIPT_OBJ_TYPE_FUNCTION;
  obj->data.function = function;
  obj->refcount = 1;
  return obj;
}

script_obj_t *script_obj_new_ref (script_obj_t *sub_obj)
{
  script_obj_t *obj = malloc (sizeof (script_obj_t));

  obj->type = SCRIPT_OBJ_TYPE_REF;
  obj->data.obj = sub_obj;
  obj->refcount = 1;
  return obj;
}

script_obj_t *script_obj_new_native (void                      *object_data,
                                     script_obj_native_class_t *class)
{
  if (!object_data) return script_obj_new_null ();
  script_obj_t *obj = malloc (sizeof (script_obj_t));
  obj->type = SCRIPT_OBJ_TYPE_NATIVE;
  obj->data.native.class = class;
  obj->data.native.object_data = object_data;
  obj->refcount = 1;
  return obj;
}

int script_obj_as_int (script_obj_t *obj)
{                                                     /* If in then reply contents, otherwise reply 0 */
  obj = script_obj_deref_direct (obj);
  switch (obj->type)
    {
      case SCRIPT_OBJ_TYPE_INT:
        return obj->data.integer;

      case SCRIPT_OBJ_TYPE_FLOAT:
        return (int) obj->data.floatpoint;

      case SCRIPT_OBJ_TYPE_NULL:
        return 0;

      case SCRIPT_OBJ_TYPE_REF:
      case SCRIPT_OBJ_TYPE_HASH:
      case SCRIPT_OBJ_TYPE_FUNCTION:
      case SCRIPT_OBJ_TYPE_NATIVE:
        return 0;

      case SCRIPT_OBJ_TYPE_STRING:
        return 0;
    }
  return 0;
}

float script_obj_as_float (script_obj_t *obj)
{                                                     /* If in then reply contents, otherwise reply 0 */
  obj = script_obj_deref_direct (obj);
  switch (obj->type)
    {
      case SCRIPT_OBJ_TYPE_INT:
        return (float) obj->data.integer;

      case SCRIPT_OBJ_TYPE_FLOAT:
        return obj->data.floatpoint;

      case SCRIPT_OBJ_TYPE_NULL:
        return NAN;

      case SCRIPT_OBJ_TYPE_REF:
      case SCRIPT_OBJ_TYPE_HASH:
      case SCRIPT_OBJ_TYPE_FUNCTION:
      case SCRIPT_OBJ_TYPE_NATIVE:
        return NAN;

      case SCRIPT_OBJ_TYPE_STRING:
        return NAN;
    }
  return NAN;
}

bool script_obj_as_bool (script_obj_t *obj)
{                                                 /* False objects are NULL, 0, "" */
  obj = script_obj_deref_direct (obj);
  switch (obj->type)
    {
      case SCRIPT_OBJ_TYPE_INT:
        if (obj->data.integer) return true;
        return false;

      case SCRIPT_OBJ_TYPE_FLOAT:
        if (fabs (obj->data.floatpoint) > FLT_MIN) return true;
        return false;

      case SCRIPT_OBJ_TYPE_NULL:
        return false;

      case SCRIPT_OBJ_TYPE_REF:
      case SCRIPT_OBJ_TYPE_HASH:
      case SCRIPT_OBJ_TYPE_FUNCTION:
      case SCRIPT_OBJ_TYPE_NATIVE:
        return true;

      case SCRIPT_OBJ_TYPE_STRING:
        if (*obj->data.string) return true;
        return false;
    }
  return false;
}

char *script_obj_as_string (script_obj_t *obj)              /* reply is strdupped and may be NULL */
{
  obj = script_obj_deref_direct (obj);
  char *reply;

  switch (obj->type)
    {
      case SCRIPT_OBJ_TYPE_INT:
        asprintf (&reply, "%d", obj->data.integer);
        return reply;

      case SCRIPT_OBJ_TYPE_FLOAT:
        asprintf (&reply, "%f", obj->data.floatpoint);
        return reply;

      case SCRIPT_OBJ_TYPE_NULL:
        return NULL;

      case SCRIPT_OBJ_TYPE_REF:
      case SCRIPT_OBJ_TYPE_HASH:
      case SCRIPT_OBJ_TYPE_FUNCTION:
      case SCRIPT_OBJ_TYPE_NATIVE:
        return NULL;

      case SCRIPT_OBJ_TYPE_STRING:
        return strdup (obj->data.string);
    }

  return NULL;
}

script_function_t *script_obj_as_function (script_obj_t *obj)
{
  obj = script_obj_deref_direct (obj);
  if (obj->type == SCRIPT_OBJ_TYPE_FUNCTION)
    return obj->data.function;
  
  return NULL;
}

void *script_obj_as_native_of_class (script_obj_t              *obj,
                                     script_obj_native_class_t *class)
{
  obj = script_obj_deref_direct (obj);
  if (script_obj_is_native_of_class (obj, class))
    return obj->data.native.object_data;
  return NULL;
}

void *script_obj_as_native_of_class_name (script_obj_t *obj,
                                          const char *class_name)
{
  obj = script_obj_deref_direct (obj);
  if (script_obj_is_native_of_class_name (obj, class_name))
    return obj->data.native.object_data;
  return NULL;
}

bool script_obj_is_null (script_obj_t *obj)
{
  obj = script_obj_deref_direct (obj);
  return obj->type == SCRIPT_OBJ_TYPE_NULL;
}

bool script_obj_is_int (script_obj_t *obj)
{
  obj = script_obj_deref_direct (obj);
  return obj->type == SCRIPT_OBJ_TYPE_INT;
}

bool script_obj_is_float (script_obj_t *obj)
{
  obj = script_obj_deref_direct (obj);
  return obj->type == SCRIPT_OBJ_TYPE_FLOAT;
}

bool script_obj_is_number (script_obj_t *obj)     /* Float or Int */
{
  obj = script_obj_deref_direct (obj);
  return obj->type == SCRIPT_OBJ_TYPE_INT || obj->type == SCRIPT_OBJ_TYPE_FLOAT;
}

bool script_obj_is_string (script_obj_t *obj)
{
  obj = script_obj_deref_direct (obj);
  return obj->type == SCRIPT_OBJ_TYPE_STRING;
}

bool script_obj_is_hash (script_obj_t *obj)
{
  obj = script_obj_deref_direct (obj);
  return obj->type == SCRIPT_OBJ_TYPE_HASH;
}

bool script_obj_is_function (script_obj_t *obj)
{
  obj = script_obj_deref_direct (obj);
  return obj->type == SCRIPT_OBJ_TYPE_FUNCTION;
}

bool script_obj_is_native (script_obj_t *obj)
{
  obj = script_obj_deref_direct (obj);
  return obj->type == SCRIPT_OBJ_TYPE_NATIVE;
}

bool script_obj_is_native_of_class (script_obj_t              *obj,
                                    script_obj_native_class_t *class)
{
  obj = script_obj_deref_direct (obj);
  return obj->type == SCRIPT_OBJ_TYPE_NATIVE && obj->data.native.class == class;
}

bool script_obj_is_native_of_class_name (script_obj_t *obj,
                                         const char   *class_name)
{
  obj = script_obj_deref_direct (obj);
  return obj->type == SCRIPT_OBJ_TYPE_NATIVE && !strcmp (
           obj->data.native.class->name,
           class_name);
}

void script_obj_assign (script_obj_t *obj_a,
                        script_obj_t *obj_b)
{
  obj_b = script_obj_deref_direct (obj_b);
  if (obj_a == obj_b) return;                   /* FIXME triple check this */
  script_obj_reset (obj_a);

  switch (obj_b->type)
    {
      case SCRIPT_OBJ_TYPE_NULL:
        obj_a->type = SCRIPT_OBJ_TYPE_NULL;
        break;

      case SCRIPT_OBJ_TYPE_INT:
        obj_a->type = SCRIPT_OBJ_TYPE_INT;
        obj_a->data.integer = obj_b->data.integer;
        break;

      case SCRIPT_OBJ_TYPE_FLOAT:
        obj_a->type = SCRIPT_OBJ_TYPE_FLOAT;
        obj_a->data.floatpoint = obj_b->data.floatpoint;
        break;

      case SCRIPT_OBJ_TYPE_STRING:
        obj_a->type = SCRIPT_OBJ_TYPE_STRING;
        obj_a->data.string = strdup (obj_b->data.string);
        break;

      case SCRIPT_OBJ_TYPE_REF:
        break;
      case SCRIPT_OBJ_TYPE_HASH:
      case SCRIPT_OBJ_TYPE_FUNCTION:
      case SCRIPT_OBJ_TYPE_NATIVE:
        obj_a->type = SCRIPT_OBJ_TYPE_REF;
        obj_a->data.obj = obj_b;
        script_obj_ref (obj_b);
        break;
    }
}

script_obj_t *script_obj_hash_get_element (script_obj_t *hash,
                                           const char   *name)
{
  hash = script_obj_deref_direct (hash);
  assert (hash->type == SCRIPT_OBJ_TYPE_HASH);
  script_variable_t *variable = ply_hashtable_lookup (hash->data.hash,
                                                      (void *) name);
  script_obj_t *obj;

  if (variable)
    obj = variable->object;
  else
    {
      obj = script_obj_new_null ();
      variable = malloc (sizeof (script_variable_t));
      variable->name = strdup (name);
      variable->object = obj;
      ply_hashtable_insert (hash->data.hash, variable->name, variable);
    }
  script_obj_ref (obj);
  return obj;
}

int script_obj_hash_get_int (script_obj_t *hash,
                             const char   *name)
{
  script_obj_t *obj = script_obj_hash_get_element (hash, name);
  int reply = script_obj_as_int (obj);

  script_obj_unref (obj);
  return reply;
}

float script_obj_hash_get_float (script_obj_t *hash,
                                 const char   *name)
{
  script_obj_t *obj = script_obj_hash_get_element (hash, name);
  float reply = script_obj_as_float (obj);

  script_obj_unref (obj);
  return reply;
}

bool script_obj_hash_get_bool (script_obj_t *hash,
                               const char   *name)
{
  script_obj_t *obj = script_obj_hash_get_element (hash, name);
  bool reply = script_obj_as_bool (obj);

  script_obj_unref (obj);
  return reply;
}

char *script_obj_hash_get_string (script_obj_t *hash,
                                  const char   *name)
{
  script_obj_t *obj = script_obj_hash_get_element (hash, name);
  char *reply = script_obj_as_string (obj);

  script_obj_unref (obj);
  return reply;
}

script_function_t *script_obj_hash_get_function (script_obj_t *hash,
                                                 const char   *name)
{
  script_obj_t *obj = script_obj_hash_get_element (hash, name);
  script_function_t *function = script_obj_as_function (obj);

  script_obj_unref (obj);
  return function;
}

void *script_obj_hash_get_native_of_class (script_obj_t              *hash,
                                           const char                *name,
                                           script_obj_native_class_t *class)
{
  script_obj_t *obj = script_obj_hash_get_element (hash, name);
  void *reply = script_obj_as_native_of_class (obj, class);

  script_obj_unref (obj);
  return reply;
}

void *script_obj_hash_get_native_of_class_name (script_obj_t *hash,
                                                const char   *name,
                                                const char   *class_name)
{
  script_obj_t *obj = script_obj_hash_get_element (hash, name);
  void *reply = script_obj_as_native_of_class_name (obj, class_name);

  script_obj_unref (obj);
  return reply;
}

void script_obj_hash_add_element (script_obj_t *hash,
                                  script_obj_t *element,
                                  const char   *name)
{
  assert (hash->type == SCRIPT_OBJ_TYPE_HASH);
  script_obj_t *obj = script_obj_hash_get_element (hash, name);
  script_obj_assign (obj, element);
  script_obj_unref (obj);
}

script_obj_t *script_obj_plus (script_obj_t *script_obj_a,
                               script_obj_t *script_obj_b)
{
  if (script_obj_is_string (script_obj_a) || script_obj_is_string (script_obj_b))
    {
      script_obj_t *obj;
      char *string_a = script_obj_as_string (script_obj_a);
      char *string_b = script_obj_as_string (script_obj_b);
      if (string_a && string_b)
        {
          char *newstring;
          asprintf (&newstring, "%s%s", string_a, string_b);
          obj = script_obj_new_string (newstring);
          free (newstring);
        }
      else
        obj = script_obj_new_null ();
      free (string_a);
      free (string_b);
      return obj;
    }
  if (script_obj_is_number (script_obj_a) && script_obj_is_number (script_obj_b))
    {
      if (script_obj_is_int (script_obj_a) && script_obj_is_int (script_obj_b))
        {
          int value = script_obj_as_int (script_obj_a) + script_obj_as_int (script_obj_b);
          return script_obj_new_int (value);
        }
      float value = script_obj_as_float (script_obj_a) + script_obj_as_float (script_obj_b);
      return script_obj_new_float (value);
    }
  return script_obj_new_null ();
}

script_obj_t *script_obj_minus (script_obj_t *script_obj_a,
                                script_obj_t *script_obj_b)
{
  if (script_obj_is_number (script_obj_a) && script_obj_is_number (script_obj_b))
    {
      if (script_obj_is_int (script_obj_a) && script_obj_is_int (script_obj_b))
        {
          int value = script_obj_as_int (script_obj_a) - script_obj_as_int (script_obj_b);
          return script_obj_new_int (value);
        }
      float value = script_obj_as_float (script_obj_a) - script_obj_as_float (script_obj_b);
      return script_obj_new_float (value);
    }
  return script_obj_new_null ();
}

script_obj_t *script_obj_mul (script_obj_t *script_obj_a,
                              script_obj_t *script_obj_b)
{
  if (script_obj_is_number (script_obj_a) && script_obj_is_number (script_obj_b))
    {
      if (script_obj_is_int (script_obj_a) && script_obj_is_int (script_obj_b))
        {
          int value = script_obj_as_int (script_obj_a) * script_obj_as_int (script_obj_b);
          return script_obj_new_int (value);
        }
      float value = script_obj_as_float (script_obj_a) * script_obj_as_float (script_obj_b);
      return script_obj_new_float (value);
    }
  return script_obj_new_null ();
}

script_obj_t *script_obj_div (script_obj_t *script_obj_a,
                              script_obj_t *script_obj_b)
{
  if (script_obj_is_number (script_obj_a) && script_obj_is_number (script_obj_b))
    {
      if (script_obj_is_int (script_obj_a) && script_obj_is_int (script_obj_b))
        if (script_obj_as_int (script_obj_a) %
            script_obj_as_int (script_obj_b) == 0)
          {
            int value = script_obj_as_int (script_obj_a) / script_obj_as_int (script_obj_b);
            return script_obj_new_int (value);
          }
      float value = script_obj_as_float (script_obj_a) / script_obj_as_float (script_obj_b);
      return script_obj_new_float (value);
    }
  return script_obj_new_null ();
}

script_obj_t *script_obj_mod (script_obj_t *script_obj_a,
                              script_obj_t *script_obj_b)
{
  if (script_obj_is_number (script_obj_a) && script_obj_is_number (script_obj_b))
    {
      if (script_obj_is_int (script_obj_a) && script_obj_is_int (script_obj_b))
        {
          int value = script_obj_as_int (script_obj_a) % script_obj_as_int (script_obj_b);
          return script_obj_new_int (value);
        }
      float value = fmodf (script_obj_as_float (script_obj_a), script_obj_as_float (script_obj_b));
      return script_obj_new_float (value);
    }
  return script_obj_new_null ();
}


script_obj_cmp_result_t script_obj_cmp (script_obj_t *script_obj_a,
                                        script_obj_t *script_obj_b)
{
  if (script_obj_is_null (script_obj_a) && script_obj_is_null (script_obj_b))
    {
      return SCRIPT_OBJ_CMP_RESULT_EQ;
    }
  else if (script_obj_is_number (script_obj_a))
    {
      if (script_obj_is_number (script_obj_b))
        {
          float diff = script_obj_as_float (script_obj_a) - script_obj_as_float (script_obj_b);
          if (diff < 0) return SCRIPT_OBJ_CMP_RESULT_LT;
          if (diff > 0) return SCRIPT_OBJ_CMP_RESULT_GT;
          return SCRIPT_OBJ_CMP_RESULT_EQ;
        }
    }
  else if (script_obj_is_string (script_obj_a))
    {
      if (script_obj_is_string (script_obj_b))
        {
          char* string_a = script_obj_as_string (script_obj_a);
          char* string_b = script_obj_as_string (script_obj_b);
          int diff = strcmp (string_a, string_b);
          free(string_a);
          free(string_b);
          if (diff < 0) return SCRIPT_OBJ_CMP_RESULT_LT;
          if (diff > 0) return SCRIPT_OBJ_CMP_RESULT_GT;
          return SCRIPT_OBJ_CMP_RESULT_EQ;
        }
    }
  else if ((script_obj_is_hash (script_obj_a) && script_obj_is_function (script_obj_b)) ||
           (script_obj_is_function (script_obj_a) && script_obj_is_function (script_obj_b)) ||
           (script_obj_is_native (script_obj_a) && script_obj_is_native (script_obj_b)))
    {
      if (script_obj_deref_direct (script_obj_a) == script_obj_deref_direct (script_obj_b))
        return SCRIPT_OBJ_CMP_RESULT_EQ;
    }
  return SCRIPT_OBJ_CMP_RESULT_NE;
}

