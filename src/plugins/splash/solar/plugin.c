/* plugin.c - boot splash plugin
 *
 * Copyright (C) 2007, 2008 Red Hat, Inc.
 *                     2008 Charlie Brej <cbrej@cs.man.ac.uk>
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
 *             Ray Strode <rstrode@redhat.com>
 */
#include "config.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <values.h>
#include <unistd.h>
#include <wchar.h>

#include "ply-boot-splash-plugin.h"
#include "ply-buffer.h"
#include "ply-entry.h"
#include "ply-event-loop.h"
#include "ply-label.h"
#include "ply-list.h"
#include "ply-logger.h"
#include "ply-frame-buffer.h"
#include "ply-image.h"
#include "ply-trigger.h"
#include "ply-utils.h"
#include "ply-window.h"

#include <linux/kd.h>

#ifndef FRAMES_PER_SECOND
#define FRAMES_PER_SECOND 40
#endif

#define FLARE_FRAMES_PER_SECOND 20
#define BG_STARS_FRAMES_PER_SECOND 10
#define FLARE_COUNT 30
#define FLARE_LINE_COUNT 20
#define HALO_BLUR 4
#define STAR_HZ 0.08

/*you can comment one or both of these out*/
/*#define SHOW_PLANETS */
/*#define SHOW_COMETS */
#define SHOW_PROGRESS_BAR
/*#define SHOW_LOGO_HALO */


typedef enum
{
  SPRITE_TYPE_STATIC,
  SPRITE_TYPE_FLARE,
  SPRITE_TYPE_SATELLITE,
  SPRITE_TYPE_PROGRESS,
  SPRITE_TYPE_STAR_BG,
} sprite_type_t;

typedef struct
{
  int x; 
  int y;
  int z;
  int oldx; 
  int oldy;
  int oldz;
  int refresh_me;
  float opacity;
  ply_image_t *image;
  sprite_type_t type;
  void* data;
} sprite_t;


typedef struct
{
  float stretch[FLARE_COUNT];
  float rotate_yz[FLARE_COUNT];
  float rotate_xy[FLARE_COUNT];
  float rotate_xz[FLARE_COUNT];
  float increase_speed[FLARE_COUNT];
  float z_offset_strength[FLARE_COUNT];
  float y_size[FLARE_COUNT];
  ply_image_t *image_a;
  ply_image_t *image_b;
  int frame_count;
} flare_t;

typedef enum
{
  SATELLITE_TYPE_PLANET,
  SATELLITE_TYPE_COMET,
} satellite_type_t;

typedef struct
{
  satellite_type_t type;
  int start_x;
  int start_y;
  int end_x;
  int end_y;
  int distance;
  double theta;
  ply_image_t *image;
  ply_image_t *image_altered;
} satellite_t;



typedef struct
{
  int start_width;
  int end_width;
  int current_width;
  ply_image_t *image;
  ply_image_t *image_altered;
} progress_t;


typedef struct
{
  int star_count;
  int *star_x;
  int *star_y;
  int *star_refresh;
  int frame_count;
} star_bg_t;

struct _ply_boot_splash_plugin
{
  ply_event_loop_t *loop;
  ply_frame_buffer_t *frame_buffer;
  ply_frame_buffer_area_t box_area, lock_area, logo_area;
  ply_image_t *logo_image;
  ply_image_t *lock_image;
  ply_image_t *box_image;
  ply_image_t *star_image;
  
#ifdef  SHOW_PLANETS
  ply_image_t *planet_image[5];
#endif
#ifdef  SHOW_PROGRESS_BAR
  ply_image_t *progress_barimage;
#endif

  ply_image_t *scaled_background_image;
#ifdef SHOW_LOGO_HALO
  ply_image_t *highlight_logo_image;
#endif

  ply_window_t *window;

  ply_entry_t *entry;
  ply_label_t *label;

  ply_trigger_t *pending_password_answer;
  ply_trigger_t *idle_trigger;

  ply_list_t *sprites;

  double now;
  
  double progress;
  double progress_target;

  uint32_t root_is_mounted : 1;
  uint32_t is_visible : 1;
  uint32_t is_animating : 1;
};

static void detach_from_event_loop (ply_boot_splash_plugin_t *plugin);



ply_boot_splash_plugin_t *
create_plugin (void)
{
  ply_boot_splash_plugin_t *plugin;

  srand ((int) ply_get_timestamp ());
  plugin = calloc (1, sizeof (ply_boot_splash_plugin_t));

  plugin->logo_image = ply_image_new (PLYMOUTH_LOGO_FILE);
  plugin->lock_image = ply_image_new (PLYMOUTH_IMAGE_DIR "solar/lock.png");
  plugin->box_image = ply_image_new (PLYMOUTH_IMAGE_DIR "solar/box.png");
  plugin->scaled_background_image = NULL;
  plugin->star_image = ply_image_new (PLYMOUTH_IMAGE_DIR "solar/star.png");
#ifdef  SHOW_PLANETS
  plugin->planet_image[0] = ply_image_new (PLYMOUTH_IMAGE_DIR "solar/planet1.png");
  plugin->planet_image[1] = ply_image_new (PLYMOUTH_IMAGE_DIR "solar/planet2.png");
  plugin->planet_image[2] = ply_image_new (PLYMOUTH_IMAGE_DIR "solar/planet3.png");
  plugin->planet_image[3] = ply_image_new (PLYMOUTH_IMAGE_DIR "solar/planet4.png");
  plugin->planet_image[4] = ply_image_new (PLYMOUTH_IMAGE_DIR "solar/planet5.png");
#endif
#ifdef  SHOW_PROGRESS_BAR
  plugin->progress_barimage = ply_image_new (PLYMOUTH_IMAGE_DIR "solar/progress_bar.png");
#endif
  plugin->entry = ply_entry_new (PLYMOUTH_IMAGE_DIR "solar");
  plugin->label = ply_label_new ();
  plugin->sprites = ply_list_new();
  plugin->progress = 0;
  plugin->progress_target = -1;
  return plugin;
}

void
destroy_plugin (ply_boot_splash_plugin_t *plugin)
{
  if (plugin == NULL)
    return;


  if (plugin->loop != NULL)
    {
      ply_event_loop_stop_watching_for_exit (plugin->loop, (ply_event_loop_exit_handler_t)
                                             detach_from_event_loop,
                                             plugin);
      detach_from_event_loop (plugin);
    }

  ply_image_free (plugin->logo_image);
  ply_image_free (plugin->lock_image);
  ply_image_free (plugin->box_image);

  ply_image_free (plugin->scaled_background_image);
  ply_image_free (plugin->star_image);
#ifdef  SHOW_PLANETS
  ply_image_free (plugin->planet_image[0]);
  ply_image_free (plugin->planet_image[1]);
  ply_image_free (plugin->planet_image[2]);
  ply_image_free (plugin->planet_image[3]);
  ply_image_free (plugin->planet_image[4]);
#endif
#ifdef  SHOW_PROGRESS_BAR
  ply_image_free (plugin->progress_barimage);
#endif

  ply_entry_free (plugin->entry);
  ply_label_free (plugin->label);
  ply_list_free (plugin->sprites);

  free (plugin);
}


