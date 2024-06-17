/* GStreamer Wayland Library
 *
 * Copyright (C) 2011 Intel Corporation
 * Copyright (C) 2011 Sreerenj Balachandran <sreerenj.balachandran@intel.com>
 * Copyright (C) 2014 Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstwlwindow.h"

#include "color-management-v1-client-protocol.h"
#include "color-representation-v1-client-protocol.h"
#include "fullscreen-shell-unstable-v1-client-protocol.h"
#include "single-pixel-buffer-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#define GST_CAT_DEFAULT gst_wl_window_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

typedef struct _GstWlWindowPrivate
{
  GObject parent_instance;

  GMutex *render_lock;

  GstWlDisplay *display;
  struct wl_surface *area_surface;
  struct wl_surface *area_surface_wrapper;
  struct wl_subsurface *area_subsurface;
  struct wp_viewport *area_viewport;
  struct wl_surface *video_surface;
  struct wl_surface *video_surface_wrapper;
  struct wl_subsurface *video_subsurface;
  struct wp_viewport *video_viewport;
  struct xdg_surface *xdg_surface;
  struct xdg_toplevel *xdg_toplevel;
  struct xx_color_management_surface_v2 *color_management_surface;
  struct wp_color_representation_v1 *color_representation;
  gboolean configured;
  GCond configure_cond;
  GMutex configure_mutex;

  /* the size and position of the area_(sub)surface */
  GstVideoRectangle render_rectangle;

  /* the size and position of the video_subsurface */
  GstVideoRectangle video_rectangle;

  /* the size of the video in the buffers */
  gint video_width, video_height;

  /* video width scaled according to par */
  gint scaled_width;

  enum wl_output_transform buffer_transform;

  /* when this is not set both the area_surface and the video_surface are not
   * visible and certain steps should be skipped */
  gboolean is_area_surface_mapped;

  GMutex window_lock;
  GstWlBuffer *next_buffer;
  GstVideoInfo *next_video_info;
  GstWlBuffer *staged_buffer;
  gboolean clear_window;
  struct wl_callback *frame_callback;
  struct wl_callback *commit_callback;
} GstWlWindowPrivate;

G_DEFINE_TYPE_WITH_CODE (GstWlWindow, gst_wl_window, G_TYPE_OBJECT,
    G_ADD_PRIVATE (GstWlWindow)
    GST_DEBUG_CATEGORY_INIT (gst_wl_window_debug,
        "wlwindow", 0, "wlwindow library");
    );

