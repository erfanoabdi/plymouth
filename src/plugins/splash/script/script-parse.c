/* script-parse.c - parser for reading in script files
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
#include "ply-logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <stdbool.h>

#include "script.h"
#include "script-scan.h"
#include "script-parse.h"

#define WITH_SEMIES

typedef struct
{
 const char        *symbol;
 script_exp_type_t  exp_type; 
 int                presedence;
}script_parse_operator_table_entry_t;

static script_op_t *script_parse_op (script_scan_t *scan);
static script_exp_t *script_parse_exp (script_scan_t *scan);
static ply_list_t *script_parse_op_list (script_scan_t *scan);
static void script_parse_op_list_free (ply_list_t *op_list);

static script_exp_t *script_parse_new_exp_single (script_exp_type_t  type,
                                                  script_exp_t      *sub)
{
  script_exp_t *exp = malloc (sizeof (script_exp_t));
  exp->type = type;
  exp->data.sub = sub;
  return exp;
}

static script_exp_t *script_parse_new_exp_dual (script_exp_type_t  type,
                                                script_exp_t      *sub_a,
                                                script_exp_t      *sub_b)
{
  script_exp_t *exp = malloc (sizeof (script_exp_t));
  exp->type = type;
  exp->data.dual.sub_a = sub_a;
  exp->data.dual.sub_b = sub_b;
  return exp;
}

static void script_parse_error (script_scan_token_t *token,
                                const char       *expected)
{
  ply_error ("Parser error L:%d C:%d : %s\n",
             token->line_index,
             token->column_index,
             expected);
}

static const script_parse_operator_table_entry_t*   /* Only allows 1 or 2 character symbols */
script_parse_operator_table_entry_lookup (script_scan_t                                *scan,
                                          const script_parse_operator_table_entry_t *table)
{
  int entry_index;
  script_scan_token_t *curtoken = script_scan_get_current_token (scan);
  script_scan_token_t *peektoken = script_scan_peek_next_token (scan);
  for (entry_index = 0; table[entry_index].symbol; entry_index++)
    {
      if (!script_scan_token_is_symbol (curtoken)) continue;
      if (curtoken->data.symbol != table[entry_index].symbol[0]) continue;
      if (table[entry_index].symbol[1])
        {
          if (!script_scan_token_is_symbol (peektoken)) continue;
          if (peektoken->data.symbol != table[entry_index].symbol[1]) continue;
          if (peektoken->whitespace) continue;
        }
      break;
    }
  return &table[entry_index];
}

static void script_parse_advance_scan_by_string (script_scan_t *scan,
                                                 const char *string)
{
  while (*string)
    {
      script_scan_get_next_token(scan);
      string++;
    }
}

static script_function_t *script_parse_function_def (script_scan_t *scan)
{
  script_scan_token_t *curtoken = script_scan_get_current_token (scan);
  ply_list_t *parameter_list;

  if (!script_scan_token_is_symbol_of_value (curtoken, '('))
    {
      script_parse_error (curtoken,
        "Function declaration requires parameters to be declared within '(' brackets");
      return NULL;
    }
  curtoken = script_scan_get_next_token (scan);
  parameter_list = ply_list_new ();

  while (true)
    {
      if (script_scan_token_is_symbol_of_value (curtoken, ')')) break;
      if (!script_scan_token_is_identifier (curtoken))
        {
          script_parse_error (curtoken,
            "Function declaration parameters must be valid identifiers");
          return NULL;
        }
      char *parameter = strdup (curtoken->data.string);
      ply_list_append_data (parameter_list, parameter);

      curtoken = script_scan_get_next_token (scan);

      if (script_scan_token_is_symbol_of_value (curtoken, ')')) break;
      if (!script_scan_token_is_symbol_of_value (curtoken, ','))
        {
          script_parse_error (curtoken,
            "Function declaration parameters must separated with ',' and terminated with a ')'");
          return NULL;
        }
      curtoken = script_scan_get_next_token (scan);
    }

  curtoken = script_scan_get_next_token (scan);

  script_op_t *func_op = script_parse_op (scan);
  
  script_function_t *function = script_function_script_new (func_op,
                                                            NULL,
                                                            parameter_list);
  return function;
}