static void
free_sprite (sprite_t* sprite)
{
  if (sprite)
    {
      switch (sprite->type){
          case SPRITE_TYPE_STATIC:
              break;
          case SPRITE_TYPE_SATELLITE:
            {
              satellite_t *satellite = sprite->data;
              ply_image_free (satellite->image_altered);
              break;
            }
              break;
          case SPRITE_TYPE_PROGRESS:
            {
              progress_t *progress = sprite->data;
              ply_image_free (progress->image_altered);
              break;
            }
              break;
          case SPRITE_TYPE_FLARE:
            {
              flare_t *flare = sprite->data;
              ply_image_free (flare->image_a);
              ply_image_free (flare->image_b);
              break;
            }
          case SPRITE_TYPE_STAR_BG:
            {
              star_bg_t *star_bg = sprite->data;
              free (star_bg->star_x);
              free (star_bg->star_y);
              free (star_bg->star_refresh);
              break;
            }
        }

      if (sprite->data) free(sprite->data);
      free(sprite);
    }
 return;
}



static sprite_t* 
add_sprite (ply_boot_splash_plugin_t *plugin, ply_image_t *image, int type, void* data)
{
 sprite_t* new_sprite = calloc (1, sizeof (sprite_t));
 new_sprite->x = 0;
 new_sprite->y = 0;
 new_sprite->z = 0;
 new_sprite->oldx = 0;
 new_sprite->oldy = 0;
 new_sprite->oldz = 0;
 new_sprite->opacity = 1;
 new_sprite->refresh_me = 0;
 new_sprite->image = image;
 new_sprite->type = type;
 new_sprite->data = data;
 ply_list_append_data (plugin->sprites, new_sprite);
 return new_sprite;
}


static void
draw_background (ply_boot_splash_plugin_t *plugin,
                 ply_frame_buffer_area_t  *area)
{
  ply_frame_buffer_area_t screen_area;

  if (area == NULL)
    {
      ply_frame_buffer_get_size (plugin->frame_buffer, &screen_area);
      area = &screen_area;
    }

  ply_window_erase_area (plugin->window, area->x, area->y,
                         area->width, area->height);
}

int sprite_compare_z(void *data_a, void *data_b)
{
 sprite_t *sprite_a = data_a;
 sprite_t *sprite_b = data_b;
 return sprite_a->z - sprite_b->z;
}

static void 
stretch_image(ply_image_t *scaled_image, ply_image_t *orig_image, int width)
{
  int x, y;
  int stretched_width = ply_image_get_width (scaled_image);
  int stretched_height = ply_image_get_height (scaled_image);
  int orig_width = ply_image_get_width (orig_image);
  int orig_height = ply_image_get_height (orig_image);
  uint32_t * scaled_image_data = ply_image_get_data (scaled_image);
  uint32_t * orig_image_data = ply_image_get_data (orig_image);
  
  
  for (y=0; y<stretched_height; y++)
    {
      float my_width = y+0.5;
      my_width /= (stretched_height);
      my_width *= 2;
      my_width -= 1;
      my_width *= my_width;
      my_width = sqrt(1-my_width)-1;
      my_width *= stretched_height;
      my_width /= 2;
      my_width = width+my_width;
      for (x=0; x<stretched_width;  x++)
        {
          if(x<my_width)
            {
              uint32_t value = 0x0;
              int new_x = (x * orig_width) / width;
              value = orig_image_data[new_x + y * orig_width];
              scaled_image_data[x + y * stretched_width] = value;
            }
          else
            {
              scaled_image_data[x + y * stretched_width] = 0;
            }
        }
    }
}

static void
progress_update (ply_boot_splash_plugin_t *plugin, sprite_t* sprite, double time)
{
  progress_t *progress = sprite->data;
  int newwidth = plugin->progress*(progress->end_width-progress->start_width)+progress->start_width;
  if (progress->current_width >newwidth) return;
  progress->current_width = newwidth;
  stretch_image(progress->image_altered, progress->image, newwidth);
  sprite->opacity = plugin->progress;
  sprite->refresh_me=1;
}


inline uint32_t 
star_bg_gradient_colour (int x, int y, int width, int height, bool star, float time)
{
  int full_dist =  sqrt(width*width+height*height);
  int my_dist = sqrt(x*x+y*y);
  float val;
  
  uint16_t r0 = 0x0000;  /* start colour:033c73 */
  uint16_t g0 = 0x3c00;
  uint16_t b0 = 0x7300;
  
  uint16_t r1 = 0x0000;  /* end colour:00193a */
  uint16_t g1 = 0x1900;
  uint16_t b1 = 0x3a00;
  
  uint16_t r = r0+((r1-r0)*my_dist)/full_dist;
  uint16_t g = g0+((g1-g0)*my_dist)/full_dist;
  uint16_t b = b0+((b1-b0)*my_dist)/full_dist;

  static uint16_t r_err = 0; 
  static uint16_t g_err = 0; 
  static uint16_t b_err = 0; 
  
  r += r_err;
  g += g_err;
  b += b_err;
  r_err = ((r>>8) | ((r>>8)<<8)) - r;
  g_err = ((g>>8) | ((g>>8)<<8)) - g;
  b_err = ((b>>8) | ((b>>8)<<8)) - b;
  r >>= 8;
  g >>= 8;
  b >>= 8;
  
  if (!star) {
    
    return 0xff000000 | r<<16 | g<<8 | b;
    }
  
  x -= width+720-800;
  y -= height+300-480;
  val = sqrt(x*x+y*y)/100;
  val = (sin(val-time*(2*M_PI)*STAR_HZ+atan2(y, x)*2)+1)/2;
  
  val= val*0.3;
  
  r = r*(1-val) + val*0xff;
  g = g*(1-val) + val*0xff;
  b = b*(1-val) + val*0xff;
  
  return 0xff000000 | r<<16 | g<<8 | b;
}



