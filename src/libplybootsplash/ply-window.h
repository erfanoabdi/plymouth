/* ply-window.h - APIs for putting up a splash screen
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
#ifndef PLY_WINDOW_H
#define PLY_WINDOW_H

#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>

#include "ply-buffer.h"
#include "ply-event-loop.h"
#include "ply-frame-buffer.h"

typedef struct _ply_window ply_window_t;

typedef void (* ply_window_keyboard_input_handler_t) (void *user_data,
                                                      const char *keyboard_input,
                                                      size_t      character_size);

typedef void (* ply_window_backspace_handler_t) (void *user_data);
typedef void (* ply_window_escape_handler_t) (void *user_data);
typedef void (* ply_window_enter_handler_t) (void *user_data,
                                             const char *line);

typedef void (* ply_window_draw_handler_t) (void *user_data,
                                            int   x,
                                            int   y,
                                            int   width,
                                            int   height);
typedef void (* ply_window_erase_handler_t) (void *user_data,
                                             int   x,
                                             int   y,
                                             int   width,
                                             int   height);

typedef enum
{
  PLY_WINDOW_MODE_TEXT,
  PLY_WINDOW_MODE_GRAPHICS
} ply_window_mode_t;

typedef enum
{
  PLY_WINDOW_COLOR_BLACK = 0,
  PLY_WINDOW_COLOR_RED,
  PLY_WINDOW_COLOR_GREEN,
  PLY_WINDOW_COLOR_BROWN,
  PLY_WINDOW_COLOR_BLUE,
  PLY_WINDOW_COLOR_MAGENTA,
  PLY_WINDOW_COLOR_CYAN,
  PLY_WINDOW_COLOR_WHITE,
  PLY_WINDOW_COLOR_DEFAULT = PLY_WINDOW_COLOR_WHITE + 2
} ply_window_color_t;

#ifndef PLY_HIDE_FUNCTION_DECLARATIONS
ply_window_t *ply_window_new (const char *name);
void ply_window_free (ply_window_t *window);

void ply_window_add_keyboard_input_handler (ply_window_t                        *window,
                                            ply_window_keyboard_input_handler_t  input_handler,
                                            void                                *user_data);
void ply_window_remove_keyboard_input_handler (ply_window_t                        *window,
                                               ply_window_keyboard_input_handler_t  input_handler);
void ply_window_add_backspace_handler (ply_window_t                   *window,
                                       ply_window_backspace_handler_t  backspace_handler,
                                       void                           *user_data);
void ply_window_remove_backspace_handler (ply_window_t                   *window,
                                          ply_window_backspace_handler_t  backspace_handler);
void ply_window_add_escape_handler (ply_window_t                *window,
                                    ply_window_escape_handler_t  escape_handler,
                                    void                        *user_data);
void ply_window_remove_escape_handler (ply_window_t                *window,
                                       ply_window_escape_handler_t  escape_handler);
void ply_window_add_enter_handler (ply_window_t               *window,
                                   ply_window_enter_handler_t  enter_handler,
                                   void                       *user_data);
void ply_window_remove_enter_handler (ply_window_t               *window,
                                      ply_window_enter_handler_t  enter_handler);
void ply_window_add_draw_handler (ply_window_t              *window,
                                  ply_window_draw_handler_t  draw_handler,
                                  void                      *user_data);
void ply_window_remove_draw_handler (ply_window_t              *window,
                                     ply_window_draw_handler_t  draw_handler);
void ply_window_add_erase_handler (ply_window_t               *window,
                                   ply_window_erase_handler_t  erase_handler,
                                   void                       *user_data);
void ply_window_remove_erase_handler (ply_window_t               *window,
                                      ply_window_erase_handler_t erase_handler);

bool ply_window_open (ply_window_t *window);
bool ply_window_is_open (ply_window_t *window);
void ply_window_close (ply_window_t *window);
bool ply_window_set_mode (ply_window_t      *window,
                          ply_window_mode_t  mode);
int  ply_window_get_tty_fd (ply_window_t *window);
int  ply_window_get_number_of_text_rows (ply_window_t *window);
int  ply_window_get_number_of_text_columns (ply_window_t *window);
void ply_window_set_text_cursor_position (ply_window_t *window,
                                          int           column,
                                          int           row);
void ply_window_hide_text_cursor (ply_window_t *window);
void ply_window_show_text_cursor (ply_window_t *window);
void ply_window_clear_screen (ply_window_t *window);
void ply_window_clear_text_line (ply_window_t *window);
void ply_window_clear_text_character (ply_window_t *window);
bool ply_window_supports_text_color (ply_window_t *window);
void ply_window_set_background_color (ply_window_t       *window,
                                      ply_window_color_t  color);
void ply_window_set_foreground_color (ply_window_t       *window,
                                      ply_window_color_t  color);
ply_window_color_t ply_window_get_background_color (ply_window_t *window);
ply_window_color_t ply_window_get_foreground_color (ply_window_t *window);

void ply_window_draw_area (ply_window_t *window,
                           int           x,
                           int           y,
                           int           width,
                           int           height);

void ply_window_erase_area (ply_window_t *window,
                            int           x,
                            int           y,
                            int           width,
                            int           height);

uint32_t ply_window_get_color_hex_value (ply_window_t       *window,
                                         ply_window_color_t  color);
void ply_window_set_color_hex_value (ply_window_t       *window,
                                     ply_window_color_t  color,
                                     uint32_t            hex_value);
void ply_window_reset_colors (ply_window_t *window);

void ply_window_set_draw_handler (ply_window_t                *window,
                                  ply_window_draw_handler_t    draw_handler,
                                  void                        *user_data);
void ply_window_set_erase_handler (ply_window_t               *window,
                                   ply_window_erase_handler_t  erase_handler,
                                   void                       *user_data);
void ply_window_attach_to_event_loop (ply_window_t     *window,
                                      ply_event_loop_t *loop);
ply_frame_buffer_t *ply_window_get_frame_buffer (ply_window_t *window);

#endif

#endif /* PLY_WINDOW_H */
/* vim: set ts=4 sw=4 expandtab autoindent cindent cino={.5s,(0: */