static script_exp_t *script_parse_exp_tm (script_scan_t *scan)
{
  script_scan_token_t *curtoken = script_scan_get_current_token (scan);
  script_exp_t *exp = NULL;

  if (script_scan_token_is_integer (curtoken))
    {
      exp = malloc (sizeof (script_exp_t));
      exp->type = SCRIPT_EXP_TYPE_TERM_INT;
      exp->data.integer = curtoken->data.integer;
      script_scan_get_next_token (scan);
      return exp;
    }
  if (script_scan_token_is_float (curtoken))
    {
      exp = malloc (sizeof (script_exp_t));
      exp->type = SCRIPT_EXP_TYPE_TERM_FLOAT;
      exp->data.floatpoint = curtoken->data.floatpoint;
      script_scan_get_next_token (scan);
      return exp;
    }
  if (script_scan_token_is_identifier (curtoken))
    {
      exp = malloc (sizeof (script_exp_t));
      if (script_scan_token_is_identifier_of_value (curtoken, "NULL"))
        exp->type = SCRIPT_EXP_TYPE_TERM_NULL;
      else if (script_scan_token_is_identifier_of_value (curtoken, "global"))
        exp->type = SCRIPT_EXP_TYPE_TERM_GLOBAL;
      else if (script_scan_token_is_identifier_of_value (curtoken, "local"))
        exp->type = SCRIPT_EXP_TYPE_TERM_LOCAL;
      else if (script_scan_token_is_identifier_of_value (curtoken, "fun"))
        {
          exp->type = SCRIPT_EXP_TYPE_FUNCTION_DEF;
          script_scan_get_next_token (scan);
          exp->data.function_def = script_parse_function_def (scan);
          return exp;
        }
      else
        {
          exp->type = SCRIPT_EXP_TYPE_TERM_VAR;
          exp->data.string = strdup (curtoken->data.string);
        }
      curtoken = script_scan_get_next_token (scan);
      return exp;
    }
  if (script_scan_token_is_string (curtoken))
    {
      exp = malloc (sizeof (script_exp_t));
      exp->type = SCRIPT_EXP_TYPE_TERM_STRING;
      exp->data.string = strdup (curtoken->data.string);
      script_scan_get_next_token (scan);
      return exp;
    }
  if (script_scan_token_is_symbol_of_value (curtoken, '('))
    {
      script_scan_get_next_token (scan);
      exp = script_parse_exp (scan);
      curtoken = script_scan_get_current_token (scan);
      if (!exp)
        {
          script_parse_error (curtoken,
            "Expected valid contents of bracketed expression");
          return NULL;
        }
      if (!script_scan_token_is_symbol_of_value (curtoken, ')'))
        {
          script_parse_error (curtoken,
            "Expected bracketed block to be terminated with a ')'");
          return NULL;
        }
      script_scan_get_next_token (scan);
      return exp;
    }
  return NULL;
}