static void
star_bg_update (ply_boot_splash_plugin_t *plugin, sprite_t* sprite, double time)
{
  star_bg_t *star_bg = sprite->data;
  int width = ply_image_get_width (sprite->image);
  int height = ply_image_get_height (sprite->image);
  uint32_t* image_data = ply_image_get_data (sprite->image);
  int i, x, y;
  star_bg->frame_count++;
  star_bg->frame_count%=FRAMES_PER_SECOND/BG_STARS_FRAMES_PER_SECOND;

  for (i = star_bg->frame_count; i < star_bg->star_count; i+=FRAMES_PER_SECOND/BG_STARS_FRAMES_PER_SECOND){
    x = star_bg->star_x[i];
    y = star_bg->star_y[i];
    uint32_t pixel_colour = star_bg_gradient_colour (x, y, width, height, true, time);
    if (abs(((image_data[x + y * width]>>16)&0xff) - ((pixel_colour>>16)&0xff))>8){
        image_data[x + y * width] = pixel_colour;
        star_bg->star_refresh[i]=1;
        }
    }
  
  
  sprite->refresh_me=1;
}

static void
satellite_move (ply_boot_splash_plugin_t *plugin, sprite_t* sprite, double time)
{
  satellite_t *satellite = sprite->data;
  ply_frame_buffer_area_t screen_area;
  
  ply_frame_buffer_get_size (plugin->frame_buffer, &screen_area);

  int width = ply_image_get_width (sprite->image);
  int height = ply_image_get_height (sprite->image);

  sprite->x=cos(satellite->theta+(1-plugin->progress)*2000/(satellite->distance))*satellite->distance;
  sprite->y=sin(satellite->theta+(1-plugin->progress)*2000/(satellite->distance))*satellite->distance;
  sprite->z=0;

  float distance = sqrt(sprite->z*sprite->z+sprite->y*sprite->y);
  float angle_zy = atan2 (sprite->y, sprite->z)-M_PI*0.4;
  sprite->z = distance* cos(angle_zy);
  sprite->y = distance* sin(angle_zy);

  float angle_offset = atan2 (sprite->x, sprite->y);
  float cresent_angle = atan2 (sqrt(sprite->x*sprite->x+sprite->y*sprite->y), sprite->z);
  
  sprite->x+=(float)satellite->end_x*plugin->progress+(float)satellite->start_x*(1-plugin->progress)-width/2;
  sprite->y+=(float)satellite->end_y*plugin->progress+(float)satellite->start_y*(1-plugin->progress)-height/2;
  
  if (sprite->x > (signed int)screen_area.width) return;
  if (sprite->y > (signed int)screen_area.height) return;
  
  if (satellite->type == SATELLITE_TYPE_PLANET)
    {
      int x, y, z;

      uint32_t *image_data = ply_image_get_data (satellite->image);
      uint32_t *cresent_data = ply_image_get_data (satellite->image_altered);

      for (y=0; y<height; y++) for (x=0; x<width; x++)
        {
      
          float fx = x-(float)width/2;
          float fy = y-(float)height/2;
          float angle = atan2 (fy, fx)+angle_offset;
          float distance = sqrt(fy*fy+fx*fx);
          fx = cos(angle)*(distance/(width/2));
          fy = sin(angle)*(distance/(height/2));
          float want_y = sqrt(1-fx*fx);
          want_y *= -cos(cresent_angle);
          if (fy<want_y)
            {
              cresent_data[x+y*width] = image_data[x+y*width];
            }
          else
            {
              int strength =(fy-want_y)*16+2;
              uint32_t val =0;
              int alpha = ((image_data[x+y*width]>>24) & 0xFF);
              if (strength<=0)strength=1;
              if (strength>=8)strength=8;
              val |= (((image_data[x+y*width]>>24) & 0xFF)/1)<<24;
              val |= (((image_data[x+y*width]>>16) & 0xFF)/strength)<<16;
              val |= (((image_data[x+y*width]>>8 ) & 0xFF)/strength)<<8;
              val |= (((image_data[x+y*width]>>0 ) & 0xFF)/strength+(alpha-alpha/(strength))/8)<<0;
              cresent_data[x+y*width] =val;
            }
        }
    }
  
  if (satellite->type == SATELLITE_TYPE_COMET)
    {
      int x, y, z;

      uint32_t *image_data = ply_image_get_data (satellite->image);
      uint32_t *comet_data = ply_image_get_data (satellite->image_altered);
      x = width/2;
      image_data[x] = 0xFFFFFFFF;
      x = 2*sin(plugin->progress*62)+width/2;
      image_data[x] = 0xFFFFFFFF;
      x = 2*sin(plugin->progress*163)+width/2;
      image_data[x] = 0xFFFFFFFF;
      x = 2*sin(plugin->progress*275)+width/2;
      image_data[x] = 0xFFFFFFFF;
      for (y=height-1; y>0; y--)
        {
          for (x=1; x<width-1; x++)
            {
               uint32_t pixel;
               if (x>0)pixel = (image_data[(x)+(y-1)*width]>>24)*2 + (image_data[(x-1)+(y-1)*width]>>24) + (image_data[(x+1)+(y-1)*width]>>24);
               pixel /= 4.05;
               pixel |= pixel<<8;
               pixel |= pixel<<16;
               image_data[x+y*width] = pixel;
            }
        }
      for (x=1; x<width-1; x++) image_data[x] = 0x0;
      for (y=0; y<height; y++) for (x=0; x<width; x++)
        {
          float scale= cos(M_PI*0.4);
          float fx=x;
          float fy=y;
          fx -= (float) width/2;
          fy -= (float) height/2;
          fy /= scale;
          float angle = atan2 (fy, fx)-(satellite->theta+(1-plugin->progress)*2000/(satellite->distance))+M_PI/2*0.0;
          float distance = sqrt(fy*fy+fx*fx);
          fx = cos(angle)*distance;
          fy = sin(angle)*distance;
          fx += (fy*fy*2)/(satellite->distance);
          fx += (float) width/2;
          fy += (float) height/2;
          int ix=fx;
          int iy=fy;
          if (ix<0 || iy<0 || ix>=width || iy>=height){
          comet_data[x+y*width] = 0;
            }
          else
            {
              comet_data[x+y*width] = image_data[ix+iy*width];
            }
        }
    }
  return;
}


static void
sprite_list_sort (ply_boot_splash_plugin_t *plugin)
{
  ply_list_sort (plugin->sprites, &sprite_compare_z);
}

static void
flare_reset (flare_t *flare, int index)
{
  flare->rotate_yz[index]=((float)(rand()%1000)/1000)*2*M_PI;
  flare->rotate_xy[index]=((float)(rand()%1000)/1000)*2*M_PI;
  flare->rotate_xz[index]=((float)(rand()%1000)/1000)*2*M_PI;
  flare->y_size[index]=((float)(rand()%1000)/1000)*0.8+0.2;
  flare->increase_speed[index] = ((float)(rand()%1000)/1000)*0.08+0.08;
  flare->stretch[index]=(((float)(rand()%1000)/1000)*0.1+0.3)*flare->y_size[index];
  flare->z_offset_strength[index]=0.1;
}

