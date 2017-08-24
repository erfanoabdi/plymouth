/* plugin.c - drm backend renderer plugin
 *
 * Copyright (C) 2006-2009 Red Hat, Inc.
 *               2008 Charlie Brej <cbrej@cs.man.ac.uk>
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
 *             Kristian Høgsberg <krh@redhat.com>
 *             Peter Jones <pjones@redhat.com>
 *             Ray Strode <rstrode@redhat.com>
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
#include <values.h>
#include <unistd.h>

#include <drm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "ply-array.h"
#include "ply-buffer.h"
#include "ply-event-loop.h"
#include "ply-list.h"
#include "ply-logger.h"
#include "ply-hashtable.h"
#include "ply-rectangle.h"
#include "ply-region.h"
#include "ply-utils.h"
#include "ply-terminal.h"

#include "ply-renderer.h"
#include "ply-renderer-plugin.h"

#define BYTES_PER_PIXEL (4)

struct _ply_renderer_head
{
        ply_renderer_backend_t *backend;
        ply_pixel_buffer_t     *pixel_buffer;
        ply_rectangle_t         area;

        unsigned long           row_stride;

        ply_array_t            *connector_ids;
        drmModeConnector       *connector0;
        int                     connector0_mode_index;

        uint32_t                controller_id;
        uint32_t                encoder_id;
        uint32_t                console_buffer_id;
        uint32_t                scan_out_buffer_id;
};

struct _ply_renderer_input_source
{
        ply_renderer_backend_t             *backend;
        ply_fd_watch_t                     *terminal_input_watch;

        ply_buffer_t                       *key_buffer;

        ply_renderer_input_source_handler_t handler;
        void                               *user_data;
};

typedef struct
{
        uint32_t id;

        uint32_t handle;
        uint32_t width;
        uint32_t height;
        uint32_t row_stride;

        void    *map_address;
        uint32_t map_size;
        int      map_count;

        uint32_t added_fb : 1;
} ply_renderer_buffer_t;

struct _ply_renderer_backend
{
        ply_event_loop_t                *loop;
        ply_terminal_t                  *terminal;

        int                              device_fd;
        char                            *device_name;
        drmModeRes                      *resources;

        ply_renderer_input_source_t      input_source;
        ply_list_t                      *heads;
        ply_hashtable_t                 *heads_by_connector_id;

        ply_hashtable_t                 *output_buffers;

        int32_t                          dither_red;
        int32_t                          dither_green;
        int32_t                          dither_blue;

        uint32_t                         is_active : 1;
        uint32_t        requires_explicit_flushing : 1;
};

ply_renderer_plugin_interface_t *ply_renderer_backend_get_interface (void);
static void ply_renderer_head_redraw (ply_renderer_backend_t *backend,
                                      ply_renderer_head_t    *head);
static bool open_input_source (ply_renderer_backend_t      *backend,
                               ply_renderer_input_source_t *input_source);
static bool reset_scan_out_buffer_if_needed (ply_renderer_backend_t *backend,
                                             ply_renderer_head_t    *head);
static void flush_head (ply_renderer_backend_t *backend,
                        ply_renderer_head_t    *head);

static bool
ply_renderer_buffer_map (ply_renderer_backend_t *backend,
                         ply_renderer_buffer_t  *buffer)
{
        struct drm_mode_map_dumb map_dumb_buffer_request;
        void *map_address;

        if (buffer->map_address != MAP_FAILED) {
                buffer->map_count++;
                return true;
        }

        memset (&map_dumb_buffer_request, 0, sizeof(struct drm_mode_map_dumb));
        map_dumb_buffer_request.handle = buffer->handle;
        if (drmIoctl (backend->device_fd, DRM_IOCTL_MODE_MAP_DUMB, &map_dumb_buffer_request) < 0) {
                ply_trace ("Could not map GEM object %u: %m", buffer->handle);
                return false;
        }

        map_address = mmap (0, buffer->map_size,
                            PROT_READ | PROT_WRITE, MAP_SHARED,
                            backend->device_fd, map_dumb_buffer_request.offset);

        if (map_address == MAP_FAILED)
                return false;

        buffer->map_address = map_address;
        buffer->map_count++;

        return true;
}

static void
ply_renderer_buffer_unmap (ply_renderer_backend_t *backend,
                           ply_renderer_buffer_t  *buffer)
{
        buffer->map_count--;

        assert (buffer->map_count >= 0);
}

static ply_renderer_buffer_t *
ply_renderer_buffer_new (ply_renderer_backend_t *backend,
                         uint32_t                width,
                         uint32_t                height)
{
        ply_renderer_buffer_t *buffer;
        struct drm_mode_create_dumb create_dumb_buffer_request;

        buffer = calloc (1, sizeof(ply_renderer_buffer_t));
        buffer->width = width;
        buffer->height = height;
        buffer->map_address = MAP_FAILED;

        memset (&create_dumb_buffer_request, 0, sizeof(struct drm_mode_create_dumb));

        create_dumb_buffer_request.width = width;
        create_dumb_buffer_request.height = height;
        create_dumb_buffer_request.bpp = 32;
        create_dumb_buffer_request.flags = 0;

        if (drmIoctl (backend->device_fd,
                      DRM_IOCTL_MODE_CREATE_DUMB,
                      &create_dumb_buffer_request) < 0) {
                free (buffer);
                ply_trace ("Could not allocate GEM object for frame buffer: %m");
                return NULL;
        }

        buffer->handle = create_dumb_buffer_request.handle;
        buffer->row_stride = create_dumb_buffer_request.pitch;
        buffer->map_size = create_dumb_buffer_request.size;

        ply_trace ("returning %ux%u buffer with stride %u",
                   width, height, buffer->row_stride);

        return buffer;
}

static void
ply_renderer_buffer_free (ply_renderer_backend_t *backend,
                          ply_renderer_buffer_t  *buffer)
{
        struct drm_mode_destroy_dumb destroy_dumb_buffer_request;

        if (buffer->added_fb)
                drmModeRmFB (backend->device_fd, buffer->id);

        if (buffer->map_address != MAP_FAILED) {
                munmap (buffer->map_address, buffer->map_size);
                buffer->map_address = MAP_FAILED;
        }

        memset (&destroy_dumb_buffer_request, 0, sizeof(struct drm_mode_destroy_dumb));
        destroy_dumb_buffer_request.handle = buffer->handle;

        if (drmIoctl (backend->device_fd,
                      DRM_IOCTL_MODE_DESTROY_DUMB,
                      &destroy_dumb_buffer_request) < 0)
                ply_trace ("Could not deallocate GEM object %u: %m", buffer->handle);

        free (buffer);
}

static ply_renderer_buffer_t *
get_buffer_from_id (ply_renderer_backend_t *backend,
                    uint32_t                id)
{
        static ply_renderer_buffer_t *buffer;

        buffer = ply_hashtable_lookup (backend->output_buffers, (void *) (uintptr_t) id);

        return buffer;
}

static uint32_t
create_output_buffer (ply_renderer_backend_t *backend,
                      unsigned long           width,
                      unsigned long           height,
                      unsigned long          *row_stride)
{
        ply_renderer_buffer_t *buffer;

        buffer = ply_renderer_buffer_new (backend, width, height);

        if (buffer == NULL) {
                ply_trace ("Could not allocate GEM object for frame buffer: %m");
                return 0;
        }

        if (drmModeAddFB (backend->device_fd, width, height,
                          24, 32, buffer->row_stride, buffer->handle,
                          &buffer->id) != 0) {
                ply_trace ("Could not set up GEM object as frame buffer: %m");
                ply_renderer_buffer_free (backend, buffer);
                return 0;
        }

        *row_stride = buffer->row_stride;

        buffer->added_fb = true;
        ply_hashtable_insert (backend->output_buffers,
                              (void *) (uintptr_t) buffer->id,
                              buffer);

        return buffer->id;
}

static bool
map_buffer (ply_renderer_backend_t *backend,
            uint32_t                buffer_id)
{
        ply_renderer_buffer_t *buffer;

        buffer = get_buffer_from_id (backend, buffer_id);

        assert (buffer != NULL);

        return ply_renderer_buffer_map (backend, buffer);
}

static void
unmap_buffer (ply_renderer_backend_t *backend,
              uint32_t                buffer_id)
{
        ply_renderer_buffer_t *buffer;

        buffer = get_buffer_from_id (backend, buffer_id);

        assert (buffer != NULL);

        ply_renderer_buffer_unmap (backend, buffer);
}

static char *
begin_flush (ply_renderer_backend_t *backend,
             uint32_t                buffer_id)
{
        ply_renderer_buffer_t *buffer;

        buffer = get_buffer_from_id (backend, buffer_id);

        assert (buffer != NULL);

        return buffer->map_address;
}

static void
end_flush (ply_renderer_backend_t *backend,
           uint32_t                buffer_id)
{
        ply_renderer_buffer_t *buffer;

        buffer = get_buffer_from_id (backend, buffer_id);

        assert (buffer != NULL);

        if (backend->requires_explicit_flushing) {
                struct drm_clip_rect flush_area;
                int ret;

                flush_area.x1 = 0;
                flush_area.y1 = 0;
                flush_area.x2 = buffer->width;
                flush_area.y2 = buffer->height;

                ret = drmModeDirtyFB (backend->device_fd, buffer->id, &flush_area, 1);

                if (ret == -ENOSYS)
                        backend->requires_explicit_flushing = false;
        }
}

static void
destroy_output_buffer (ply_renderer_backend_t *backend,
                       uint32_t                buffer_id)
{
        ply_renderer_buffer_t *buffer;

        buffer = ply_hashtable_remove (backend->output_buffers,
                                       (void *) (uintptr_t) buffer_id);

        assert (buffer != NULL);

        ply_renderer_buffer_free (backend, buffer);
}

static bool
ply_renderer_head_add_connector (ply_renderer_head_t *head,
                                 drmModeConnector    *connector,
                                 int                  connector_mode_index)
{
        drmModeModeInfo *mode;

        mode = &connector->modes[connector_mode_index];

        if (mode->hdisplay != head->area.width || mode->vdisplay != head->area.height) {
                ply_trace ("Tried to add connector with resolution %dx%d to %dx%d head",
                           (int) mode->hdisplay, (int) mode->vdisplay,
                           (int) head->area.width, (int) head->area.height);
                return false;
        } else {
                ply_trace ("Adding connector with id %d to %dx%d head",
                           (int) connector->connector_id,
                           (int) head->area.width, (int) head->area.height);
        }

        ply_array_add_uint32_element (head->connector_ids, connector->connector_id);

        return true;
}

static ply_renderer_head_t *
ply_renderer_head_new (ply_renderer_backend_t *backend,
                       drmModeConnector       *connector,
                       int                     connector_mode_index,
                       uint32_t                encoder_id,
                       uint32_t                controller_id,
                       uint32_t                console_buffer_id)
{
        ply_renderer_head_t *head;
        drmModeModeInfo *mode;

        head = calloc (1, sizeof(ply_renderer_head_t));

        head->backend = backend;
        head->encoder_id = encoder_id;
        head->connector_ids = ply_array_new (PLY_ARRAY_ELEMENT_TYPE_UINT32);
        head->controller_id = controller_id;
        head->console_buffer_id = console_buffer_id;

        assert (connector_mode_index < connector->count_modes);
        mode = &connector->modes[connector_mode_index];

        head->connector0 = connector;
        head->connector0_mode_index = connector_mode_index;

        head->area.x = 0;
        head->area.y = 0;
        head->area.width = mode->hdisplay;
        head->area.height = mode->vdisplay;

        ply_renderer_head_add_connector (head, connector, connector_mode_index);
        assert (ply_array_get_size (head->connector_ids) > 0);

        head->pixel_buffer = ply_pixel_buffer_new (head->area.width, head->area.height);
        ply_pixel_buffer_set_device_scale (head->pixel_buffer,
                                           ply_get_device_scale (head->area.width,
                                                                 head->area.height,
                                                                 connector->mmWidth,
                                                                 connector->mmHeight));

        ply_trace ("Creating %ldx%ld renderer head", head->area.width, head->area.height);
        ply_pixel_buffer_fill_with_color (head->pixel_buffer, NULL,
                                          0.0, 0.0, 0.0, 1.0);

        return head;
}

static void
ply_renderer_head_free (ply_renderer_head_t *head)
{
        ply_trace ("freeing %ldx%ld renderer head", head->area.width, head->area.height);
        ply_pixel_buffer_free (head->pixel_buffer);

        drmModeFreeConnector (head->connector0);
        ply_array_free (head->connector_ids);
        free (head);
}

static bool
ply_renderer_head_set_scan_out_buffer (ply_renderer_backend_t *backend,
                                       ply_renderer_head_t    *head,
                                       uint32_t                buffer_id)
{
        drmModeModeInfo *mode;
        uint32_t *connector_ids;
        int number_of_connectors;

        connector_ids = (uint32_t *) ply_array_get_uint32_elements (head->connector_ids);
        number_of_connectors = ply_array_get_size (head->connector_ids);

        mode = &head->connector0->modes[head->connector0_mode_index];

        ply_trace ("Setting scan out buffer of %ldx%ld head to our buffer",
                   head->area.width, head->area.height);

        /* Tell the controller to use the allocated scan out buffer on each connectors
         */
        if (drmModeSetCrtc (backend->device_fd, head->controller_id, buffer_id,
                            0, 0, connector_ids, number_of_connectors, mode) < 0) {
                ply_trace ("Couldn't set scan out buffer for head with controller id %d",
                           head->controller_id);
                return false;
        }

        return true;
}