static script_exp_t *script_parse_exp_pi (script_scan_t *scan)
{
  script_exp_t *exp = script_parse_exp_tm (scan);
  script_scan_token_t *curtoken = script_scan_get_current_token (scan);

  while (true)
    {
      if (!script_scan_token_is_symbol (curtoken)) break;
      if (script_scan_token_is_symbol_of_value (curtoken, '('))
        {
          script_exp_t *func = malloc (sizeof (script_exp_t));
          ply_list_t *parameters = ply_list_new ();
          script_scan_get_next_token (scan);
          while (true)
            {
              if (script_scan_token_is_symbol_of_value (curtoken, ')')) break;
              script_exp_t *parameter = script_parse_exp (scan);

              ply_list_append_data (parameters, parameter);

              curtoken = script_scan_get_current_token (scan);
              if (script_scan_token_is_symbol_of_value (curtoken, ')')) break;
              if (!script_scan_token_is_symbol_of_value (curtoken, ','))
                {
                  script_parse_error (curtoken,
                    "Function parameters should be separated with a ',' and terminated with a ')'");
                  return NULL;
                }
              curtoken = script_scan_get_next_token (scan);
            }
          script_scan_get_next_token (scan);
          func->data.function_exe.name = exp;
          exp = func;
          exp->type = SCRIPT_EXP_TYPE_FUNCTION_EXE;
          exp->data.function_exe.parameters = parameters;
          continue;
        }
      script_exp_t *key;

      if (script_scan_token_is_symbol_of_value (curtoken, '.'))
        {
          script_scan_get_next_token (scan);
          if (script_scan_token_is_identifier (curtoken))
            {
              key = malloc (sizeof (script_exp_t));
              key->type = SCRIPT_EXP_TYPE_TERM_STRING;
              key->data.string = strdup (curtoken->data.string);
            }
          else if (script_scan_token_is_integer (curtoken))       /* errrr, integer keys without being [] bracketed */
            {
              key = malloc (sizeof (script_exp_t));                       /* This is broken with floats as obj.10.6 is obj[10.6] and not obj[10][6] */
              key->type = SCRIPT_EXP_TYPE_TERM_INT;
              key->data.integer = curtoken->data.integer;
            }
          else
            {
              script_parse_error (curtoken,
                "A dot based hash index must be an identifier (or a integer)");
              return NULL;
            }
          curtoken = script_scan_get_next_token (scan);
        }
      else if (script_scan_token_is_symbol_of_value (curtoken, '['))
        {
          script_scan_get_next_token (scan);
          key = script_parse_exp (scan);
          curtoken = script_scan_get_current_token (scan);
          if (!script_scan_token_is_symbol_of_value (curtoken, ']'))
            {
              script_parse_error (curtoken,
                "Expected a ']' to terminate the index expression");
              return NULL;
            }
          curtoken = script_scan_get_next_token (scan);
        }
      else break;
      exp = script_parse_new_exp_dual (SCRIPT_EXP_TYPE_HASH, exp, key);
    }
  return exp;
}

static script_exp_t *script_parse_exp_pr (script_scan_t *scan)
{
  static const script_parse_operator_table_entry_t operator_table[] =
    {
      {"++", SCRIPT_EXP_TYPE_PRE_INC,    0},
      {"+",  SCRIPT_EXP_TYPE_POS,        0},
      {"--", SCRIPT_EXP_TYPE_PRE_DEC,    0},
      {"-",  SCRIPT_EXP_TYPE_NEG,        0},
      {"!",  SCRIPT_EXP_TYPE_NOT,        0},
      {NULL, SCRIPT_EXP_TYPE_TERM_NULL, -1},
    };
  
  const script_parse_operator_table_entry_t* entry;
  entry =  script_parse_operator_table_entry_lookup(scan, operator_table);
  if (entry->presedence < 0) return script_parse_exp_pi (scan);
  script_parse_advance_scan_by_string(scan, entry->symbol);
  return script_parse_new_exp_single(entry->exp_type, script_parse_exp_pr (scan));
}

static script_exp_t *script_parse_exp_po (script_scan_t *scan)
{
  static const script_parse_operator_table_entry_t operator_table[] =
    {
      {"++", SCRIPT_EXP_TYPE_POST_INC,   0},
      {"--", SCRIPT_EXP_TYPE_POST_DEC,   0},
      {NULL, SCRIPT_EXP_TYPE_TERM_NULL, -1},
    };
  script_exp_t *exp = script_parse_exp_pr (scan);

  while (true)
    {
      const script_parse_operator_table_entry_t* entry;
      entry =  script_parse_operator_table_entry_lookup(scan, operator_table);
      if (entry->presedence < 0) break;
      script_parse_advance_scan_by_string(scan, entry->symbol);
      exp = script_parse_new_exp_single(entry->exp_type, exp);
    }
  return exp;
}