static void
flare_update (sprite_t* sprite, double time)
{
  int width;
  int height;
  flare_t *flare = sprite->data;
  ply_image_t *old_image;
  ply_image_t *new_image;
  uint32_t * old_image_data;
  uint32_t * new_image_data;

  flare->frame_count++;
  if (flare->frame_count%(FRAMES_PER_SECOND/FLARE_FRAMES_PER_SECOND)){
    return;
    }

  old_image = flare->image_a;
  new_image = flare->image_b;

  old_image_data = ply_image_get_data (old_image);
  new_image_data = ply_image_get_data (new_image);

  width = ply_image_get_width (new_image);
  height = ply_image_get_height (new_image);


  int b;
  static int start=1;

  for (b=0; b<FLARE_COUNT; b++)
    {
      int flare_line;
      flare->stretch[b] += (flare->stretch[b] * flare->increase_speed[b]) * (1-(1/(3.01-flare->stretch[b])));
      flare->increase_speed[b]-=0.003;
      flare->z_offset_strength[b]+=0.01;

      if (flare->stretch[b]>2 || flare->stretch[b]<0.2)
        {
          flare_reset (flare, b);
        }
      for (flare_line=0; flare_line<FLARE_LINE_COUNT; flare_line++)
        {
          double x, y, z;
          double angle, distance;
          float theta;
          for (theta = -M_PI+(0.05*cos(flare->increase_speed[b]*1000+flare_line)); theta < M_PI; theta+=0.05)
            {
              int ix;
              int iy;

              x=(cos(theta)+0.5)*flare->stretch[b]*0.8;
              y=(sin(theta))*flare->y_size[b];
              z=x*(sin(b+flare_line*flare_line))*flare->z_offset_strength[b];
              
              float strength = 1.1-(x/2)+flare->increase_speed[b]*3;
              x+=4.5;
              if ((x*x+y*y+z*z)<25) continue;

              strength = CLAMP(strength, 0, 1);
              strength*=32;

              x+=0.05*sin(4*theta*(sin(b+flare_line*5)));
              y+=0.05*cos(4*theta*(sin(b+flare_line*5)));
              z+=0.05*sin(4*theta*(sin(b+flare_line*5)));

              distance = sqrt(x*x+y*y);
              angle = atan2 (y, x) + flare->rotate_xy[b]+0.02*sin(b*flare_line) ;
              x = distance* cos(angle);
              y = distance* sin(angle);

              distance = sqrt(z*z+y*y);
              angle = atan2 (y, z) + flare->rotate_yz[b]+0.02*sin(3*b*flare_line);
              z = distance* cos(angle);
              y = distance* sin(angle);

              distance = sqrt(x*x+z*z);
              angle = atan2 (z, x) + flare->rotate_xz[b]+0.02*sin(8*b*flare_line);
              x = distance* cos(angle);
              z = distance* sin(angle);


              x*=41;
              y*=41;

              x+=720-800+width;
              y+=300-480+height;

              ix=x;
              iy=y;
              if (ix>=(width-1) || iy>=(height-1) || ix<=0 || iy<=0) continue;

              uint32_t colour = MIN (strength + (old_image_data[ix + iy * width]>>24), 255);
              colour <<= 24;
              old_image_data[ix + iy * width] = colour;

            }
        }
    }

  {
  int  x, y;
  for (y = 1; y < (height-1); y++)
    {
      for (x = 1; x < (width-1); x++)
        {
          uint32_t value = 0;
          value += (old_image_data[(x-1) + (y-1) * width]>>24)*1;
          value += (old_image_data[ x +    (y-1) * width]>>24)*2;
          value += (old_image_data[(x+1) + (y-1) * width]>>24)*1;
          value += (old_image_data[(x-1) +  y    * width]>>24)*2;
          value += (old_image_data[ x +     y    * width]>>24)*8;
          value += (old_image_data[(x+1) +  y    * width]>>24)*2;
          value += (old_image_data[(x-1) + (y+1) * width]>>24)*1;
          value += (old_image_data[ x +    (y+1) * width]>>24)*2;
          value += (old_image_data[(x+1) + (y+1) * width]>>24)*1;
          value /=21;
          value = (value<<24) | ((int)(value*0.7)<<16) | (value<<8) | (value<<0);
          new_image_data[x +y * width] = value;
        }
    }
  }
  flare->image_a = new_image;
  flare->image_b = old_image;
  sprite->image = new_image;
  sprite->refresh_me = 1;
  return;
}




static void
sprite_move (ply_boot_splash_plugin_t *plugin, sprite_t* sprite, double time)
{
  sprite->oldx = sprite->x;
  sprite->oldy = sprite->y;
  sprite->oldz = sprite->z;
  switch (sprite->type){
      case SPRITE_TYPE_STATIC:
          break;
      case SPRITE_TYPE_PROGRESS:
          progress_update (plugin, sprite, time);
          break;
      case SPRITE_TYPE_FLARE:
          flare_update (sprite, time);
          break;
      case SPRITE_TYPE_SATELLITE:
          satellite_move (plugin, sprite, time);
          break;
      case SPRITE_TYPE_STAR_BG:
          star_bg_update (plugin, sprite, time);
          break;
    }
}

void
on_draw (ply_boot_splash_plugin_t *plugin,
         int                       x,
         int                       y,
         int                       width,
         int                       height);