static bool
ply_renderer_head_map (ply_renderer_backend_t *backend,
                       ply_renderer_head_t    *head)
{
        bool scan_out_set;

        assert (backend != NULL);
        assert (backend->device_fd >= 0);
        assert (backend != NULL);

        assert (head != NULL);

        ply_trace ("Creating buffer for %ldx%ld renderer head", head->area.width, head->area.height);
        head->scan_out_buffer_id = create_output_buffer (backend,
                                                         head->area.width, head->area.height,
                                                         &head->row_stride);

        if (head->scan_out_buffer_id == 0)
                return false;

        ply_trace ("Mapping buffer for %ldx%ld renderer head", head->area.width, head->area.height);
        if (!map_buffer (backend, head->scan_out_buffer_id)) {
                destroy_output_buffer (backend, head->scan_out_buffer_id);
                head->scan_out_buffer_id = 0;
                return false;
        }

        /* FIXME: Maybe we should blit the fbcon contents instead of the (blank)
         * shadow buffer?
         */
        ply_renderer_head_redraw (backend, head);

        scan_out_set = reset_scan_out_buffer_if_needed (backend, head);
        if (!scan_out_set && backend->is_active) {
                destroy_output_buffer (backend, head->scan_out_buffer_id);
                head->scan_out_buffer_id = 0;
                return false;
        }

        return true;
}

