/* plugin.c - frame-backend renderer plugin
 *
 * Copyright (C) 2021 Droidian Project.
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
 * Written by: Erfan Abdi <erfangplus@gmail.com>
 */
#include "config.h"

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include <values.h>
#include <unistd.h>

#include "core/minui.h"

#include "ply-buffer.h"
#include "ply-event-loop.h"
#include "ply-list.h"
#include "ply-logger.h"
#include "ply-rectangle.h"
#include "ply-region.h"
#include "ply-utils.h"

#include "ply-renderer.h"
#include "ply-renderer-plugin.h"

#define SURFACE_DATA_ALIGNMENT 8

struct _ply_renderer_head
{
        ply_renderer_backend_t *backend;
        ply_pixel_buffer_t     *pixel_buffer;
        ply_rectangle_t         area; /* in device pixels */
        gr_surface              surface;
};

struct _ply_renderer_input_source
{
        ply_buffer_t                       *key_buffer;
        ply_renderer_input_source_handler_t handler;
        void                               *user_data;
};

struct _ply_renderer_backend
{
        ply_event_loop_t           *loop;
        ply_renderer_input_source_t input_source;
        ply_list_t                 *heads;
        ply_renderer_head_t         head;

        uint32_t                    is_active : 1;
};

ply_renderer_plugin_interface_t *ply_renderer_backend_get_interface (void);
static void flush_head (ply_renderer_backend_t *backend,
                        ply_renderer_head_t    *head);

static ply_renderer_backend_t *
create_backend (const char     *device_name,
                ply_terminal_t *terminal)
{
        ply_renderer_backend_t *backend;

        backend = calloc (1, sizeof(ply_renderer_backend_t));

        backend->loop = ply_event_loop_get_default ();
        backend->heads = ply_list_new();
        backend->input_source.key_buffer = ply_buffer_new ();

        return backend;
}

static void
destroy_backend (ply_renderer_backend_t *backend)
{
        ply_list_remove_data (backend->heads, &backend->head);
        ply_list_free (backend->heads);
        free(&backend->head);

        ply_buffer_free (backend->input_source.key_buffer);
        free (backend);
}

static bool
open_device (ply_renderer_backend_t *backend)
{
        if (gr_init(true)) {
		printf("Failed gr_init!\n");
		return false;
	}

	/* Clear the screen */
	gr_color(0, 0, 0, 255);
	gr_clear();

        return true;
}

static const char *
get_device_name (ply_renderer_backend_t *backend)
{
        return "Mini UI";
}

static void
close_device (ply_renderer_backend_t *backend)
{
        gr_exit();
        return;
}

static void
initialize_head (ply_renderer_backend_t *backend,
                 ply_renderer_head_t    *head)
{
        ply_trace ("initializing %lux%lu head",
                   head->area.width, head->area.height);
        head->pixel_buffer = ply_pixel_buffer_new (head->area.width,
                                                   head->area.height);
        ply_pixel_buffer_fill_with_color (backend->head.pixel_buffer, NULL,
                                          0.0, 0.0, 0.0, 1.0);
        ply_list_append_data (backend->heads, head);
}

static bool
query_device (ply_renderer_backend_t *backend)
{
        assert (backend != NULL);

        backend->head.area.x = 0;
        backend->head.area.y = 0;
        backend->head.area.width = gr_fb_width();
        backend->head.area.height = gr_fb_height();
        
        initialize_head (backend, &backend->head);

        return true;
}

static void
activate (ply_renderer_backend_t *backend)
{
        ply_renderer_head_t *head;
        ply_region_t *region;

        head = &backend->head;

        backend->is_active = true;
        /* Flush out any pending drawing to the buffer */
        region = ply_pixel_buffer_get_updated_areas(head->pixel_buffer);
        ply_region_add_rectangle(region, &head->area);
        flush_head (backend, head);
}

static void
deactivate (ply_renderer_backend_t *backend)
{
        backend->is_active = false;
}

static gr_surface
malloc_surface(size_t data_size)
{
        unsigned char *temp;
        gr_surface surface;

        temp = malloc(sizeof(GRSurface) + data_size + SURFACE_DATA_ALIGNMENT);
        if (!temp)
                return NULL;

        surface = (gr_surface)temp;
        surface->data = temp + sizeof(GRSurface) +
                        (SURFACE_DATA_ALIGNMENT -
                         (sizeof(GRSurface) % SURFACE_DATA_ALIGNMENT));
        return surface;
}

static bool
map_to_device (ply_renderer_backend_t *backend)
{
        ply_renderer_head_t *head;

        assert (backend != NULL);

        head = &backend->head;

        if (!(head->surface = malloc_surface(head->area.width * head->area.height * 4)))
                return false;

        head->surface->width = head->area.width;
        head->surface->height = head->area.height;
        head->surface->row_bytes = head->area.width * 4;
        head->surface->pixel_bytes = 4;

        activate (backend);

        return true;
}

static void
unmap_from_device (ply_renderer_backend_t *backend)
{
        ply_renderer_head_t *head;

        head = &backend->head;

        assert (backend != NULL);

        ply_pixel_buffer_free(head->pixel_buffer);
        head->pixel_buffer = NULL;
        free(head->surface);
}