static void
animate_attime (ply_boot_splash_plugin_t *plugin, double time)
{
  ply_list_node_t *node;
  long width, height;

  ply_window_set_mode (plugin->window, PLY_WINDOW_MODE_GRAPHICS);

  if (plugin->progress_target>=0)
      plugin->progress = (plugin->progress*10 + plugin->progress_target) /11;
    
  node = ply_list_get_first_node (plugin->sprites);
  while(node)
    {
      sprite_t* sprite = ply_list_node_get_data (node);
      sprite_move (plugin, sprite, time);
      node = ply_list_get_next_node (plugin->sprites, node);
    }

  sprite_list_sort (plugin);

  for(node = ply_list_get_first_node (plugin->sprites); node; node = ply_list_get_next_node (plugin->sprites, node))
    {
      sprite_t* sprite = ply_list_node_get_data (node);
      if (sprite->x != sprite->oldx ||
          sprite->y != sprite->oldy ||
          sprite->z != sprite->oldz ||
          sprite->refresh_me)
        {
          ply_frame_buffer_area_t sprite_area;
          sprite->refresh_me=0;

          int width = ply_image_get_width (sprite->image);
          int height= ply_image_get_height (sprite->image);
          
          if (sprite->type == SPRITE_TYPE_STAR_BG){
            star_bg_t *star_bg = sprite->data;
            int i;
            for (i=0; i<star_bg->star_count; i++){
                if (star_bg->star_refresh[i]){
                    ply_window_draw_area (plugin->window, sprite->x+star_bg->star_x[i], sprite->y+star_bg->star_y[i], 1, 1);
                    star_bg->star_refresh[i]=0;
                    }
              }
            continue;
            }
          
          
          
          int x = sprite->x - sprite->oldx;
          int y = sprite->y - sprite->oldy;

          if (x < width && x > -width && y < height && y > -height)
            {
              x=MIN(sprite->x, sprite->oldx);
              y=MIN(sprite->y, sprite->oldy);
              width =(MAX(sprite->x, sprite->oldx)-x)+ply_image_get_width (sprite->image);
              height=(MAX(sprite->y, sprite->oldy)-y)+ply_image_get_height (sprite->image);
              ply_window_draw_area (plugin->window, x, y, width, height);
            }
          else
            {
              ply_window_draw_area (plugin->window, sprite->x, sprite->y, width, height);
              ply_window_draw_area (plugin->window, sprite->oldx, sprite->oldy, width, height);
            }
        }
    }
}

static void
on_timeout (ply_boot_splash_plugin_t *plugin)
{
  double sleep_time;
  double now;

  now = ply_get_timestamp ();

  animate_attime (plugin, now);
  plugin->now = now;

  sleep_time = 1.0 / FRAMES_PER_SECOND;

  ply_event_loop_watch_for_timeout (plugin->loop, 
                                    sleep_time,
                                    (ply_event_loop_timeout_handler_t)
                                    on_timeout, plugin);
}

void
on_boot_progress (ply_boot_splash_plugin_t *plugin,
                  double                    duration,
                  double                    percent_done)
{
  if (plugin->progress_target<0)
    plugin->progress = percent_done;
  plugin->progress_target = percent_done;
}

void 
setup_solar (ply_boot_splash_plugin_t *plugin);

static void
start_animation (ply_boot_splash_plugin_t *plugin)
{

  ply_frame_buffer_area_t area;
  assert (plugin != NULL);
  assert (plugin->loop != NULL);

  if (plugin->is_animating)
     return;

  ply_frame_buffer_get_size (plugin->frame_buffer, &area);

  plugin->now = ply_get_timestamp ();
  setup_solar (plugin);
  ply_window_draw_area (plugin->window, area.x, area.y, area.width, area.height);
  on_timeout (plugin);

  plugin->is_animating = true;
}

static void
stop_animation (ply_boot_splash_plugin_t *plugin,
                ply_trigger_t            *trigger)
{
  int i;
  ply_list_node_t *node;

  assert (plugin != NULL);
  assert (plugin->loop != NULL);

  if (!plugin->is_animating)
     return;

  plugin->is_animating = false;

  if (plugin->loop != NULL)
    {
      ply_event_loop_stop_watching_for_timeout (plugin->loop,
                                                (ply_event_loop_timeout_handler_t)
                                                on_timeout, plugin);
    }

#ifdef  SHOW_LOGO_HALO
  ply_image_free(plugin->highlight_logo_image);
#endif

  for(node = ply_list_get_first_node (plugin->sprites); node; node = ply_list_get_next_node (plugin->sprites, node))
    {
      sprite_t* sprite = ply_list_node_get_data (node);
      free_sprite (sprite);
    }
  ply_list_remove_all_nodes (plugin->sprites);
}

static void
on_interrupt (ply_boot_splash_plugin_t *plugin)
{
  ply_event_loop_exit (plugin->loop, 1);
  stop_animation (plugin, NULL);
  ply_window_set_mode (plugin->window, PLY_WINDOW_MODE_TEXT);
}

static void
detach_from_event_loop (ply_boot_splash_plugin_t *plugin)
{
  plugin->loop = NULL;
}

void
on_keyboard_input (ply_boot_splash_plugin_t *plugin,
                   const char               *keyboard_input,
                   size_t                    character_size)
{
  if (plugin->pending_password_answer == NULL)
    return;

  ply_entry_add_bullet (plugin->entry);
}

void
on_backspace (ply_boot_splash_plugin_t *plugin)
{
  ply_entry_remove_bullet (plugin->entry);
}

void
on_enter (ply_boot_splash_plugin_t *plugin,
          const char               *text)
{
  if (plugin->pending_password_answer == NULL)
    return;

  ply_trigger_pull (plugin->pending_password_answer, text);
  plugin->pending_password_answer = NULL;

  ply_entry_hide (plugin->entry);
  ply_entry_remove_all_bullets (plugin->entry);
  start_animation (plugin);
}


void
on_draw (ply_boot_splash_plugin_t *plugin,
         int                       x,
         int                       y,
         int                       width,
         int                       height)
{
  ply_frame_buffer_area_t clip_area;
  clip_area.x = x;
  clip_area.y = y;
  clip_area.width = width;
  clip_area.height = height;

  bool single_pixel = 0;
  float pixel_r=0;
  float pixel_g=0;
  float pixel_b=0;
  if (width==1 && height==1)
      single_pixel = true;
  else 
      ply_frame_buffer_pause_updates (plugin->frame_buffer);

  if (plugin->pending_password_answer != NULL)
    {
      draw_background (plugin, &clip_area);
      ply_entry_draw (plugin->entry);
      ply_label_draw (plugin->label);
    }
  else
    {
      ply_list_node_t *node;
      for(node = ply_list_get_first_node (plugin->sprites); node; node = ply_list_get_next_node (plugin->sprites, node))
        {
          sprite_t* sprite = ply_list_node_get_data (node);
          ply_frame_buffer_area_t sprite_area;


          sprite_area.x = sprite->x;
          sprite_area.y = sprite->y;

          if (sprite_area.x>=(x+width)) continue;
          if (sprite_area.y>=(y+height)) continue;

          sprite_area.width =  ply_image_get_width (sprite->image);
          sprite_area.height = ply_image_get_height (sprite->image);

          if ((sprite_area.x+sprite_area.width)<=x) continue;
          if ((sprite_area.y+sprite_area.height)<=y) continue;

          if (single_pixel)
            {
              uint32_t* image_data = ply_image_get_data (sprite->image);
              uint32_t overlay_pixel = image_data[(x-sprite_area.x)+(y-sprite_area.y)*sprite_area.width];
              float  alpha = (float)((overlay_pixel>>24)&0xff)/255 * sprite->opacity;
              float  red =   (float)((overlay_pixel>>16)&0xff)/255 * sprite->opacity;
              float  green = (float)((overlay_pixel>>8) &0xff)/255 * sprite->opacity;
              float  blue =  (float)((overlay_pixel>>0) &0xff)/255 * sprite->opacity;
              pixel_r = pixel_r*(1-alpha) + red;
              pixel_g = pixel_g*(1-alpha) + green;
              pixel_b = pixel_b*(1-alpha) + blue;
            }
          else
            {
              ply_frame_buffer_fill_with_argb32_data_at_opacity_with_clip (plugin->frame_buffer,
                                                     &sprite_area, &clip_area, 0, 0,
                                                     ply_image_get_data (sprite->image), sprite->opacity);
            }
        }
    }
  if (single_pixel){
      ply_frame_buffer_fill_with_color (plugin->frame_buffer, &clip_area, pixel_r, pixel_g, pixel_b, 1.0);
      }
  else {
      ply_frame_buffer_unpause_updates (plugin->frame_buffer);
      }
}