static void
ply_renderer_head_unmap (ply_renderer_backend_t *backend,
                         ply_renderer_head_t    *head)
{
        ply_trace ("unmapping %ldx%ld renderer head", head->area.width, head->area.height);
        unmap_buffer (backend, head->scan_out_buffer_id);

        destroy_output_buffer (backend, head->scan_out_buffer_id);
        head->scan_out_buffer_id = 0;
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

        dst = &map_address[area_to_flush->y * head->row_stride + area_to_flush->x * BYTES_PER_PIXEL];
        src = (char *) &shadow_buffer[area_to_flush->y * head->area.width + area_to_flush->x];

        flush_area (src, head->area.width * 4, dst, head->row_stride, area_to_flush);
}

static void
free_heads (ply_renderer_backend_t *backend)
{
        ply_list_node_t *node;

        node = ply_list_get_first_node (backend->heads);
        while (node != NULL) {
                ply_list_node_t *next_node;
                ply_renderer_head_t *head;

                head = (ply_renderer_head_t *) ply_list_node_get_data (node);
                next_node = ply_list_get_next_node (backend->heads, node);

                ply_renderer_head_free (head);
                ply_list_remove_node (backend->heads, node);

                node = next_node;
        }
}

static ply_renderer_backend_t *
create_backend (const char     *device_name,
                ply_terminal_t *terminal)
{
        ply_renderer_backend_t *backend;

        backend = calloc (1, sizeof(ply_renderer_backend_t));

        if (device_name != NULL)
                backend->device_name = strdup (device_name);
        else
                backend->device_name = strdup ("/dev/dri/card0");

        ply_trace ("creating renderer backend for device %s", backend->device_name);

        backend->device_fd = -1;

        backend->loop = ply_event_loop_get_default ();
        backend->heads = ply_list_new ();
        backend->input_source.key_buffer = ply_buffer_new ();
        backend->terminal = terminal;
        backend->requires_explicit_flushing = true;
        backend->output_buffers = ply_hashtable_new (ply_hashtable_direct_hash,
                                                     ply_hashtable_direct_compare);

        return backend;
}