static script_exp_t *script_parse_exp_ltr (script_scan_t *scan, int presedence)
{
  static const script_parse_operator_table_entry_t operator_table[] =
    {
      {"||", SCRIPT_EXP_TYPE_OR,         0},    /* FIXME Does const imply static? */
      {"&&", SCRIPT_EXP_TYPE_AND,        1},
      {"==", SCRIPT_EXP_TYPE_EQ,         2},
      {"!=", SCRIPT_EXP_TYPE_NE,         2},
      {">=", SCRIPT_EXP_TYPE_GE,         3},
      {"<=", SCRIPT_EXP_TYPE_LE,         3},
      {"+=", SCRIPT_EXP_TYPE_TERM_NULL, -1},    /* A few things it shouldn't consume */
      {"-=", SCRIPT_EXP_TYPE_TERM_NULL, -1},
      {"*=", SCRIPT_EXP_TYPE_TERM_NULL, -1},
      {"/=", SCRIPT_EXP_TYPE_TERM_NULL, -1},
      {"%=", SCRIPT_EXP_TYPE_TERM_NULL, -1},
      {">",  SCRIPT_EXP_TYPE_GT,         3},
      {"<",  SCRIPT_EXP_TYPE_LT,         3},
      {"+",  SCRIPT_EXP_TYPE_PLUS,       4},
      {"-",  SCRIPT_EXP_TYPE_MINUS,      4},
      {"*",  SCRIPT_EXP_TYPE_MUL,        5},
      {"/",  SCRIPT_EXP_TYPE_DIV,        5},
      {"%",  SCRIPT_EXP_TYPE_MOD,        5},
      {NULL, SCRIPT_EXP_TYPE_TERM_NULL, -1},
    };
    
  if (presedence > 5) return script_parse_exp_po (scan);
  script_exp_t *exp = script_parse_exp_ltr (scan, presedence + 1);
  if (!exp) return NULL;
  
  while (true)
    {
      const script_parse_operator_table_entry_t* entry;
      entry =  script_parse_operator_table_entry_lookup(scan, operator_table);
      if (entry->presedence != presedence) break;
      script_parse_advance_scan_by_string(scan, entry->symbol);
      exp = script_parse_new_exp_dual(entry->exp_type, exp, script_parse_exp_ltr (scan, presedence + 1));
      if (!exp->data.dual.sub_b)
        {
          script_parse_error (script_scan_get_current_token (scan),
                              "An invalid RHS of an expression");
          return NULL;
        }
    }
  return exp;
}

static script_exp_t *script_parse_exp_as (script_scan_t *scan)
{
  static const script_parse_operator_table_entry_t operator_table[] =
    {
      {"+=", SCRIPT_EXP_TYPE_ASSIGN_PLUS,  0},
      {"-=", SCRIPT_EXP_TYPE_ASSIGN_MINUS, 0}, 
      {"*=", SCRIPT_EXP_TYPE_ASSIGN_MUL,   0}, 
      {"/=", SCRIPT_EXP_TYPE_ASSIGN_DIV,   0}, 
      {"%=", SCRIPT_EXP_TYPE_ASSIGN_MOD,   0}, 
      {"=",  SCRIPT_EXP_TYPE_ASSIGN,       0}, 
      {NULL, SCRIPT_EXP_TYPE_TERM_NULL,   -1}, 
    };
  script_exp_t *lhs = script_parse_exp_ltr (scan, 0);
  if (!lhs) return NULL;

  const script_parse_operator_table_entry_t* entry;
  entry =  script_parse_operator_table_entry_lookup(scan, operator_table);
  if (entry->presedence < 0) return lhs;
  script_parse_advance_scan_by_string(scan, entry->symbol);
  script_exp_t *rhs = script_parse_exp_as (scan);
  if (!rhs)
    {
      script_parse_error (script_scan_get_current_token (scan),
                          "An invalid RHS of an expression");
      return NULL;
    }
  return script_parse_new_exp_dual (entry->exp_type, lhs, rhs);
}

static script_exp_t *script_parse_exp (script_scan_t *scan)
{
  return script_parse_exp_as (scan);
}