void
on_erase (ply_boot_splash_plugin_t *plugin,
          int                       x,
          int                       y,
          int                       width,
          int                       height)
{
  ply_frame_buffer_area_t area;
  ply_frame_buffer_area_t image_area;

  area.x = x;
  area.y = y;
  area.width = width;
  area.height = height;

  image_area.x = 0;
  image_area.y = 0;
  image_area.width = ply_image_get_width(plugin->scaled_background_image);
  image_area.height = ply_image_get_height(plugin->scaled_background_image);

  ply_frame_buffer_fill_with_argb32_data_with_clip (plugin->frame_buffer,
                                                     &image_area, &area, 0, 0,
                                                     ply_image_get_data (plugin->scaled_background_image));
  
  image_area.x = image_area.width-ply_image_get_width(plugin->star_image);
  image_area.y = image_area.height-ply_image_get_height(plugin->star_image);
  image_area.width = ply_image_get_width(plugin->star_image);
  image_area.height = ply_image_get_height(plugin->star_image);
  
  
  ply_frame_buffer_fill_with_argb32_data_with_clip (plugin->frame_buffer,
                                                     &image_area, &area, 0, 0,
                                                     ply_image_get_data (plugin->star_image));
                                                     
  image_area.x = 20;
  image_area.y = 20;
  image_area.width = ply_image_get_width(plugin->logo_image);
  image_area.height = ply_image_get_height(plugin->logo_image);
  
  
  ply_frame_buffer_fill_with_argb32_data_with_clip (plugin->frame_buffer,
                                                     &image_area, &area, 0, 0,
                                                     ply_image_get_data (plugin->logo_image));
}

void
add_window (ply_boot_splash_plugin_t *plugin,
            ply_window_t             *window)
{
  plugin->window = window;
}

void
remove_window (ply_boot_splash_plugin_t *plugin,
               ply_window_t             *window)
{
  plugin->window = NULL;
}