static const char *
get_device_name (ply_renderer_backend_t *backend)
{
        return backend->device_name;
}

static void
destroy_backend (ply_renderer_backend_t *backend)
{
        ply_trace ("destroying renderer backend for device %s", backend->device_name);
        free_heads (backend);

        free (backend->device_name);
        ply_hashtable_free (backend->output_buffers);

        drmModeFreeResources (backend->resources);

        free (backend);
}

static void
activate (ply_renderer_backend_t *backend)
{
        ply_list_node_t *node;

        ply_trace ("taking master and scanning out");
        backend->is_active = true;

        drmSetMaster (backend->device_fd);
        node = ply_list_get_first_node (backend->heads);
        while (node != NULL) {
                ply_list_node_t *next_node;
                ply_renderer_head_t *head;

                head = (ply_renderer_head_t *) ply_list_node_get_data (node);
                next_node = ply_list_get_next_node (backend->heads, node);

                if (head->scan_out_buffer_id != 0) {
                        /* Flush out any pending drawing to the buffer
                         */
                        flush_head (backend, head);

                        /* Then send the buffer to the monitor
                         */
                        ply_renderer_head_set_scan_out_buffer (backend, head,
                                                               head->scan_out_buffer_id);
                }

                node = next_node;
        }
}

static void
deactivate (ply_renderer_backend_t *backend)
{
        ply_trace ("dropping master");
        drmDropMaster (backend->device_fd);
        backend->is_active = false;
}