static script_op_t *script_parse_op_block (script_scan_t *scan)
{
  script_scan_token_t *curtoken = script_scan_get_current_token (scan);

  if (!script_scan_token_is_symbol_of_value (curtoken, '{'))
    return NULL;
  script_scan_get_next_token (scan);
  ply_list_t *sublist = script_parse_op_list (scan);
  if (!script_scan_token_is_symbol_of_value (curtoken, '}'))
    {
      script_parse_error (script_scan_get_current_token (scan),
                          "Expected a '}' to terminate the operation block");
      return NULL;
    }
  curtoken = script_scan_get_next_token (scan);

  script_op_t *op = malloc (sizeof (script_op_t));
  op->type = SCRIPT_OP_TYPE_OP_BLOCK;
  op->data.list = sublist;
  return op;
}

static script_op_t *script_parse_if_while (script_scan_t *scan)
{
  script_scan_token_t *curtoken = script_scan_get_current_token (scan);
  script_op_type_t type;

  if       (script_scan_token_is_identifier_of_value (curtoken, "if"))
    type = SCRIPT_OP_TYPE_IF;
  else if  (script_scan_token_is_identifier_of_value (curtoken, "while"))
    type = SCRIPT_OP_TYPE_WHILE;
  else return NULL;
  curtoken = script_scan_get_next_token (scan);
  if (!script_scan_token_is_symbol_of_value (curtoken, '('))
    {
      script_parse_error (curtoken,
                          "Expected a '(' at the start of a condition block");
      return NULL;
    }
  curtoken = script_scan_get_next_token (scan);

  script_exp_t *cond = script_parse_exp (scan);
  curtoken = script_scan_get_current_token (scan);
  if (!cond)
    {
      script_parse_error (curtoken, "Expected a valid condition expression");
      return NULL;
    }
  if (!script_scan_token_is_symbol_of_value (curtoken, ')'))
    {
      script_parse_error (curtoken,
                          "Expected a ')' at the end of a condition block");
      return NULL;
    }
  script_scan_get_next_token (scan);
  script_op_t *cond_op = script_parse_op (scan);
  script_op_t *else_op = NULL;

  curtoken = script_scan_get_current_token (scan);
  if ((type == SCRIPT_OP_TYPE_IF)
      && (script_scan_token_is_identifier_of_value (curtoken, "else")))
    {
      script_scan_get_next_token (scan);
      else_op = script_parse_op (scan);
    }
  script_op_t *op = malloc (sizeof (script_op_t));
  op->type = type;
  op->data.cond_op.cond = cond;
  op->data.cond_op.op1 = cond_op;
  op->data.cond_op.op2 = else_op;
  return op;
}

static script_op_t *script_parse_for (script_scan_t *scan)
{
  script_scan_token_t *curtoken = script_scan_get_current_token (scan);

  if (!script_scan_token_is_identifier_of_value (curtoken, "for")) return NULL;
  curtoken = script_scan_get_next_token (scan);
  if (!script_scan_token_is_symbol_of_value (curtoken, '('))
    {
      script_parse_error (curtoken,
                          "Expected a '(' at the start of a condition block");
      return NULL;
    }
  curtoken = script_scan_get_next_token (scan);

  script_exp_t *first = script_parse_exp (scan);
  if (!first)
    {
      script_parse_error (curtoken, "Expected a valid first expression");
      return NULL;
    }
  curtoken = script_scan_get_current_token (scan);
  if (!script_scan_token_is_symbol_of_value (curtoken, ';'))
    {
      script_parse_error (curtoken,
                          "Expected a ';' after the first 'for' expression");
      return NULL;
    }
  script_scan_get_next_token (scan);

  script_exp_t *cond = script_parse_exp (scan);
  if (!cond)
    {
      script_parse_error (curtoken, "Expected a valid condition expression");
      return NULL;
    }
  curtoken = script_scan_get_current_token (scan);
  if (!script_scan_token_is_symbol_of_value (curtoken, ';'))
    {
      script_parse_error (curtoken, "Expected a ';' after the 'for' condition");
      return NULL;
    }
  script_scan_get_next_token (scan);

  script_exp_t *last = script_parse_exp (scan);
  if (!last)
    {
      script_parse_error (curtoken, "Expected a valid last expression");
      return NULL;
    }
  curtoken = script_scan_get_current_token (scan);
  if (!script_scan_token_is_symbol_of_value (curtoken, ')'))
    {
      script_parse_error (curtoken, "Expected a ')' at the end of a for block");
      return NULL;
    }
  script_scan_get_next_token (scan);
  script_op_t *op_body = script_parse_op (scan);

  script_op_t *op_first = malloc (sizeof (script_op_t));
  op_first->type = SCRIPT_OP_TYPE_EXPRESSION;
  op_first->data.exp = first;

  script_op_t *op_last = malloc (sizeof (script_op_t));
  op_last->type = SCRIPT_OP_TYPE_EXPRESSION;
  op_last->data.exp = last;

  script_op_t *op_for = malloc (sizeof (script_op_t));
  op_for->type = SCRIPT_OP_TYPE_FOR;
  op_for->data.cond_op.cond = cond;
  op_for->data.cond_op.op1 = op_body;
  op_for->data.cond_op.op2 = op_last;

  script_op_t *op_block = malloc (sizeof (script_op_t));
  op_block->type = SCRIPT_OP_TYPE_OP_BLOCK;
  op_block->data.list = ply_list_new ();
  ply_list_append_data (op_block->data.list, op_first);
  ply_list_append_data (op_block->data.list, op_for);

  return op_block;
}