enum
{
  CLOSED,
  MAP,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static void gst_wl_window_finalize (GObject * gobject);

static void gst_wl_window_update_borders (GstWlWindow * self);

static void gst_wl_window_commit_buffer (GstWlWindow * self,
    GstWlBuffer * buffer);

static void gst_wl_window_set_colorimetry (GstWlWindow * self,
    GstVideoColorimetry * colorimetry);

static void
handle_xdg_toplevel_close (void *data, struct xdg_toplevel *xdg_toplevel)
{
  GstWlWindow *self = data;

  GST_DEBUG ("XDG toplevel got a \"close\" event.");
  g_signal_emit (self, signals[CLOSED], 0);
}

static void
handle_xdg_toplevel_configure (void *data, struct xdg_toplevel *xdg_toplevel,
    int32_t width, int32_t height, struct wl_array *states)
{
  GstWlWindow *self = data;
  const uint32_t *state;

  GST_DEBUG ("XDG toplevel got a \"configure\" event, [ %d, %d ].",
      width, height);

  wl_array_for_each (state, states) {
    switch (*state) {
      case XDG_TOPLEVEL_STATE_FULLSCREEN:
      case XDG_TOPLEVEL_STATE_MAXIMIZED:
      case XDG_TOPLEVEL_STATE_RESIZING:
      case XDG_TOPLEVEL_STATE_ACTIVATED:
        break;
    }
  }

  if (width <= 0 || height <= 0)
    return;

  gst_wl_window_set_render_rectangle (self, 0, 0, width, height);
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
  handle_xdg_toplevel_configure,
  handle_xdg_toplevel_close,
};

static void
handle_xdg_surface_configure (void *data, struct xdg_surface *xdg_surface,
    uint32_t serial)
{
  GstWlWindow *self = data;
  GstWlWindowPrivate *priv = gst_wl_window_get_instance_private (self);

  xdg_surface_ack_configure (xdg_surface, serial);

  g_mutex_lock (&priv->configure_mutex);
  priv->configured = TRUE;
  g_cond_signal (&priv->configure_cond);
  g_mutex_unlock (&priv->configure_mutex);
}

static const struct xdg_surface_listener xdg_surface_listener = {
  handle_xdg_surface_configure,
};

static void
gst_wl_window_class_init (GstWlWindowClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = gst_wl_window_finalize;

  signals[CLOSED] = g_signal_new ("closed", G_TYPE_FROM_CLASS (gobject_class),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
  signals[MAP] = g_signal_new ("map", G_TYPE_FROM_CLASS (gobject_class),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);

}

static void
gst_wl_window_init (GstWlWindow * self)
{
  GstWlWindowPrivate *priv = gst_wl_window_get_instance_private (self);

  priv->configured = TRUE;
  g_cond_init (&priv->configure_cond);
  g_mutex_init (&priv->configure_mutex);
  g_mutex_init (&priv->window_lock);
}

static void
gst_wl_window_finalize (GObject * gobject)
{
  GstWlWindow *self = GST_WL_WINDOW (gobject);
  GstWlWindowPrivate *priv = gst_wl_window_get_instance_private (self);

  gst_wl_display_callback_destroy (priv->display, &priv->frame_callback);
  gst_wl_display_callback_destroy (priv->display, &priv->commit_callback);

  if (priv->staged_buffer)
    gst_wl_buffer_unref_buffer (priv->staged_buffer);

  g_cond_clear (&priv->configure_cond);
  g_mutex_clear (&priv->configure_mutex);
  g_mutex_clear (&priv->window_lock);

  if (priv->xdg_toplevel)
    xdg_toplevel_destroy (priv->xdg_toplevel);
  if (priv->xdg_surface)
    xdg_surface_destroy (priv->xdg_surface);

  if (priv->video_viewport)
    wp_viewport_destroy (priv->video_viewport);

  if (priv->color_management_surface)
    xx_color_management_surface_v2_destroy (priv->color_management_surface);

  if (priv->color_representation)
    wp_color_representation_v1_destroy (priv->color_representation);

  wl_proxy_wrapper_destroy (priv->video_surface_wrapper);
  wl_subsurface_destroy (priv->video_subsurface);
  wl_surface_destroy (priv->video_surface);

  if (priv->area_subsurface)
    wl_subsurface_destroy (priv->area_subsurface);

  if (priv->area_viewport)
    wp_viewport_destroy (priv->area_viewport);

  wl_proxy_wrapper_destroy (priv->area_surface_wrapper);
  wl_surface_destroy (priv->area_surface);

  g_clear_object (&priv->display);

  G_OBJECT_CLASS (gst_wl_window_parent_class)->finalize (gobject);
}

static GstWlWindow *
gst_wl_window_new_internal (GstWlDisplay * display, GMutex * render_lock)
{
  GstWlWindow *self;
  GstWlWindowPrivate *priv;
  struct wl_compositor *compositor;
  struct wl_event_queue *event_queue;
  struct wl_region *region;
  struct wp_viewporter *viewporter;

  self = g_object_new (GST_TYPE_WL_WINDOW, NULL);
  priv = gst_wl_window_get_instance_private (self);
  priv->display = g_object_ref (display);
  priv->render_lock = render_lock;
  g_cond_init (&priv->configure_cond);

  compositor = gst_wl_display_get_compositor (display);
  priv->area_surface = wl_compositor_create_surface (compositor);
  priv->video_surface = wl_compositor_create_surface (compositor);

  priv->area_surface_wrapper = wl_proxy_create_wrapper (priv->area_surface);
  priv->video_surface_wrapper = wl_proxy_create_wrapper (priv->video_surface);

  event_queue = gst_wl_display_get_event_queue (display);
  wl_proxy_set_queue ((struct wl_proxy *) priv->area_surface_wrapper,
      event_queue);
  wl_proxy_set_queue ((struct wl_proxy *) priv->video_surface_wrapper,
      event_queue);

  /* embed video_surface in area_surface */
  priv->video_subsurface =
      wl_subcompositor_get_subsurface (gst_wl_display_get_subcompositor
      (display), priv->video_surface, priv->area_surface);
  wl_subsurface_set_desync (priv->video_subsurface);

  viewporter = gst_wl_display_get_viewporter (display);
  if (viewporter) {
    priv->area_viewport = wp_viewporter_get_viewport (viewporter,
        priv->area_surface);
    priv->video_viewport = wp_viewporter_get_viewport (viewporter,
        priv->video_surface);
  }

  /* never accept input events on the video surface */
  region = wl_compositor_create_region (compositor);
  wl_surface_set_input_region (priv->video_surface, region);
  wl_region_destroy (region);

  return self;
}

void
gst_wl_window_ensure_fullscreen (GstWlWindow * self, gboolean fullscreen)
{
  GstWlWindowPrivate *priv;

  g_return_if_fail (self);

  priv = gst_wl_window_get_instance_private (self);
  if (fullscreen)
    xdg_toplevel_set_fullscreen (priv->xdg_toplevel, NULL);
  else
    xdg_toplevel_unset_fullscreen (priv->xdg_toplevel);
}

GstWlWindow *
gst_wl_window_new_toplevel (GstWlDisplay * display, const GstVideoInfo * info,
    gboolean fullscreen, GMutex * render_lock)
{
  GstWlWindow *self;
  GstWlWindowPrivate *priv;
  struct xdg_wm_base *xdg_wm_base;
  struct zwp_fullscreen_shell_v1 *fullscreen_shell;

  self = gst_wl_window_new_internal (display, render_lock);
  priv = gst_wl_window_get_instance_private (self);

  xdg_wm_base = gst_wl_display_get_xdg_wm_base (display);
  fullscreen_shell = gst_wl_display_get_fullscreen_shell_v1 (display);

  /* Check which protocol we will use (in order of preference) */
  if (xdg_wm_base) {
    gint64 timeout;

    /* First create the XDG surface */
    priv->xdg_surface = xdg_wm_base_get_xdg_surface (xdg_wm_base,
        priv->area_surface);
    if (!priv->xdg_surface) {
      GST_ERROR ("Unable to get xdg_surface");
      goto error;
    }
    xdg_surface_add_listener (priv->xdg_surface, &xdg_surface_listener, self);

    /* Then the toplevel */
    priv->xdg_toplevel = xdg_surface_get_toplevel (priv->xdg_surface);
    if (!priv->xdg_toplevel) {
      GST_ERROR ("Unable to get xdg_toplevel");
      goto error;
    }
    xdg_toplevel_add_listener (priv->xdg_toplevel,
        &xdg_toplevel_listener, self);
    if (g_get_prgname ()) {
      xdg_toplevel_set_app_id (priv->xdg_toplevel, g_get_prgname ());
    } else {
      xdg_toplevel_set_app_id (priv->xdg_toplevel, "org.gstreamer.wayland");
    }

    gst_wl_window_ensure_fullscreen (self, fullscreen);

    /* Finally, commit the xdg_surface state as toplevel */
    priv->configured = FALSE;
    wl_surface_commit (priv->area_surface);
    wl_display_flush (gst_wl_display_get_display (display));

    g_mutex_lock (&priv->configure_mutex);
    timeout = g_get_monotonic_time () + 100 * G_TIME_SPAN_MILLISECOND;
    while (!priv->configured) {
      if (!g_cond_wait_until (&priv->configure_cond, &priv->configure_mutex,
              timeout)) {
        GST_WARNING ("The compositor did not send configure event.");
        break;
      }
    }
    g_mutex_unlock (&priv->configure_mutex);
  } else if (fullscreen_shell) {
    zwp_fullscreen_shell_v1_present_surface (fullscreen_shell,
        priv->area_surface, ZWP_FULLSCREEN_SHELL_V1_PRESENT_METHOD_ZOOM, NULL);
  } else {
    GST_ERROR ("Unable to use either xdg_wm_base or zwp_fullscreen_shell.");
    goto error;
  }

  /* render_rectangle is already set via toplevel_configure in
   * xdg_shell fullscreen mode */
  if (!(xdg_wm_base && fullscreen)) {
    /* set the initial size to be the same as the reported video size */
    gint width =
        gst_util_uint64_scale_int_round (info->width, info->par_n, info->par_d);
    gst_wl_window_set_render_rectangle (self, 0, 0, width, info->height);
  }

  return self;

error:
  g_object_unref (self);
  return NULL;
}

GstWlWindow *
gst_wl_window_new_in_surface (GstWlDisplay * display,
    struct wl_surface *parent, GMutex * render_lock)
{
  GstWlWindow *self;
  GstWlWindowPrivate *priv;
  struct wl_region *region;

  self = gst_wl_window_new_internal (display, render_lock);
  priv = gst_wl_window_get_instance_private (self);

  /* do not accept input events on the area surface when embedded */
  region =
      wl_compositor_create_region (gst_wl_display_get_compositor (display));
  wl_surface_set_input_region (priv->area_surface, region);
  wl_region_destroy (region);

  /* embed in parent */
  priv->area_subsurface =
      wl_subcompositor_get_subsurface (gst_wl_display_get_subcompositor
      (display), priv->area_surface, parent);
  wl_subsurface_set_desync (priv->area_subsurface);

  wl_surface_commit (parent);

  return self;
}

GstWlDisplay *
gst_wl_window_get_display (GstWlWindow * self)
{
  GstWlWindowPrivate *priv;

  g_return_val_if_fail (self != NULL, NULL);

  priv = gst_wl_window_get_instance_private (self);
  return g_object_ref (priv->display);
}

struct wl_surface *
gst_wl_window_get_wl_surface (GstWlWindow * self)
{
  GstWlWindowPrivate *priv;

  g_return_val_if_fail (self != NULL, NULL);

  priv = gst_wl_window_get_instance_private (self);
  return priv->video_surface_wrapper;
}

struct wl_subsurface *
gst_wl_window_get_subsurface (GstWlWindow * self)
{
  GstWlWindowPrivate *priv;

  g_return_val_if_fail (self != NULL, NULL);

  priv = gst_wl_window_get_instance_private (self);
  return priv->area_subsurface;
}

gboolean
gst_wl_window_is_toplevel (GstWlWindow * self)
{
  GstWlWindowPrivate *priv;

  g_return_val_if_fail (self != NULL, FALSE);

  priv = gst_wl_window_get_instance_private (self);
  return (priv->xdg_toplevel != NULL);
}

static void
gst_wl_window_resize_video_surface (GstWlWindow * self, gboolean commit)
{
  GstWlWindowPrivate *priv = gst_wl_window_get_instance_private (self);
  GstVideoRectangle src = { 0, };
  GstVideoRectangle dst = { 0, };
  GstVideoRectangle res;

  switch (priv->buffer_transform) {
    case WL_OUTPUT_TRANSFORM_NORMAL:
    case WL_OUTPUT_TRANSFORM_180:
    case WL_OUTPUT_TRANSFORM_FLIPPED:
    case WL_OUTPUT_TRANSFORM_FLIPPED_180:
      src.w = priv->scaled_width;
      src.h = priv->video_height;
      break;
    case WL_OUTPUT_TRANSFORM_90:
    case WL_OUTPUT_TRANSFORM_270:
    case WL_OUTPUT_TRANSFORM_FLIPPED_90:
    case WL_OUTPUT_TRANSFORM_FLIPPED_270:
      src.w = priv->video_height;
      src.h = priv->scaled_width;
      break;
  }

  dst.w = priv->render_rectangle.w;
  dst.h = priv->render_rectangle.h;

  /* center the video_subsurface inside area_subsurface */
  if (priv->video_viewport) {
    gst_video_center_rect (&src, &dst, &res, TRUE);
    wp_viewport_set_source (priv->video_viewport, wl_fixed_from_int (0),
        wl_fixed_from_int (0), wl_fixed_from_int (priv->video_width),
        wl_fixed_from_int (priv->video_height));
    wp_viewport_set_destination (priv->video_viewport, res.w, res.h);
  } else {
    gst_video_center_rect (&src, &dst, &res, FALSE);
  }

  wl_subsurface_set_position (priv->video_subsurface, res.x, res.y);
  wl_surface_set_buffer_transform (priv->video_surface_wrapper,
      priv->buffer_transform);

  if (commit)
    wl_surface_commit (priv->video_surface_wrapper);

  priv->video_rectangle = res;
}

static void
gst_wl_window_set_opaque (GstWlWindow * self, const GstVideoInfo * info)
{
  GstWlWindowPrivate *priv = gst_wl_window_get_instance_private (self);
  struct wl_compositor *compositor;
  struct wl_region *region;

  /* Set area opaque */
  compositor = gst_wl_display_get_compositor (priv->display);
  region = wl_compositor_create_region (compositor);
  wl_region_add (region, 0, 0, G_MAXINT32, G_MAXINT32);
  wl_surface_set_opaque_region (priv->area_surface, region);
  wl_region_destroy (region);

  if (!GST_VIDEO_INFO_HAS_ALPHA (info)) {
    /* Set video opaque */
    region = wl_compositor_create_region (compositor);
    wl_region_add (region, 0, 0, G_MAXINT32, G_MAXINT32);
    wl_surface_set_opaque_region (priv->video_surface, region);
    wl_region_destroy (region);
  }
}

static void
frame_redraw_callback (void *data, struct wl_callback *callback, uint32_t time)
{
  GstWlWindow *self = data;
  GstWlWindowPrivate *priv = gst_wl_window_get_instance_private (self);
  GstWlBuffer *next_buffer;

  GST_INFO ("frame_redraw_cb ");

  wl_callback_destroy (callback);
  priv->frame_callback = NULL;

  g_mutex_lock (&priv->window_lock);
  next_buffer = priv->next_buffer = priv->staged_buffer;
  priv->staged_buffer = NULL;
  g_mutex_unlock (&priv->window_lock);

  if (next_buffer || priv->clear_window)
    gst_wl_window_commit_buffer (self, next_buffer);

  if (next_buffer)
    gst_wl_buffer_unref_buffer (next_buffer);
}

static const struct wl_callback_listener frame_callback_listener = {
  frame_redraw_callback
};

static void
gst_wl_window_commit_buffer (GstWlWindow * self, GstWlBuffer * buffer)
{
  GstWlWindowPrivate *priv = gst_wl_window_get_instance_private (self);
  GstVideoInfo *info = priv->next_video_info;
  struct wl_callback *callback;

  if (G_UNLIKELY (info)) {
    priv->scaled_width =
        gst_util_uint64_scale_int_round (info->width, info->par_n, info->par_d);
    priv->video_width = info->width;
    priv->video_height = info->height;

    wl_subsurface_set_sync (priv->video_subsurface);
    gst_wl_window_resize_video_surface (self, FALSE);
    gst_wl_window_set_opaque (self, info);

    gst_wl_window_set_colorimetry (self, &info->colorimetry);
  }

  if (G_LIKELY (buffer)) {
    callback = wl_surface_frame (priv->video_surface_wrapper);
    priv->frame_callback = callback;
    wl_callback_add_listener (callback, &frame_callback_listener, self);
    gst_wl_buffer_attach (buffer, priv->video_surface_wrapper);
    wl_surface_damage_buffer (priv->video_surface_wrapper, 0, 0, G_MAXINT32,
        G_MAXINT32);
    wl_surface_commit (priv->video_surface_wrapper);

    if (!priv->is_area_surface_mapped) {
      gst_wl_window_update_borders (self);
      wl_surface_commit (priv->area_surface_wrapper);
      priv->is_area_surface_mapped = TRUE;
      g_signal_emit (self, signals[MAP], 0);
    }
  } else {
    /* clear both video and parent surfaces */
    wl_surface_attach (priv->video_surface_wrapper, NULL, 0, 0);
    wl_surface_commit (priv->video_surface_wrapper);
    wl_surface_attach (priv->area_surface_wrapper, NULL, 0, 0);
    wl_surface_commit (priv->area_surface_wrapper);
    priv->is_area_surface_mapped = FALSE;
    priv->clear_window = FALSE;
  }

  if (G_UNLIKELY (info)) {
    /* commit also the parent (area_surface) in order to change
     * the position of the video_subsurface */
    wl_surface_commit (priv->area_surface_wrapper);
    wl_subsurface_set_desync (priv->video_subsurface);
    gst_video_info_free (priv->next_video_info);
    priv->next_video_info = NULL;
  }

}

static void
commit_callback (void *data, struct wl_callback *callback, uint32_t serial)
{
  GstWlWindow *self = data;
  GstWlWindowPrivate *priv = gst_wl_window_get_instance_private (self);
  GstWlBuffer *next_buffer;

  wl_callback_destroy (callback);
  priv->commit_callback = NULL;

  g_mutex_lock (&priv->window_lock);
  next_buffer = priv->next_buffer;
  g_mutex_unlock (&priv->window_lock);

  gst_wl_window_commit_buffer (self, next_buffer);

  if (next_buffer)
    gst_wl_buffer_unref_buffer (next_buffer);
}

static const struct wl_callback_listener commit_listener = {
  commit_callback
};

gboolean
gst_wl_window_render (GstWlWindow * self, GstWlBuffer * buffer,
    const GstVideoInfo * info)
{
  GstWlWindowPrivate *priv = gst_wl_window_get_instance_private (self);
  gboolean ret = TRUE;

  if (G_LIKELY (buffer))
    gst_wl_buffer_ref_gst_buffer (buffer);

  g_mutex_lock (&priv->window_lock);
  if (G_UNLIKELY (info))
    priv->next_video_info = gst_video_info_copy (info);

  if (priv->next_buffer && priv->staged_buffer) {
    GST_LOG_OBJECT (self, "buffer %p dropped (replaced)", priv->staged_buffer);
    gst_wl_buffer_unref_buffer (priv->staged_buffer);
    ret = FALSE;
  }

  if (!priv->next_buffer) {
    priv->next_buffer = buffer;
    priv->commit_callback =
        gst_wl_display_sync (priv->display, &commit_listener, self);
    wl_display_flush (gst_wl_display_get_display (priv->display));
  } else {
    priv->staged_buffer = buffer;
  }
  if (!buffer)
    priv->clear_window = TRUE;

  g_mutex_unlock (&priv->window_lock);

  return ret;
}

/* Update the buffer used to draw black borders. When we have viewporter
 * support, this is a scaled up 1x1 image, and without we need an black image
 * the size of the rendering areay. */
static void
gst_wl_window_update_borders (GstWlWindow * self)
{
  GstWlWindowPrivate *priv = gst_wl_window_get_instance_private (self);
  gint width, height;
  GstBuffer *buf;
  struct wl_buffer *wlbuf;
  struct wp_single_pixel_buffer_manager_v1 *single_pixel;
  GstWlBuffer *gwlbuf;

  if (gst_wl_display_get_viewporter (priv->display)) {
    wp_viewport_set_destination (priv->area_viewport,
        priv->render_rectangle.w, priv->render_rectangle.h);

    if (priv->is_area_surface_mapped) {
      /* The area_surface is already visible and only needed to get resized.
       * We don't need to attach a new buffer and are done here. */
      return;
    }
  }

  if (gst_wl_display_get_viewporter (priv->display)) {
    width = height = 1;
  } else {
    width = priv->render_rectangle.w;
    height = priv->render_rectangle.h;
  }

  /* draw the area_subsurface */
  single_pixel =
      gst_wl_display_get_single_pixel_buffer_manager_v1 (priv->display);
  if (width == 1 && height == 1 && single_pixel) {
    buf = gst_buffer_new_allocate (NULL, 1, NULL);
    wlbuf =
        wp_single_pixel_buffer_manager_v1_create_u32_rgba_buffer (single_pixel,
        0, 0, 0, 0xffffffffU);
  } else {
    GstVideoFormat format;
    GstVideoInfo info;
    GstAllocator *alloc;

    /* we want WL_SHM_FORMAT_XRGB8888 */
    format = GST_VIDEO_FORMAT_BGRx;
    gst_video_info_set_format (&info, format, width, height);
    alloc = gst_shm_allocator_get ();

    buf = gst_buffer_new_allocate (alloc, info.size, NULL);
    gst_buffer_memset (buf, 0, 0, info.size);

    wlbuf =
        gst_wl_shm_memory_construct_wl_buffer (gst_buffer_peek_memory (buf, 0),
        priv->display, &info);

    g_object_unref (alloc);
  }

  gwlbuf = gst_buffer_add_wl_buffer (buf, wlbuf, priv->display);
  gst_wl_buffer_attach (gwlbuf, priv->area_surface_wrapper);
  wl_surface_damage_buffer (priv->area_surface_wrapper, 0, 0, G_MAXINT32,
      G_MAXINT32);

  /* at this point, the GstWlBuffer keeps the buffer
   * alive and will free it on wl_buffer::release */
  gst_buffer_unref (buf);
}

static void
gst_wl_window_update_geometry (GstWlWindow * self)
{
  GstWlWindowPrivate *priv = gst_wl_window_get_instance_private (self);

  /* position the area inside the parent - needs a parent commit to apply */
  if (priv->area_subsurface) {
    wl_subsurface_set_position (priv->area_subsurface, priv->render_rectangle.x,
        priv->render_rectangle.y);
  }

  if (priv->is_area_surface_mapped)
    gst_wl_window_update_borders (self);

  if (!priv->configured)
    return;

  if (priv->scaled_width != 0) {
    wl_subsurface_set_sync (priv->video_subsurface);
    gst_wl_window_resize_video_surface (self, TRUE);
  }

  wl_surface_commit (priv->area_surface_wrapper);

  if (priv->scaled_width != 0)
    wl_subsurface_set_desync (priv->video_subsurface);
}

void
gst_wl_window_set_render_rectangle (GstWlWindow * self, gint x, gint y,
    gint w, gint h)
{
  GstWlWindowPrivate *priv = gst_wl_window_get_instance_private (self);

  if (priv->render_rectangle.x == x && priv->render_rectangle.y == y &&
      priv->render_rectangle.w == w && priv->render_rectangle.h == h)
    return;

  priv->render_rectangle.x = x;
  priv->render_rectangle.y = y;
  priv->render_rectangle.w = w;
  priv->render_rectangle.h = h;

  gst_wl_window_update_geometry (self);
}

const GstVideoRectangle *
gst_wl_window_get_render_rectangle (GstWlWindow * self)
{
  GstWlWindowPrivate *priv = gst_wl_window_get_instance_private (self);

  return &priv->render_rectangle;
}

static enum wl_output_transform
output_transform_from_orientation_method (GstVideoOrientationMethod method)
{
  switch (method) {
    case GST_VIDEO_ORIENTATION_IDENTITY:
      return WL_OUTPUT_TRANSFORM_NORMAL;
    case GST_VIDEO_ORIENTATION_90R:
      return WL_OUTPUT_TRANSFORM_90;
    case GST_VIDEO_ORIENTATION_180:
      return WL_OUTPUT_TRANSFORM_180;
    case GST_VIDEO_ORIENTATION_90L:
      return WL_OUTPUT_TRANSFORM_270;
    case GST_VIDEO_ORIENTATION_HORIZ:
      return WL_OUTPUT_TRANSFORM_FLIPPED;
    case GST_VIDEO_ORIENTATION_VERT:
      return WL_OUTPUT_TRANSFORM_FLIPPED_180;
    case GST_VIDEO_ORIENTATION_UL_LR:
      return WL_OUTPUT_TRANSFORM_FLIPPED_90;
    case GST_VIDEO_ORIENTATION_UR_LL:
      return WL_OUTPUT_TRANSFORM_FLIPPED_270;
    default:
      g_assert_not_reached ();
  }
}

void
gst_wl_window_set_rotate_method (GstWlWindow * self,
    GstVideoOrientationMethod method)
{
  GstWlWindowPrivate *priv = gst_wl_window_get_instance_private (self);

  priv->buffer_transform = output_transform_from_orientation_method (method);

  gst_wl_window_update_geometry (self);
}

typedef struct
{
  gboolean ready;
  gboolean failed;
} ImageDescriptionFeedback;

static void
image_description_failed (void *data,
    struct xx_image_description_v2 *xx_image_description_v2, uint32_t cause,
    const char *msg)
{
  ImageDescriptionFeedback *image_description_feedback = data;

  image_description_feedback->failed = TRUE;
}

static void
image_description_ready (void *data,
    struct xx_image_description_v2 *xx_image_description_v2, uint32_t identity)
{
  ImageDescriptionFeedback *image_description_feedback = data;

  image_description_feedback->ready = TRUE;
}

static const struct xx_image_description_v2_listener description_listerer = {
  .failed = image_description_failed,
  .ready = image_description_ready,
};

static void
gst_wl_window_set_colorimetry (GstWlWindow * self,
    GstVideoColorimetry * colorimetry)
{
  GstWlWindowPrivate *priv = gst_wl_window_get_instance_private (self);
  struct xx_color_manager_v2 *color_manager;
  struct wp_color_representation_manager_v1 *cr_manager;
  struct wl_display *wl_display;
  struct xx_color_manager_v2 *color_manager_wrapper = NULL;
  struct wl_event_queue *color_manager_queue = NULL;
  struct xx_image_description_creator_params_v2 *params;
  struct xx_image_description_v2 *image_description = NULL;
  ImageDescriptionFeedback image_description_feedback = { 0 };
  gboolean colorimetry_supported = FALSE;
  GList *colorimetries, *l;
  uint32_t wl_transfer_function;
  uint32_t wl_primaries;
  uint32_t wl_coefficients;
  uint32_t wl_range;
  uint32_t wl_chroma_location;

  wl_display = gst_wl_display_get_display (priv->display);
  color_manager = gst_wl_display_get_color_manager_v1 (priv->display);
  cr_manager =
      gst_wl_display_get_color_representation_manager_v1 (priv->display);

  if (!color_manager) {
    g_warning ("%s can't set colorimetry: color management not supported",
        __func__);
    return;
  }

  if (!cr_manager) {
    g_warning ("%s can't set colorimetry: color representation not supported",
        __func__);
    return;
  }

  if (!gst_wl_display_get_color_parametric_creator_supported (priv->display)) {
    g_warning ("%s can't set colorimetry: parametric creator not supported",
        __func__);
    return;
  }

  colorimetries = gst_wl_display_get_colorimetries (priv->display);
  for (l = colorimetries; l; l = l->next) {
    if (g_strcmp0 (gst_video_colorimetry_to_string (colorimetry),
            (char *) l->data) == 0) {
      colorimetry_supported = TRUE;
      break;
    }
  }
  if (!colorimetry_supported) {
    g_warning
        ("%s can't set colorimetry: colorimetry %s not supported by display",
        __func__, gst_video_colorimetry_to_string (colorimetry));

    if (priv->color_management_surface) {
      xx_color_management_surface_v2_unset_image_description
          (priv->color_management_surface);
      priv->color_management_surface = NULL;
    }
    if (!priv->color_representation) {
      wp_color_representation_v1_destroy (priv->color_representation);
      priv->color_representation = NULL;
    }
    return;
  }

  g_warning ("%s setting colorimetry: %s", __func__,
      gst_video_colorimetry_to_string (colorimetry));

  color_manager_wrapper = wl_proxy_create_wrapper (color_manager);
  color_manager_queue = wl_display_create_queue (wl_display);
  wl_proxy_set_queue ((struct wl_proxy *) color_manager_wrapper,
      color_manager_queue);

  params = xx_color_manager_v2_new_parametric_creator (color_manager_wrapper);

  switch (colorimetry->transfer) {
    case GST_VIDEO_TRANSFER_SRGB:
      wl_transfer_function = WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_SRGB;
      break;
    case GST_VIDEO_TRANSFER_BT601:
    case GST_VIDEO_TRANSFER_BT709:
    case GST_VIDEO_TRANSFER_BT2020_10:
      wl_transfer_function = WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_BT709;
      break;
    case GST_VIDEO_TRANSFER_SMPTE2084:
      wl_transfer_function = WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ;
      break;
    case GST_VIDEO_TRANSFER_ARIB_STD_B67:
      wl_transfer_function = WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_HLG;
      break;
    default:
      g_assert_not_reached ();
  }

  xx_image_description_creator_params_v2_set_tf_named (params,
      wl_transfer_function);

  switch (colorimetry->primaries) {
    case GST_VIDEO_COLOR_PRIMARIES_BT709:
      wl_primaries = WP_COLOR_MANAGER_V1_PRIMARIES_SRGB;
      break;
    case GST_VIDEO_COLOR_PRIMARIES_SMPTE170M:
      wl_primaries = WP_COLOR_MANAGER_V1_PRIMARIES_NTSC;
      break;
    case GST_VIDEO_COLOR_PRIMARIES_BT2020:
      wl_primaries = WP_COLOR_MANAGER_V1_PRIMARIES_BT2020;
      break;
    default:
      g_assert_not_reached ();
  }

  xx_image_description_creator_params_v2_set_primaries_named (params,
      wl_primaries);

  image_description = xx_image_description_creator_params_v2_create (params);
  xx_image_description_v2_add_listener (image_description,
      &description_listerer, &image_description_feedback);

  while (!image_description_feedback.ready &&
      !image_description_feedback.failed) {
    if (wl_display_dispatch_queue (wl_display, color_manager_queue) == -1)
      break;
  }

  if (!image_description_feedback.ready) {
    g_warning ("%s creating image description failed", __func__);
    goto out;
  }

  if (!priv->color_management_surface) {
    priv->color_management_surface =
        xx_color_manager_v2_get_surface (color_manager,
        priv->video_surface_wrapper);
  }

  xx_color_management_surface_v2_set_image_description
      (priv->color_management_surface, image_description,
      XX_COLOR_MANAGER_V2_RENDER_INTENT_PERCEPTUAL);

  switch (colorimetry->matrix) {
    case GST_VIDEO_COLOR_MATRIX_RGB:
      wl_coefficients = WP_COLOR_REPRESENTATION_V1_COEFFICIENTS_IDENTITY;
      wl_range = WP_COLOR_REPRESENTATION_V1_RANGE_FULL;
      break;
    case GST_VIDEO_COLOR_MATRIX_BT709:
      wl_coefficients = WP_COLOR_REPRESENTATION_V1_COEFFICIENTS_BT709;
      wl_range = WP_COLOR_REPRESENTATION_V1_RANGE_LIMITED;
      break;
    case GST_VIDEO_COLOR_MATRIX_BT601:
      wl_coefficients = WP_COLOR_REPRESENTATION_V1_COEFFICIENTS_BT601;
      wl_range = WP_COLOR_REPRESENTATION_V1_RANGE_LIMITED;
      break;
    case GST_VIDEO_COLOR_MATRIX_BT2020:
      wl_coefficients = WP_COLOR_REPRESENTATION_V1_COEFFICIENTS_BT2020;
      wl_range = WP_COLOR_REPRESENTATION_V1_RANGE_LIMITED;
      break;
    default:
      g_assert_not_reached ();
  }

  if (g_strcmp0 (gst_video_colorimetry_to_string (colorimetry),
          GST_VIDEO_COLORIMETRY_BT2020_10) == 0 ||
      g_strcmp0 (gst_video_colorimetry_to_string (colorimetry),
          GST_VIDEO_COLORIMETRY_BT2100_PQ) == 0 ||
      g_strcmp0 (gst_video_colorimetry_to_string (colorimetry),
          GST_VIDEO_COLORIMETRY_BT2100_HLG) == 0) {
    wl_chroma_location = WP_COLOR_REPRESENTATION_V1_CHROMA_LOCATION_TYPE_2;
  } else {
    wl_chroma_location = WP_COLOR_REPRESENTATION_V1_CHROMA_LOCATION_TYPE_0;
  }

  if (!priv->color_representation) {
    priv->color_representation =
        wp_color_representation_manager_v1_create (cr_manager,
        priv->video_surface_wrapper);
  }

  wp_color_representation_v1_set_coefficients_and_range
      (priv->color_representation, wl_coefficients, wl_range);
  wp_color_representation_v1_set_chroma_location (priv->color_representation,
      wl_chroma_location);

out:
  if (image_description)
    xx_image_description_v2_destroy (image_description);
  if (color_manager_wrapper)
    wl_proxy_wrapper_destroy (color_manager_wrapper);
  if (color_manager_queue)
    wl_event_queue_destroy (color_manager_queue);
}