static void
on_active_vt_changed (ply_renderer_backend_t *backend)
{
        if (ply_terminal_is_active (backend->terminal)) {
                ply_trace ("activating on vt change");
                activate (backend);
        } else {
                ply_trace ("deactivating on vt change");
                deactivate (backend);
        }
}

static bool
load_driver (ply_renderer_backend_t *backend)
{
        int device_fd;

        ply_trace ("Opening '%s'", backend->device_name);
        device_fd = open (backend->device_name, O_RDWR);

        if (device_fd < 0) {
                ply_trace ("open failed: %m");
                return false;
        }

        backend->device_fd = device_fd;

        drmDropMaster (device_fd);

        return true;
}

static void
unload_backend (ply_renderer_backend_t *backend)
{
        if (backend == NULL)
                return;

        ply_trace ("unloading backend");

        if (backend->device_fd >= 0) {
                drmClose (backend->device_fd);
                backend->device_fd = -1;
        }

        destroy_backend (backend);
        backend = NULL;

}

static bool
open_device (ply_renderer_backend_t *backend)
{
        assert (backend != NULL);
        assert (backend->device_name != NULL);

        if (!load_driver (backend))
                return false;

        if (backend->terminal == NULL)
                return true;

        if (!ply_terminal_open (backend->terminal)) {
                ply_trace ("could not open terminal: %m");
                return false;
        }

        if (!ply_terminal_is_vt (backend->terminal)) {
                ply_trace ("terminal is not a VT");
                ply_terminal_close (backend->terminal);
                return false;
        }

        ply_terminal_watch_for_active_vt_change (backend->terminal,
                                                 (ply_terminal_active_vt_changed_handler_t)
                                                 on_active_vt_changed,
                                                 backend);

        return true;
}

static void
close_device (ply_renderer_backend_t *backend)
{
        ply_trace ("closing device");

        free_heads (backend);

        if (backend->terminal != NULL) {
                ply_terminal_stop_watching_for_active_vt_change (backend->terminal,
                                                                 (ply_terminal_active_vt_changed_handler_t)
                                                                 on_active_vt_changed,
                                                                 backend);
        }

        unload_backend (backend);
}