static script_op_t *script_parse_function (script_scan_t *scan)
{
  script_scan_token_t *curtoken = script_scan_get_current_token (scan);

  if (!script_scan_token_is_identifier_of_value (curtoken, "fun")) return NULL;
  curtoken = script_scan_get_next_token (scan);
  if (!script_scan_token_is_identifier (curtoken))
    {
      script_parse_error (curtoken,
                          "A function declaration requires a valid name");
      return NULL;
    }
  script_exp_t *name = malloc (sizeof (script_exp_t));    /* FIXME parse any type of exp as target and do an assign*/
  name->type = SCRIPT_EXP_TYPE_TERM_VAR;
  name->data.string = strdup (curtoken->data.string);

  curtoken = script_scan_get_next_token (scan);

  script_function_t *function = script_parse_function_def (scan);
  if (!function) return NULL;
  
  script_op_t *op = malloc (sizeof (script_op_t));
  op->type = SCRIPT_OP_TYPE_FUNCTION_DEF;
  op->data.function_def.name = name;
  op->data.function_def.function = function;
  return op;
}

static script_op_t *script_parse_return (script_scan_t *scan)
{
  script_scan_token_t *curtoken = script_scan_get_current_token (scan);
  script_op_type_t type;
  if      (script_scan_token_is_identifier_of_value (curtoken, "return"))
    type = SCRIPT_OP_TYPE_RETURN;
  else if (script_scan_token_is_identifier_of_value (curtoken, "break"))
    type = SCRIPT_OP_TYPE_BREAK;
  else if (script_scan_token_is_identifier_of_value (curtoken, "continue"))
    type = SCRIPT_OP_TYPE_CONTINUE;
  else return NULL;
  curtoken = script_scan_get_next_token (scan);

  script_op_t *op = malloc (sizeof (script_op_t));
  if (type == SCRIPT_OP_TYPE_RETURN)
    {
      op->data.exp = script_parse_exp (scan);                  /* May be NULL */
      curtoken = script_scan_get_current_token (scan);
    }
#ifdef WITH_SEMIES
  if (!script_scan_token_is_symbol_of_value (curtoken, ';'))
    {
      script_parse_error (curtoken, "Expected ';' after an expression");
      return NULL;
    }
  curtoken = script_scan_get_next_token (scan);
#endif

  op->type = type;
  return op;
}