static void
flush_area (const char      *src,
            unsigned long    src_row_stride,
            char            *dst,
            unsigned long    dst_row_stride,
            ply_rectangle_t *area_to_flush)
{
        unsigned long y1, y2, y;

        y1 = area_to_flush->y;
        y2 = y1 + area_to_flush->height;

        if (area_to_flush->width * 4 == src_row_stride &&
            area_to_flush->width * 4 == dst_row_stride) {
                memcpy (dst, src, area_to_flush->width * area_to_flush->height * 4);
                return;
        }

        for (y = y1; y < y2; y++) {
                memcpy (dst, src, area_to_flush->width * 4);
                dst += dst_row_stride;
                src += src_row_stride;
        }
}

static void
ply_renderer_head_flush_area (ply_renderer_head_t *head,
                              ply_rectangle_t     *area_to_flush,
                              char                *map_address)
{
        uint32_t *shadow_buffer;
        char *dst, *src;

        shadow_buffer = ply_pixel_buffer_get_argb32_data (head->pixel_buffer);

        dst = &map_address[area_to_flush->y * head->surface->row_bytes + area_to_flush->x * 4];
        src = (char *) &shadow_buffer[area_to_flush->y * head->area.width + area_to_flush->x];

        flush_area (src, head->area.width * 4, dst, head->surface->row_bytes, area_to_flush);
}

static void
flush_head (ply_renderer_backend_t *backend,
            ply_renderer_head_t    *head)
{
        ply_region_t *updated_region;
        ply_list_t *areas_to_flush;
        ply_list_node_t *node;
        ply_pixel_buffer_t *pixel_buffer;
        bool dirty = false;

        assert (backend != NULL);
        assert (&backend->head == head);

        if (!backend->is_active)
                return;

        pixel_buffer = head->pixel_buffer;
        updated_region = ply_pixel_buffer_get_updated_areas (pixel_buffer);
        areas_to_flush = ply_region_get_sorted_rectangle_list (updated_region);

        node = ply_list_get_first_node (areas_to_flush);
        while (node != NULL) {
                ply_rectangle_t *area_to_flush;
                area_to_flush = (ply_rectangle_t *) ply_list_node_get_data (node);

                ply_renderer_head_flush_area(head, area_to_flush, (char *)head->surface->data);
                gr_blit(head->surface, 0, 0, head->area.width, head->area.height, 0, 0);
                dirty = true;

                node = ply_list_get_next_node(areas_to_flush, node);
        }
        if (dirty)
                gr_flip();
        ply_region_clear (updated_region);
}

static ply_list_t *
get_heads (ply_renderer_backend_t *backend)
{
        return backend->heads;
}

static ply_pixel_buffer_t *
get_buffer_for_head (ply_renderer_backend_t *backend,
                     ply_renderer_head_t    *head)
{
        if (head != &backend->head)
                return NULL;

        return backend->head.pixel_buffer;
}

static bool
has_input_source (ply_renderer_backend_t      *backend,
                  ply_renderer_input_source_t *input_source)
{
        return input_source == &backend->input_source;
}

static ply_renderer_input_source_t *
get_input_source (ply_renderer_backend_t *backend)
{
        return &backend->input_source;
}

static bool
open_input_source (ply_renderer_backend_t      *backend,
                   ply_renderer_input_source_t *input_source)
{
        assert (backend != NULL);
        assert (has_input_source (backend, input_source));

        return true;
}

static void
set_handler_for_input_source (ply_renderer_backend_t             *backend,
                              ply_renderer_input_source_t        *input_source,
                              ply_renderer_input_source_handler_t handler,
                              void                               *user_data)
{
        assert (backend != NULL);
        assert (has_input_source (backend, input_source));

        input_source->handler = handler;
        input_source->user_data = user_data;
}

static void
close_input_source (ply_renderer_backend_t      *backend,
                    ply_renderer_input_source_t *input_source)
{
        assert (backend != NULL);
        assert (has_input_source (backend, input_source));
}

ply_renderer_plugin_interface_t *
ply_renderer_backend_get_interface (void)
{
        static ply_renderer_plugin_interface_t plugin_interface =
        {
                .create_backend               = create_backend,
                .destroy_backend              = destroy_backend,
                .open_device                  = open_device,
                .close_device                 = close_device,
                .query_device                 = query_device,
                .map_to_device                = map_to_device,
                .unmap_from_device            = unmap_from_device,
                .activate                     = activate,
                .deactivate                   = deactivate,
                .flush_head                   = flush_head,
                .get_heads                    = get_heads,
                .get_buffer_for_head          = get_buffer_for_head,
                .get_input_source             = get_input_source,
                .open_input_source            = open_input_source,
                .set_handler_for_input_source = set_handler_for_input_source,
                .close_input_source           = close_input_source,
                .get_device_name              = get_device_name
        };

        return &plugin_interface;
}

/* vim: set ts=4 sw=4 expandtab autoindent cindent cino={.5s,(0: */