static drmModeCrtc *
find_controller_for_encoder (ply_renderer_backend_t *backend,
                             drmModeEncoder         *encoder)
{
        drmModeCrtc *controller;

        controller = NULL;

        /* Monitor is already lit. We'll use the same controller.
         */
        if (encoder->crtc_id != 0) {
                controller = drmModeGetCrtc (backend->device_fd, encoder->crtc_id);

                if (controller != NULL) {
                        ply_trace ("Found already lit monitor");
                        return controller;
                }
        }

        return NULL;
}

static drmModeEncoder *
find_encoder_for_connector (ply_renderer_backend_t *backend,
                            drmModeConnector       *connector)
{
        int i;
        drmModeEncoder *encoder;

        assert (backend != NULL);

        for (i = 0; i < connector->count_encoders; i++) {
                encoder = drmModeGetEncoder (backend->device_fd,
                                             connector->encoders[i]);

                if (encoder == NULL)
                        continue;

                if (encoder->encoder_id == connector->encoder_id)
                        return encoder;

                drmModeFreeEncoder (encoder);
        }

        return NULL;
}

static bool
modes_are_equal (drmModeModeInfo *a,
                 drmModeModeInfo *b)
{
        return a->clock == b->clock &&
               a->hdisplay == b->hdisplay &&
               a->hsync_start == b->hsync_start &&
               a->hsync_end == b->hsync_end &&
               a->htotal == b->htotal &&
               a->hskew == b->hskew &&
               a->vdisplay == b->vdisplay &&
               a->vsync_start == b->vsync_start &&
               a->vsync_end == b->vsync_end &&
               a->vtotal == b->vtotal &&
               a->vscan == b->vscan &&
               a->vrefresh == b->vrefresh &&
               a->flags == b->flags &&
               a->type == b->type;
}

static int
find_index_of_mode (ply_renderer_backend_t *backend,
                    drmModeConnector       *connector,
                    drmModeModeInfo        *mode)
{
        int i;

        for (i = 0; i < connector->count_modes; i++) {
                if (modes_are_equal (&connector->modes[i], mode)) {
                        ply_trace ("Found connector mode index %d for mode %dx%d",
                                   i, mode->hdisplay, mode->vdisplay);

                        return i;
                }
        }

        return -1;
}

static int
get_index_of_active_mode (ply_renderer_backend_t *backend,
                          drmModeCrtc            *controller,
                          drmModeConnector       *connector)
{
        if (!controller->mode_valid) {
                ply_trace ("No valid mode currently active on monitor");
                return -1;
        }

        ply_trace ("Looking for connector mode index of active mode %dx%d",
                   controller->mode.hdisplay, controller->mode.vdisplay);

        return find_index_of_mode (backend, connector, &controller->mode);
}

static bool
create_heads_for_active_connectors (ply_renderer_backend_t *backend)
{
        int i;
        drmModeConnector *connector;
        ply_hashtable_t *heads_by_controller_id;

        heads_by_controller_id = ply_hashtable_new (NULL, NULL);

        for (i = 0; i < backend->resources->count_connectors; i++) {
                ply_renderer_head_t *head;
                drmModeEncoder *encoder;
                uint32_t encoder_id;
                drmModeCrtc *controller;
                uint32_t controller_id;
                uint32_t console_buffer_id;
                int connector_mode_index;

                connector = drmModeGetConnector (backend->device_fd,
                                                 backend->resources->connectors[i]);

                if (connector == NULL)
                        continue;

                if (connector->connection != DRM_MODE_CONNECTED) {
                        drmModeFreeConnector (connector);
                        continue;
                }

                if (connector->count_modes <= 0) {
                        drmModeFreeConnector (connector);
                        continue;
                }

                encoder = find_encoder_for_connector (backend, connector);

                if (encoder == NULL) {
                        drmModeFreeConnector (connector);
                        continue;
                }

                encoder_id = encoder->encoder_id;
                controller = find_controller_for_encoder (backend, encoder);
                drmModeFreeEncoder (encoder);

                if (controller == NULL) {
                        drmModeFreeConnector (connector);
                        continue;
                }

                controller_id = controller->crtc_id;

                connector_mode_index = get_index_of_active_mode (backend, controller, connector);

                /* If we couldn't find the current active mode, fall back to the first available.
                 */
                if (connector_mode_index < 0) {
                        ply_trace ("falling back to first available mode");
                        connector_mode_index = 0;
                }

                console_buffer_id = controller->buffer_id;
                drmModeFreeCrtc (controller);

                head = ply_hashtable_lookup (heads_by_controller_id,
                                             (void *) (intptr_t) controller_id);

                if (head == NULL) {
                        head = ply_renderer_head_new (backend, connector, connector_mode_index,
                                                      encoder_id, controller_id,
                                                      console_buffer_id);

                        ply_list_append_data (backend->heads, head);

                        ply_hashtable_insert (heads_by_controller_id,
                                              (void *) (intptr_t) controller_id,
                                              head);
                } else {
                        if (!ply_renderer_head_add_connector (head, connector, connector_mode_index))
                                ply_trace ("couldn't connect monitor to existing head");

                        drmModeFreeConnector (connector);
                }
        }

        ply_hashtable_free (heads_by_controller_id);

        return ply_list_get_length (backend->heads) > 0;
}