static script_op_t *script_parse_op (script_scan_t *scan)
{
  script_scan_token_t *curtoken = script_scan_get_current_token (scan);
  script_op_t *reply = NULL;

  reply = script_parse_op_block (scan);
  if (reply) return reply;
  reply = script_parse_if_while (scan);
  if (reply) return reply;
  reply = script_parse_for (scan);
  if (reply) return reply;
  reply = script_parse_return (scan);
  if (reply) return reply;
  reply = script_parse_function (scan);
  if (reply) return reply;
/* if curtoken->data.string == "if/for/while... */

/* default is expression */
  {
    script_exp_t *exp = script_parse_exp (scan);
    if (!exp) return NULL;
    curtoken = script_scan_get_current_token (scan);
#ifdef WITH_SEMIES
    if (!script_scan_token_is_symbol_of_value (curtoken, ';'))
      {
        script_parse_error (curtoken, "Expected ';' after an expression");
        return NULL;
      }
    curtoken = script_scan_get_next_token (scan);
#endif

    script_op_t *op = malloc (sizeof (script_op_t));
    op->type = SCRIPT_OP_TYPE_EXPRESSION;
    op->data.exp = exp;
    return op;
  }
  return NULL;
}

static ply_list_t *script_parse_op_list (script_scan_t *scan)
{
  ply_list_t *op_list = ply_list_new ();

  while (true)
    {
      script_op_t *op = script_parse_op (scan);
      if (!op) break;
      ply_list_append_data (op_list, op);
    }

  return op_list;
}

static void script_parse_exp_free (script_exp_t *exp)
{
  if (!exp) return;
  switch (exp->type)
    {
      case SCRIPT_EXP_TYPE_PLUS:
      case SCRIPT_EXP_TYPE_MINUS:
      case SCRIPT_EXP_TYPE_MUL:
      case SCRIPT_EXP_TYPE_DIV:
      case SCRIPT_EXP_TYPE_MOD:
      case SCRIPT_EXP_TYPE_EQ:
      case SCRIPT_EXP_TYPE_NE:
      case SCRIPT_EXP_TYPE_GT:
      case SCRIPT_EXP_TYPE_GE:
      case SCRIPT_EXP_TYPE_LT:
      case SCRIPT_EXP_TYPE_LE:
      case SCRIPT_EXP_TYPE_AND:
      case SCRIPT_EXP_TYPE_OR:
      case SCRIPT_EXP_TYPE_ASSIGN:
      case SCRIPT_EXP_TYPE_ASSIGN_PLUS:
      case SCRIPT_EXP_TYPE_ASSIGN_MINUS:
      case SCRIPT_EXP_TYPE_ASSIGN_MUL:
      case SCRIPT_EXP_TYPE_ASSIGN_DIV:
      case SCRIPT_EXP_TYPE_ASSIGN_MOD:
      case SCRIPT_EXP_TYPE_HASH:
        script_parse_exp_free (exp->data.dual.sub_a);
        script_parse_exp_free (exp->data.dual.sub_b);
        break;

      case SCRIPT_EXP_TYPE_NOT:
      case SCRIPT_EXP_TYPE_POS:
      case SCRIPT_EXP_TYPE_NEG:
      case SCRIPT_EXP_TYPE_PRE_INC:
      case SCRIPT_EXP_TYPE_PRE_DEC:
      case SCRIPT_EXP_TYPE_POST_INC:
      case SCRIPT_EXP_TYPE_POST_DEC:
        script_parse_exp_free (exp->data.sub);
        break;

      case SCRIPT_EXP_TYPE_TERM_INT:
      case SCRIPT_EXP_TYPE_TERM_FLOAT:
      case SCRIPT_EXP_TYPE_TERM_NULL:
      case SCRIPT_EXP_TYPE_TERM_LOCAL:
      case SCRIPT_EXP_TYPE_TERM_GLOBAL:
        break;

      case SCRIPT_EXP_TYPE_FUNCTION_EXE:
        {
          ply_list_node_t *node;
          for (node = ply_list_get_first_node (exp->data.function_exe.parameters);
               node;
               node = ply_list_get_next_node (exp->data.function_exe.parameters, node))
            {
              script_exp_t *sub = ply_list_node_get_data (node);
              script_parse_exp_free (sub);
            }
          ply_list_free (exp->data.function_exe.parameters);
          script_parse_exp_free (exp->data.function_exe.name);
          break;
        }
      case SCRIPT_EXP_TYPE_FUNCTION_DEF:   /* FIXME merge the frees with one from op_free */
        {
          if (exp->data.function_def->type == SCRIPT_FUNCTION_TYPE_SCRIPT) 
            script_parse_op_free (exp->data.function_def->data.script);
          ply_list_node_t *node;
          for (node = ply_list_get_first_node (exp->data.function_def->parameters);
               node;
               node = ply_list_get_next_node (exp->data.function_def->parameters, node))
            {
              char *arg = ply_list_node_get_data (node);
              free (arg);
            }
          ply_list_free (exp->data.function_def->parameters);
          free (exp->data.function_def);
          break;
        }

      case SCRIPT_EXP_TYPE_TERM_STRING:
      case SCRIPT_EXP_TYPE_TERM_VAR:
        free (exp->data.string);
        break;
    }
  free (exp);
}