void highlight_image (ply_image_t *highlighted_image, ply_image_t *orig_image, int distance)
{
 int x, y;
 int orig_width = ply_image_get_width(orig_image);
 int orig_height = ply_image_get_height(orig_image);
 int width = ply_image_get_width(highlighted_image);
 int height = ply_image_get_height(highlighted_image);
 
 int x_offset = (orig_width- width)/2;
 int y_offset = (orig_height-height)/2;
 uint32_t *highlighted_image_data = ply_image_get_data (highlighted_image);
 uint32_t *orig_image_data = ply_image_get_data (orig_image);
 
 for (x=0; x<width; x++)
 for (y=0; y<height; y++){
    int best=0;
    int subx, suby;
    int min_x= MAX(-distance, -x-x_offset);
    int max_x= MIN(distance, orig_width-x-x_offset);
    int min_y= MAX(-distance, -y-y_offset);
    int max_y= MIN(distance, orig_height-y-y_offset);
    for (subx=min_x; subx<max_x; subx++){
    for (suby=min_y; suby<max_y; suby++){
        uint32_t pixel = orig_image_data[x+subx+x_offset + (y+suby+y_offset) * orig_width];
        float current = 1-(sqrt((subx*subx)+(suby*suby))+1)/(distance+2);
        current*=pixel>>24;
        if (current>best) best=current;
        }
        if (best >=255) break;
    }
    uint32_t val = best<<24|best<<16|best<<8|best;
    highlighted_image_data[x + y * width] = val;
    }
 
 
}
void 
setup_solar (ply_boot_splash_plugin_t *plugin)
{
  ply_frame_buffer_area_t screen_area;
  sprite_t *sprite;
  int i;
  int x, y;
  int width = 360;
  int height = 460;

  ply_frame_buffer_get_size (plugin->frame_buffer, &screen_area);
  
    {
      star_bg_t* star_bg;
      if (plugin->scaled_background_image)
          ply_image_free(plugin->scaled_background_image);
      
      plugin->scaled_background_image = ply_image_resize (plugin->logo_image, screen_area.width, screen_area.height);
      star_bg = malloc(sizeof(star_bg_t));
      star_bg->star_count = (screen_area.width * screen_area.height)/400;
      star_bg->star_x = malloc(sizeof(int)*star_bg->star_count);
      star_bg->star_y = malloc(sizeof(int)*star_bg->star_count);
      star_bg->star_refresh = malloc(sizeof(int)*star_bg->star_count);
      star_bg->frame_count=0;
      sprite = add_sprite (plugin, plugin->scaled_background_image, SPRITE_TYPE_STAR_BG, star_bg);
      sprite->z = -10000;
      
      uint32_t* image_data = ply_image_get_data (plugin->scaled_background_image);
      for (y=0; y<screen_area.height; y++) for (x=0; x<screen_area.width; x++){
          image_data[x + y * screen_area.width] = star_bg_gradient_colour(x, y, screen_area.width, screen_area.height, false, 0);
        }
      
      for (i=0; i<star_bg->star_count; i++){
          do
            {
              x = rand()%screen_area.width;
              y = rand()%screen_area.height;
            }
          while (image_data[x + y * screen_area.width] == 0xFFFFFFFF);
          star_bg->star_refresh[i] = 0;
          star_bg->star_x[i] = x;
          star_bg->star_y[i] = y;
          image_data[x + y * screen_area.width] = 0xFFFFFFFF;
        }
      for (i=0; i<(screen_area.width * screen_area.height)/400; i++){
        x = rand()%screen_area.width;
        y = rand()%screen_area.height;
        image_data[x + y * screen_area.width] = star_bg_gradient_colour(x, y, screen_area.width, screen_area.height, true, ((float)x*y*13/10000));
        }
      
      for (i=0; i<star_bg->star_count; i++){
        image_data[star_bg->star_x[i]  + star_bg->star_y[i] * screen_area.width] = 
            star_bg_gradient_colour(star_bg->star_x[i], star_bg->star_y[i], screen_area.width, screen_area.height, true, 0.0);
        }
    }

  sprite = add_sprite (plugin, plugin->logo_image, SPRITE_TYPE_STATIC, NULL);
  sprite->x=screen_area.width/2-ply_image_get_width(plugin->logo_image)/2;
  sprite->y=screen_area.height/2-ply_image_get_height(plugin->logo_image)/2;
  sprite->z=1000;

#ifdef SHOW_LOGO_HALO
  plugin->highlight_logo_image = ply_image_resize (plugin->logo_image, ply_image_get_width(plugin->logo_image)+HALO_BLUR*2, ply_image_get_height(plugin->logo_image)+HALO_BLUR*2);
  highlight_image (plugin->highlight_logo_image, plugin->logo_image, HALO_BLUR);
  sprite = add_sprite (plugin, plugin->highlight_logo_image, SPRITE_TYPE_STATIC, NULL);
  sprite->x=10-HALO_BLUR;
  sprite->y=10-HALO_BLUR;
  sprite->z=-910;
#endif

  sprite = add_sprite (plugin, plugin->star_image, SPRITE_TYPE_STATIC, NULL);
  sprite->x=screen_area.width-ply_image_get_width(plugin->star_image);
  sprite->y=screen_area.height-ply_image_get_height(plugin->star_image);
  sprite->z=0;
#ifdef  SHOW_PLANETS
  for (i=0; i<5; i++)
    {
       satellite_t* satellite = malloc(sizeof(satellite_t));
       satellite->type=SATELLITE_TYPE_PLANET;
       satellite->end_x=satellite->start_x=720-800+screen_area.width;
       satellite->end_y=satellite->start_y=300-480+screen_area.height;

       satellite->distance=i*100+280;
       satellite->theta=M_PI*0.8;
       satellite->image=plugin->planet_image[i];
       satellite->image_altered=ply_image_resize (satellite->image, ply_image_get_width(satellite->image), ply_image_get_height(satellite->image));
       sprite = add_sprite (plugin, satellite->image_altered, SPRITE_TYPE_SATELLITE, satellite);
       satellite_move (plugin, sprite, 0);

    }
#endif
#ifdef  SHOW_COMETS
  for (i=0; i<1; i++)
    {
      satellite_t* satellite = malloc(sizeof(satellite_t));
      satellite->type=SATELLITE_TYPE_COMET;
      satellite->end_x=satellite->start_x=720-800+screen_area.width;
      satellite->end_y=satellite->start_y=300-480+screen_area.height;
      satellite->distance=550+i*50;
      satellite->theta=M_PI*0.8;
#define COMET_SIZE 64
      satellite->image=ply_image_resize (plugin->progress_barimage, COMET_SIZE, COMET_SIZE);
      satellite->image_altered=ply_image_resize (satellite->image, COMET_SIZE, COMET_SIZE);
      uint32_t * image_data = ply_image_get_data (satellite->image);
      uint32_t * image_altered_data = ply_image_get_data (satellite->image_altered);


      for (y=0; y<COMET_SIZE; y++)for (x=0; x<COMET_SIZE; x++){
          image_data[x + y * COMET_SIZE] = 0x0;
          image_altered_data[x + y * COMET_SIZE] = 0x0;
        }
            
      sprite = add_sprite (plugin, satellite->image_altered, SPRITE_TYPE_SATELLITE, satellite);
      for (x=0; x<COMET_SIZE; x++) satellite_move (plugin, sprite, 0);
     }
#endif

#ifdef  SHOW_PROGRESS_BAR
  progress_t* progress = malloc(sizeof(progress_t));
  
  progress->image = plugin->progress_barimage;
  
  x = screen_area.width/2-ply_image_get_width(plugin->logo_image)/2;;
  y = screen_area.height/2+ply_image_get_height(plugin->logo_image)/2+20;
  progress->image_altered = ply_image_resize (plugin->progress_barimage, ply_image_get_width(plugin->logo_image), ply_image_get_height(plugin->progress_barimage));
  progress->start_width = 1;
  progress->end_width = ply_image_get_width(plugin->logo_image);
  progress->current_width = 0;
  
  sprite = add_sprite (plugin, progress->image_altered, SPRITE_TYPE_PROGRESS, progress);
  sprite->x=x;
  sprite->y=y;
  sprite->z=10011;
  progress_update (plugin, sprite, 0);
  
  
  
#endif

  flare_t *flare = malloc(sizeof(flare_t));

  flare->image_a = ply_image_resize (plugin->star_image, width, height);
  flare->image_b = ply_image_resize (plugin->star_image, width, height);

  sprite = add_sprite (plugin, flare->image_a, SPRITE_TYPE_FLARE, flare);
  sprite->x=screen_area.width-width;
  sprite->y=screen_area.height-height;
  sprite->z=1;

  sprite_list_sort (plugin);

  uint32_t * old_image_data = ply_image_get_data (flare->image_a);
  uint32_t * new_image_data = ply_image_get_data (flare->image_b);


  for (y = 0; y < height; y++)
    {
      for (x = 0; x < width; x++)
        {
          new_image_data[x + y * width] = 0x0;
          old_image_data[x + y * width] = 0x0;
        }
    }

  for (i=0; i<FLARE_COUNT; i++)
    {
      flare_reset (flare, i);
    }
  flare->frame_count=0;
  flare_update(sprite, i);
}