static bool
has_32bpp_support (ply_renderer_backend_t *backend)
{
        uint32_t buffer_id;
        unsigned long row_stride;
        uint32_t min_width;
        uint32_t min_height;

        min_width = backend->resources->min_width;
        min_height = backend->resources->min_height;

        /* Some backends set min_width/min_height to 0,
         * but 0x0 sized buffers don't work.
         */
        if (min_width == 0)
                min_width = 1;

        if (min_height == 0)
                min_height = 1;

        buffer_id = create_output_buffer (backend, min_width, min_height, &row_stride);

        if (buffer_id == 0) {
                ply_trace ("Could not create minimal (%ux%u) 32bpp dummy buffer",
                           backend->resources->min_width,
                           backend->resources->min_height);
                return false;
        }

        destroy_output_buffer (backend, buffer_id);

        return true;
}

static bool
query_device (ply_renderer_backend_t *backend)
{
        assert (backend != NULL);
        assert (backend->device_fd >= 0);

        backend->resources = drmModeGetResources (backend->device_fd);

        if (backend->resources == NULL) {
                ply_trace ("Could not get card resources");
                return false;
        }

        if (!create_heads_for_active_connectors (backend)) {
                ply_trace ("Could not initialize heads");
                return false;
        }

        if (!has_32bpp_support (backend)) {
                ply_trace ("Device doesn't support 32bpp framebuffer");
                return false;
        }

        return true;
}

static bool
map_to_device (ply_renderer_backend_t *backend)
{
        ply_list_node_t *node;
        bool head_mapped;

        head_mapped = false;
        node = ply_list_get_first_node (backend->heads);
        while (node != NULL) {
                ply_list_node_t *next_node;
                ply_renderer_head_t *head;

                head = (ply_renderer_head_t *) ply_list_node_get_data (node);
                next_node = ply_list_get_next_node (backend->heads, node);

                if (ply_renderer_head_map (backend, head))
                        head_mapped = true;

                node = next_node;
        }

        if (backend->terminal != NULL) {
                if (ply_terminal_is_active (backend->terminal))
                        activate (backend);
                else
                        ply_terminal_activate_vt (backend->terminal);
        } else {
                activate (backend);
        }

        return head_mapped;
}

static void
unmap_from_device (ply_renderer_backend_t *backend)
{
        ply_list_node_t *node;

        node = ply_list_get_first_node (backend->heads);
        while (node != NULL) {
                ply_list_node_t *next_node;
                ply_renderer_head_t *head;

                head = (ply_renderer_head_t *) ply_list_node_get_data (node);
                next_node = ply_list_get_next_node (backend->heads, node);

                ply_renderer_head_unmap (backend, head);

                node = next_node;
        }
}