void script_parse_op_free (script_op_t *op)
{
  if (!op) return;
  switch (op->type)
    {
      case SCRIPT_OP_TYPE_EXPRESSION:
        {
          script_parse_exp_free (op->data.exp);
          break;
        }

      case SCRIPT_OP_TYPE_OP_BLOCK:
        {
          script_parse_op_list_free (op->data.list);
          break;
        }

      case SCRIPT_OP_TYPE_IF:
      case SCRIPT_OP_TYPE_WHILE:
      case SCRIPT_OP_TYPE_FOR:
        {
          script_parse_exp_free (op->data.cond_op.cond);
          script_parse_op_free  (op->data.cond_op.op1);
          script_parse_op_free  (op->data.cond_op.op2);
          break;
        }

      case SCRIPT_OP_TYPE_FUNCTION_DEF:
        {
          if (op->data.function_def.function->type == SCRIPT_FUNCTION_TYPE_SCRIPT)
            script_parse_op_free (op->data.function_def.function->data.script);
          ply_list_node_t *node;
          for (node = ply_list_get_first_node (op->data.function_def.function->parameters);
               node;
               node = ply_list_get_next_node (op->data.function_def.function->parameters, node))
            {
              char *arg = ply_list_node_get_data (node);
              free (arg);
            }
          ply_list_free (op->data.function_def.function->parameters);
          script_parse_exp_free (op->data.function_def.name);
          free (op->data.function_def.function);
          break;
        }

      case SCRIPT_OP_TYPE_RETURN:
        {
          if (op->data.exp) script_parse_exp_free (op->data.exp);
          break;
        }

      case SCRIPT_OP_TYPE_BREAK:
      case SCRIPT_OP_TYPE_CONTINUE:
        {
          break;
        }
    }
  free (op);
}

static void script_parse_op_list_free (ply_list_t *op_list)
{
  ply_list_node_t *node;

  for (node = ply_list_get_first_node (op_list);
       node;
       node = ply_list_get_next_node (op_list, node))
    {
      script_op_t *op = ply_list_node_get_data (node);
      script_parse_op_free (op);
    }
  ply_list_free (op_list);
  return;
}

script_op_t *script_parse_file (const char *filename)
{
  script_scan_t *scan = script_scan_file (filename);

  if (!scan)
    {
      ply_error ("Parser error : Error opening file %s\n", filename);
      return NULL;
    }
  ply_list_t *list = script_parse_op_list (scan);

  script_scan_token_t *curtoken = script_scan_get_current_token (scan);
  if (curtoken->type != script_scan_TOKEN_TYPE_EOF)
    {
      script_parse_error (curtoken, "Unparsed characters at end of file");
      return NULL;
    }
  script_scan_free (scan);
  script_op_t *op = malloc (sizeof (script_op_t));
  op->type = SCRIPT_OP_TYPE_OP_BLOCK;
  op->data.list = list;
  return op;
}

script_op_t *script_parse_string (const char *string)
{
  script_scan_t *scan = script_scan_string (string);

  if (!scan)
    {
      ply_error ("Parser error : Error creating a parser with a string");
      return NULL;
    }
  ply_list_t *list = script_parse_op_list (scan);
  script_scan_free (scan);
  script_op_t *op = malloc (sizeof (script_op_t));
  op->type = SCRIPT_OP_TYPE_OP_BLOCK;
  op->data.list = list;
  return op;
}