bool
show_splash_screen (ply_boot_splash_plugin_t *plugin,
                    ply_event_loop_t         *loop,
                    ply_buffer_t             *boot_buffer)
{
  assert (plugin != NULL);
  assert (plugin->logo_image != NULL);

  ply_window_set_keyboard_input_handler (plugin->window,
                                         (ply_window_keyboard_input_handler_t)
                                         on_keyboard_input, plugin);
  ply_window_set_backspace_handler (plugin->window,
                                    (ply_window_backspace_handler_t)
                                    on_backspace, plugin);
  ply_window_set_enter_handler (plugin->window,
                                (ply_window_enter_handler_t)
                                on_enter, plugin);

  ply_window_set_draw_handler (plugin->window,
                               (ply_window_draw_handler_t)
                               on_draw, plugin);

  ply_window_set_erase_handler (plugin->window,
                               (ply_window_erase_handler_t)
                               on_erase, plugin);

  plugin->loop = loop;

  ply_trace ("loading logo image");
  if (!ply_image_load (plugin->logo_image))
    return false;

  ply_trace ("loading star image");
  if (!ply_image_load (plugin->star_image))
    return false;

  ply_trace ("loading planet images");
#ifdef  SHOW_PLANETS
  if (!ply_image_load (plugin->planet_image[0]))
    return false;
  if (!ply_image_load (plugin->planet_image[1]))
    return false;
  if (!ply_image_load (plugin->planet_image[2]))
    return false;
  if (!ply_image_load (plugin->planet_image[3]))
    return false;
  if (!ply_image_load (plugin->planet_image[4]))
    return false;
#endif
#ifdef  SHOW_PROGRESS_BAR
  if (!ply_image_load (plugin->progress_barimage))
    return false;
#endif

  ply_trace ("loading lock image");
  if (!ply_image_load (plugin->lock_image))
    return false;

  ply_trace ("loading box image");
  if (!ply_image_load (plugin->box_image))
    return false;

  ply_trace ("loading entry");
  if (!ply_entry_load (plugin->entry))
    return false;

  ply_trace ("setting graphics mode");
  if (!ply_window_set_mode (plugin->window, PLY_WINDOW_MODE_GRAPHICS))
    return false;

  plugin->frame_buffer = ply_window_get_frame_buffer (plugin->window);

  ply_event_loop_watch_for_exit (loop, (ply_event_loop_exit_handler_t)
                                 detach_from_event_loop,
                                 plugin);

  ply_event_loop_watch_signal (plugin->loop,
                               SIGINT,
                               (ply_event_handler_t) 
                               on_interrupt, plugin);

  ply_window_clear_screen (plugin->window);
  ply_window_hide_text_cursor (plugin->window);

  ply_trace ("starting boot animation");

  start_animation (plugin);

  plugin->is_visible = true;

  return true;
}

void
update_status (ply_boot_splash_plugin_t *plugin,
               const char               *status)
{
  assert (plugin != NULL);
}

void
hide_splash_screen (ply_boot_splash_plugin_t *plugin,
                    ply_event_loop_t         *loop)
{
  assert (plugin != NULL);

  if (plugin->pending_password_answer != NULL)
    {
      ply_trigger_pull (plugin->pending_password_answer, "");
      plugin->pending_password_answer = NULL;
    }

  ply_window_set_keyboard_input_handler (plugin->window, NULL, NULL);
  ply_window_set_backspace_handler (plugin->window, NULL, NULL);
  ply_window_set_enter_handler (plugin->window, NULL, NULL);
  ply_window_set_draw_handler (plugin->window, NULL, NULL);
  ply_window_set_erase_handler (plugin->window, NULL, NULL);

  if (plugin->loop != NULL)
    {
      stop_animation (plugin, NULL);

      ply_event_loop_stop_watching_for_exit (plugin->loop, (ply_event_loop_exit_handler_t)
                                             detach_from_event_loop,
                                             plugin);
      detach_from_event_loop (plugin);
    }

  plugin->frame_buffer = NULL;
  plugin->is_visible = false;

  ply_window_set_mode (plugin->window, PLY_WINDOW_MODE_TEXT);
}

static void
show_password_prompt (ply_boot_splash_plugin_t *plugin,
                      const char               *prompt)
{
  ply_frame_buffer_area_t area;
  int x, y;
  int entry_width, entry_height;

  uint32_t *box_data, *lock_data;

  assert (plugin != NULL);

  draw_background (plugin, NULL);

  ply_frame_buffer_get_size (plugin->frame_buffer, &area);
  plugin->box_area.width = ply_image_get_width (plugin->box_image);
  plugin->box_area.height = ply_image_get_height (plugin->box_image);
  plugin->box_area.x = area.width / 2.0 - plugin->box_area.width / 2.0;
  plugin->box_area.y = area.height / 2.0 - plugin->box_area.height / 2.0;

  plugin->lock_area.width = ply_image_get_width (plugin->lock_image);
  plugin->lock_area.height = ply_image_get_height (plugin->lock_image);

  entry_width = ply_entry_get_width (plugin->entry);
  entry_height = ply_entry_get_height (plugin->entry);

  x = area.width / 2.0 - (plugin->lock_area.width + entry_width) / 2.0 + plugin->lock_area.width;
  y = area.height / 2.0 - entry_height / 2.0;

  plugin->lock_area.x = area.width / 2.0 - (plugin->lock_area.width + entry_width) / 2.0;
  plugin->lock_area.y = area.height / 2.0 - plugin->lock_area.height / 2.0;

  box_data = ply_image_get_data (plugin->box_image);
  ply_frame_buffer_fill_with_argb32_data (plugin->frame_buffer,
                                          &plugin->box_area, 0, 0,
                                          box_data);

  ply_entry_show (plugin->entry, plugin->loop, plugin->window, x, y);

  lock_data = ply_image_get_data (plugin->lock_image);
  ply_frame_buffer_fill_with_argb32_data (plugin->frame_buffer,
                                          &plugin->lock_area, 0, 0,
                                          lock_data);

  if (prompt != NULL)
    {
      int label_width, label_height;

      ply_label_set_text (plugin->label, prompt);
      label_width = ply_label_get_width (plugin->label);
      label_height = ply_label_get_height (plugin->label);

      x = plugin->box_area.x + plugin->lock_area.width / 2;
      y = plugin->box_area.y + plugin->box_area.height + label_height;

      ply_label_show (plugin->label, plugin->window, x, y);
    }

}

void
ask_for_password (ply_boot_splash_plugin_t *plugin,
                  const char               *prompt,
                  ply_trigger_t             *answer)
{
  plugin->pending_password_answer = answer;

  if (ply_entry_is_hidden (plugin->entry))
    {
      stop_animation (plugin, NULL);
      show_password_prompt (plugin, prompt);
    }
  else
    {
      ply_entry_draw (plugin->entry);
      ply_label_draw (plugin->label);
    }
}

void
on_root_mounted (ply_boot_splash_plugin_t *plugin)
{
  plugin->root_is_mounted = true;
}

void
become_idle (ply_boot_splash_plugin_t *plugin,
             ply_trigger_t            *idle_trigger)
{
  stop_animation (plugin, idle_trigger);
}

ply_boot_splash_plugin_interface_t *
ply_boot_splash_plugin_get_interface (void)
{
  static ply_boot_splash_plugin_interface_t plugin_interface =
    {
      .create_plugin = create_plugin,
      .destroy_plugin = destroy_plugin,
      .add_window = add_window,
      .remove_window = remove_window,
      .show_splash_screen = show_splash_screen,
      .update_status = update_status,
      .on_boot_progress = on_boot_progress,
      .hide_splash_screen = hide_splash_screen,
      .ask_for_password = ask_for_password,
      .on_root_mounted = on_root_mounted,
      .become_idle = become_idle
    };

  return &plugin_interface;
}

/* vim: set ts=4 sw=4 expandtab autoindent cindent cino={.5s,(0: */