static bool
reset_scan_out_buffer_if_needed (ply_renderer_backend_t *backend,
                                 ply_renderer_head_t    *head)
{
        drmModeCrtc *controller;
        bool did_reset = false;

        if (backend->terminal != NULL)
                if (!ply_terminal_is_active (backend->terminal))
                        return false;

        controller = drmModeGetCrtc (backend->device_fd, head->controller_id);

        if (controller == NULL)
                return false;

        if (controller->buffer_id != head->scan_out_buffer_id) {
                ply_renderer_head_set_scan_out_buffer (backend, head,
                                                       head->scan_out_buffer_id);
                did_reset = true;
        }

        drmModeFreeCrtc (controller);

        return did_reset;
}

static void
flush_head (ply_renderer_backend_t *backend,
            ply_renderer_head_t    *head)
{
        ply_region_t *updated_region;
        ply_list_t *areas_to_flush;
        ply_list_node_t *node;
        ply_pixel_buffer_t *pixel_buffer;
        char *map_address;

        assert (backend != NULL);

        if (!backend->is_active)
                return;

        if (backend->terminal != NULL) {
                ply_terminal_set_mode (backend->terminal, PLY_TERMINAL_MODE_GRAPHICS);
                ply_terminal_set_unbuffered_input (backend->terminal);
        }
        pixel_buffer = head->pixel_buffer;
        updated_region = ply_pixel_buffer_get_updated_areas (pixel_buffer);
        areas_to_flush = ply_region_get_sorted_rectangle_list (updated_region);

        map_address = begin_flush (backend, head->scan_out_buffer_id);

        node = ply_list_get_first_node (areas_to_flush);
        while (node != NULL) {
                ply_list_node_t *next_node;
                ply_rectangle_t *area_to_flush;

                area_to_flush = (ply_rectangle_t *) ply_list_node_get_data (node);

                next_node = ply_list_get_next_node (areas_to_flush, node);

                if (reset_scan_out_buffer_if_needed (backend, head))
                        ply_trace ("Needed to reset scan out buffer on %ldx%ld renderer head",
                                   head->area.width, head->area.height);

                ply_renderer_head_flush_area (head, area_to_flush, map_address);

                node = next_node;
        }

        end_flush (backend, head->scan_out_buffer_id);

        ply_region_clear (updated_region);
}

static void
ply_renderer_head_redraw (ply_renderer_backend_t *backend,
                          ply_renderer_head_t    *head)
{
        ply_region_t *region;

        ply_trace ("Redrawing %ldx%ld renderer head", head->area.width, head->area.height);

        region = ply_pixel_buffer_get_updated_areas (head->pixel_buffer);

        ply_region_add_rectangle (region, &head->area);

        flush_head (backend, head);
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
        if (head->backend != backend)
                return NULL;

        return head->pixel_buffer;
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

static void
on_key_event (ply_renderer_input_source_t *input_source,
              int                          terminal_fd)
{
        ply_buffer_append_from_fd (input_source->key_buffer,
                                   terminal_fd);

        if (input_source->handler != NULL)
                input_source->handler (input_source->user_data, input_source->key_buffer, input_source);
}

static void
on_input_source_disconnected (ply_renderer_input_source_t *input_source)
{
        ply_trace ("input source disconnected, reopening");

        open_input_source (input_source->backend, input_source);
}

static bool
open_input_source (ply_renderer_backend_t      *backend,
                   ply_renderer_input_source_t *input_source)
{
        int terminal_fd;

        assert (backend != NULL);
        assert (has_input_source (backend, input_source));

        if (backend->terminal == NULL)
                return false;

        terminal_fd = ply_terminal_get_fd (backend->terminal);

        input_source->backend = backend;
        input_source->terminal_input_watch = ply_event_loop_watch_fd (backend->loop, terminal_fd, PLY_EVENT_LOOP_FD_STATUS_HAS_DATA,
                                                                      (ply_event_handler_t) on_key_event,
                                                                      (ply_event_handler_t)
                                                                      on_input_source_disconnected, input_source);
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

        if (backend->terminal == NULL)
                return;

        ply_event_loop_stop_watching_fd (backend->loop, input_source->terminal_input_watch);
        input_source->terminal_input_watch = NULL;
        input_source->backend = NULL;
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
/* vim: set ts=4 sw=4 et ai ci cino={.5s,^-2,+.5s,t0,g0,e-2,n-2,p2s,(0,=.5s,:.5s */
