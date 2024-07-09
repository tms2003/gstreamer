/* GStreamer
 * Copyright (C) 2024 Benjamin Desef <projekter-git@yahoo.de>
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
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
 * Boston, MA 02110-1335, USA.
 */
/**
 * SECTION:element-gstkmscompositor
 * @title: kmscompositor
 * @short_description: A KMS/DRM based video compositor
 *
 * kmscompositor is a video self that renders video frames directly
 * on various planes of a DRM writeback connector. It then exposes the final
 * data.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 -v \
 *    videotestsrc pattern=1 ! \
 *    video/x-raw,format=RGB16,framerate=10/1,width=100,height=100 ! \
 *    kmscompositor name=comp sink_0::alpha=0.7 sink_0::x=70 sink_0::y=70 sink_1::alpha=0.5 ! \
 *    video/x-raw,format=RGB,width=1280,height=720 ! queue min-threshold-buffers=1 ! \
 *    v4l2h264enc output-io-mode=5 extra-controls="controls,h264_profile=3,video_bitrate=1500000,h264_i_frame_period=100,video_bitrate_mode=0,repeat_sequence_header=1;" ! \
 *    "video/x-h264,profile=high,level=(string)4" ! \
 *    queue! rtph264pay ! udpsink host=127.0.0.1 port=8004 \
 *    videotestsrc ! \
 *    video/x-raw,format=RGB16 ! comp.
 * ]|
 * This should send a video stream which shows a 320x240 pixels video test
 * source with some transparency revealing the background checker pattern to
 * some UDP server.
 * Another video test source with just the snow pattern of 100x100 pixels is
 * overlaid on top of the first one on the left vertically centered with a small
 * transparency showing the first video test source behind and the checker
 * pattern under it. Note that the framerate of the output video is 10 frames
 * per second.
 * </refsect2>
 * TODO: simpler example. But probably not ideal to do one where the KMS output
 * is sent to an fbdevsink
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* Unset this define for production mode
 * Set it to 0 if the detailed information about the atomic commit should be
 * printed in case of a failure.
 * Set it to 1 if it should always be printed. Note that there's a 1s delay
 * after each print, so best use it for just a single aggregation or two. */
// #define DEBUG_KMS 0

#include <stdio.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/allocators/gstdmabuf.h>

#include <xf86drm.h>
#include <stdbool.h>
#include <asm/param.h>
#include <string.h>
#include <assert.h>
#include <poll.h>
#include <unistd.h>

#include "gstkmscompositor.h"
#include "gstkmscompositorpad.h"
#include "gstkmsutils.h"
#include "gstkmsbufferpool.h"
#include "gstkmsallocator.h"

#define GST_PLUGIN_NAME "kmscompositor"
#define GST_PLUGIN_DESC "Video compositor using the Linux kernel mode setting API"

GST_DEBUG_CATEGORY_STATIC (gst_kms_compositor_debug);
#define GST_CAT_DEFAULT gst_kms_compositor_debug

#define parent_class gst_kms_compositor_parent_class

static void gst_kms_compositor_child_proxy_init (gpointer g_iface,
    gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (GstKMSCompositor, gst_kms_compositor,
    GST_TYPE_VIDEO_AGGREGATOR,
    GST_DEBUG_CATEGORY_GET (gst_kms_compositor_debug, "kmscompositor");
    G_IMPLEMENT_INTERFACE (GST_TYPE_CHILD_PROXY,
        gst_kms_compositor_child_proxy_init));
GST_ELEMENT_REGISTER_DEFINE (kmscompositor, "kmscompositor",
    GST_RANK_PRIMARY + 1, GST_TYPE_KMS_COMPOSITOR);

enum
{
  PROP_DRIVER_NAME = 1,
  PROP_BUS_ID,
  PROP_CONNECTOR_ID,
  PROP_CONNECTOR_PROPS,
  PROP_FD,
  PROP_FORCE_DMA,
  PROP_N,
};

static GParamSpec *g_properties[PROP_N] = {
  NULL,
};

/* compositor-related */
static GstFlowReturn
gst_kms_compositor_aggregate_frames (GstVideoAggregator * vagg,
    GstBuffer * outbuf)
{
  GstKMSCompositor *self = GST_KMS_COMPOSITOR (vagg);
  guint sinks, i;
  guint32 total;
  struct drm_mode_atomic *atomic;
  guint32 *obj_ids, *count_props, *prop_ids;
  guint64 *prop_vals;
  int bufi, result;
  gint32 out_sync_file;
  GstFlowReturn ret;
  GList *l;

  if (GST_ELEMENT_CAST (self)->sinkpads == NULL) {
    GST_ERROR_OBJECT (self, "No input streams configured");
    return GST_FLOW_ERROR;
  }

  GST_TRACE_OBJECT (self, "Aggragating");

  GST_OBJECT_LOCK (vagg);
  /* we set all KMS properties in one atomic transaction. First get a handle on
   * how many properties there are (at most). There are several properties per
   * plane defined in GstKMSCompositorPad, but we need to set some more in the
   * transaction:
   * connector:   WRITEBACK_FD_ID [one buf], WRITEBACK_OUT_FENCE_PTR, CRTC_ID,
   *              all connector properties
   * crtc:        MODE_ID, ACTIVE
   * every plane: PROPS_PER_PLANE properties [one buf each] */
  sinks = g_list_length (GST_ELEMENT (vagg)->sinkpads);
  total = 3 + 2 + PROPS_PER_PLANE * sinks;
  if (self->connector_props)
    total += gst_structure_n_fields (self->connector_props);
  atomic = &self->atomic;
  obj_ids =
      g_realloc ((guint32 *) atomic->objs_ptr, sizeof (guint32) * (2 + sinks));
  count_props = g_realloc ((guint32 *) atomic->count_props_ptr,
      sizeof (guint32) * (2 + sinks));
  prop_ids =
      g_realloc ((guint32 *) atomic->props_ptr, sizeof (guint32) * total);
  prop_vals =
      g_realloc ((guint64 *) atomic->prop_values_ptr, sizeof (guint64) * total);
  atomic->count_objs = 0;
  atomic->objs_ptr = (guint64) obj_ids;
  atomic->count_props_ptr = (guint64) count_props;
  atomic->props_ptr = (guint64) prop_ids;
  atomic->prop_values_ptr = (guint64) prop_vals;
  /* these allocations are not freed here; we keep them in the element and free
   * them at the very end, as we'll use them all the time */
  atomic->flags = DRM_MODE_PAGE_FLIP_EVENT;
  if (self->need_modesetting)
    atomic->flags |= DRM_MODE_ATOMIC_ALLOW_MODESET;

  GstBuffer *bufs[sinks];
  bufi = 0;
  out_sync_file = -1;

  ret = GST_FLOW_ERROR;

  /* first part in transaction: set connector properties. There may be
   * user-defined properties which we want to set, but in any case, we need the
   * writeback properties. */
  {
    gboolean has_writeback_fb = FALSE;
    gboolean has_writeback_fence = FALSE;
    drmModeObjectProperties *props;

    *obj_ids = self->conn_id;
    *count_props = 0;
    props = drmModeObjectGetProperties (self->fd, self->conn_id,
        DRM_MODE_OBJECT_CONNECTOR);
    for (i = 0; i < props->count_props; ++i) {
      /* we only need rudimentary property information */
      struct drm_mode_get_property prop;

      prop.count_values = prop.count_enum_blobs = 0;
      prop.prop_id = props->props[i];
      if (drmIoctl (self->fd, DRM_IOCTL_MODE_GETPROPERTY, &prop))
        continue;
      if (!strcmp (prop.name, "WRITEBACK_FB_ID")) {
        GstMemory *mem;
        guint32 fb_id;

        if (has_writeback_fb) {
          GST_WARNING_OBJECT (self, "Property WRITEBACK_FB_ID occurs twice");
          continue;
        }
        mem = gst_buffer_peek_memory (self->src_buffer, 0);
        if (!self->src_needs_copy)      /* mem is a DMABuf, access the underlying KMS */
          mem = gst_kms_allocator_get_cached (mem);
        /* else there was no need to create a DMABuf */
        if (!gst_is_kms_memory (mem)) {
          GST_ERROR_OBJECT (self, "invalid output buffer");
          goto done;
        }
        fb_id = gst_kms_memory_get_fb_id (mem);
        if (!fb_id) {
          GST_ERROR_OBJECT (self,
              "invalid output buffer: it doesn't have a fb id");
          goto done;
        }
        ++*count_props;
        *(prop_ids++) = prop.prop_id;
        *(prop_vals++) = fb_id;
        has_writeback_fb = TRUE;
      } else if (!strcmp (prop.name, "WRITEBACK_OUT_FENCE_PTR")) {
        if (has_writeback_fence) {
          GST_WARNING_OBJECT (self,
              "Property WRITEBACK_FB_FENCE_PTR occurs twice");
          continue;
        }
        ++*count_props;
        *(prop_ids++) = prop.prop_id;
        *(prop_vals++) = (guint64) & out_sync_file;
        has_writeback_fence = TRUE;
      } else if (!strcmp (prop.name, "CRTC_ID")) {
        ++*count_props;
        *(prop_ids++) = prop.prop_id;
        *(prop_vals++) = self->crtc_id;
      } else if (self->connector_props) {
        /* GstStructure parser limits the set of supported character, so we
         * replace the invalid characters with '-'. In DRM, this is generally
         * replacing spaces into '-'. */
        const GValue *value;

        g_strcanon (prop.name, G_CSET_a_2_z G_CSET_A_2_Z G_CSET_DIGITS "_",
            '-');
        value = gst_structure_get_value (self->connector_props, prop.name);
        if (value) {
          guint64 v;
          if (G_VALUE_HOLDS (value, G_TYPE_INT))
            v = g_value_get_int (value);
          else if (G_VALUE_HOLDS (value, G_TYPE_UINT))
            v = g_value_get_uint (value);
          else if (G_VALUE_HOLDS (value, G_TYPE_INT64))
            v = g_value_get_int64 (value);
          else if (G_VALUE_HOLDS (value, G_TYPE_UINT64))
            v = g_value_get_uint64 (value);
          else {
            GST_WARNING_OBJECT (self,
                "'uint64' value expected for connector property '%s'.",
                prop.name);
            continue;
          }
          ++*count_props;
          *(prop_ids++) = prop.prop_id;
          *(prop_vals++) = v;
        }
      }
    }
    drmModeFreeObjectProperties (props);
    ++(atomic->count_objs);

    if (!has_writeback_fb || !has_writeback_fence) {
      GST_ERROR_OBJECT (self,
          "property WRITEBACK_OUT_FD or WRITEBACK_OUT_FENCE_PTR not found");
      goto done;
    }
  }

  /* second part in transaction: set crtc property (correct output mode
   * according to the desired caps) */
  if (self->need_modesetting) {
    gboolean has_mode_id = FALSE;
    gboolean has_active = FALSE;
    drmModeObjectProperties *props;

    *(++obj_ids) = self->crtc_id;
    *(++count_props) = 0;
    props = drmModeObjectGetProperties (self->fd, self->crtc_id,
        DRM_MODE_OBJECT_CRTC);
    for (i = 0; i < props->count_props; ++i) {
      struct drm_mode_get_property prop;
      prop.count_values = prop.count_enum_blobs = 0;
      prop.prop_id = props->props[i];
      if (drmIoctl (self->fd, DRM_IOCTL_MODE_GETPROPERTY, &prop))
        continue;
      if (!strcmp (prop.name, "MODE_ID")) {
        ++*count_props;
        *(prop_ids++) = prop.prop_id;
        *(prop_vals++) = self->mode_id;
        has_mode_id = TRUE;
        if (has_active)         /* short-circuit */
          break;
      } else if (!strcmp (prop.name, "ACTIVE")) {
        ++*count_props;
        *(prop_ids++) = prop.prop_id;
        *(prop_vals++) = 1;
        has_active = TRUE;
        if (has_mode_id)        /* short-circuit */
          break;
      }
    }
    drmModeFreeObjectProperties (props);
    ++(atomic->count_objs);

    if (!has_mode_id) {
      GST_ERROR_OBJECT (self, "property MODE_ID not found");
      goto done;
    }
    if (!has_active) {
      GST_ERROR_OBJECT (self, "property ACTIVE not found");
      goto done;
    }
  }
  /* third part in transaction: set input planes */
  for (l = GST_ELEMENT (vagg)->sinkpads; l; l = l->next) {
    GstKMSCompositorPad *pad = GST_KMS_COMPOSITOR_PAD (l->data);
    gint plane_id = pad->plane_id;
    GstBuffer *inbuf;

    *(++obj_ids) = plane_id;
    ++(atomic->count_objs);
    if (pad->alpha == 0)
      goto disable_plane;
    inbuf =
        gst_video_aggregator_pad_get_current_buffer (GST_VIDEO_AGGREGATOR_PAD
        (pad));
    if (inbuf) {
      GstBuffer *buf = gst_kms_compositor_pad_get_input_buffer (pad, inbuf);
      if (buf) {
        guint32 fb_id;
        GstVideoInfo vinfo;
        GstVideoCropMeta *crop;
        gint xpos, ypos, width, height, src_x, src_y;
        guint src_width, src_height, num_props;

        bufs[bufi++] = buf;
        fb_id = gst_kms_memory_get_fb_id (gst_buffer_peek_memory (buf, 0));
        if (!fb_id) {
          GST_WARNING_OBJECT (self, "invalid buffer: it doesn't have a fb id");
          goto disable_plane;
        }
        gst_buffer_copy_into (buf, inbuf, GST_BUFFER_COPY_METADATA, 0, -1);
        vinfo = pad->vinfo;

        xpos = pad->xpos;
        ypos = pad->ypos;
        width = pad->width;
        if (width < 0)
          width = GST_VIDEO_INFO_WIDTH (&self->src_vinfo);
        height = pad->height;
        if (height < 0)
          height = GST_VIDEO_INFO_HEIGHT (&self->src_vinfo);
        src_x = pad->src_x;
        src_y = pad->src_y;
        if ((crop = gst_buffer_get_video_crop_meta (buf))) {
          src_x += crop->x << 16;
          src_y += crop->y << 16;
          vinfo.width = crop->width;
          vinfo.height = crop->height;
        }
        {
          guint src_max_width = (vinfo.width >= 0 ? vinfo.width << 16 : 0);
          guint src_max_height = (vinfo.height >= 0 ? vinfo.height << 16 : 0);
          if (src_x > src_max_width || (src_y > src_max_height)) {
            GST_DEBUG_OBJECT (self,
                "ignoring sink, requested region is off-picture");
            goto disable_plane; /* nothing is in the visible range */
          }
          src_max_width -= src_x;
          src_max_height -= src_y;

          src_width = (guint) pad->src_width;
          /* in this case, the width of the region we take cannot be larger than
           * the region of the image itself. And since the default value is the
           * largest possible value (after the uint-casting), this automatically
           * is a default check also. */
          if (src_width > src_max_width)
            src_width = src_max_width;
          src_height = (guint) pad->src_height;
          if (src_height > src_max_height)
            src_height = src_max_height;
        }

        GST_TRACE_OBJECT (self,
            "set plane at (%i,%i) %ix%i sourcing at (%f,%f) %fx%f with alpha value 0x%X, blend mode %i, rotation %i, zpos %i",
            xpos, ypos, width, height, src_x / 65536., src_y / 65536.,
            src_width / 65536., src_height / 65536., pad->alpha, pad->blend,
            pad->rotation, pad->zorder);
        num_props = PROPS_PER_PLANE;
        if (!pad->zorder_mutable)
          --num_props;
        *(++count_props) = num_props;
        memcpy (prop_ids, pad->kms_ids, sizeof (pad->kms_ids));
        prop_ids += num_props;
        prop_vals[0] = fb_id;
        prop_vals[1] = self->crtc_id;
        prop_vals[2] = xpos;
        prop_vals[3] = ypos;
        prop_vals[4] = width;
        prop_vals[5] = height;
        prop_vals[6] = src_x;
        prop_vals[7] = src_y;
        prop_vals[8] = src_width;
        prop_vals[9] = src_height;
        prop_vals[10] = pad->alpha;
        prop_vals[11] = pad->blend;
        prop_vals[12] = pad->rotation;
        if (pad->zorder_mutable)
          prop_vals[13] = pad->zorder;
        prop_vals += num_props;
        continue;
      }
    }
  disable_plane:
    /* no buffer, disable the plane */
    *(++count_props) = 2;
    memcpy (prop_ids, pad->kms_ids, 2 * sizeof (*pad->kms_ids));
    prop_ids += 2;
    prop_vals[0] = 0;
    prop_vals[1] = 0;
    prop_vals += 2;
  }

  /* transaction fully prepared, go! */
  GST_TRACE_OBJECT (self, "Committing atomic properties");
  /* If the atomic commit fails for some mysterious reason, uncomment this; it
   * will output the detailed log (but note the sleep, as it takes a bit of time
   * to collect all the log, so only use it for one aggregation). */
#ifdef DEBUG_KMS
  {
    FILE *dbg = fopen ("/sys/module/drm/parameters/debug", "w");
    fprintf (dbg, "0x3DF");
    fclose (dbg);
    system ("dmesg -C");
  }
#endif
  result = drmIoctl (self->fd, DRM_IOCTL_MODE_ATOMIC, atomic);
#ifdef DEBUG_KMS
  if (DEBUG_KMS || result) {
    FILE *dbg;
    pid_t pid = fork ();

    if (pid == 0) {
      char *command[] = { "dmesg", "-w", NULL };
      execvp (*command, command);
      exit (1);
    }
    usleep (40000);
    kill (pid, SIGTERM);
    dbg = fopen ("/sys/module/drm/parameters/debug", "w");
    fprintf (dbg, "0");
    fclose (dbg);
    fflush (stdout);
  }
#endif
  GST_TRACE_OBJECT (self, "Commit result: %d", result);

  if (result) {
    GST_ERROR_OBJECT (self, "Atomic modesetting failed: %s (%d)",
        g_strerror (errno), errno);
    goto done;
  }

  self->need_modesetting = FALSE;

  /* now everything was committed to the driver. We need to wait until we get a
   * signal on the fence, then we're ready to use the data in the output buffer.
   * First make sure the fence was set appropriately. */
  if (out_sync_file == -1) {
    GST_ERROR_OBJECT (self, "Out sync file was not set properly");
    goto done;
  }
  /* We also need to wait for the VBLANK event, as we explicitly requested one.
   * (If we don't fetch the events, the kernel will sooner rather than later run
   * out of memory, as it cannot allocate new events. If we in turn don't even
   * request VBLANK - which should make sense, as we have the output fence -
   * display is stagnant.) */
  {
    struct drm_event_vblank ev;

    while (1) {
      do {
        if (read (self->fd, &ev, sizeof (ev)) != sizeof (ev)) {
          GST_ERROR_OBJECT (self, "Bad DRM event size");
          goto done;
        }
      } while (ev.base.type != DRM_EVENT_FLIP_COMPLETE);
      if (ev.crtc_id && ev.crtc_id != self->crtc_id)
        /* comment says crtc_id is 0 on older kernels */
        GST_WARNING_OBJECT (self, "Unexpected page flip for CRTC %d",
            ev.crtc_id);
      else
        break;
    }
    /* here we could in principle check ev.tv_sec/tv_usec to see if the flip was
     * acceptably fast and print a warning. */
  }

  /* Then wait for the fence signal */
  {
    struct pollfd poll_sync_file = {
      .fd = out_sync_file,
      .events = POLLIN,
      .revents = 0
    };

    poll (&poll_sync_file, 1, 3000);
    if (poll_sync_file.revents != POLLIN) {
      GST_ERROR_OBJECT (self, "Did not get a writeback within three seconds");
      goto done;
    }
  }

  if (self->src_needs_copy) {
    /* we need to copy from our dumb buffer to the output */
    GstVideoFrame inframe, outframe;
    gboolean success;

    if (!gst_video_frame_map (&inframe, &self->src_vinfo, self->src_buffer,
            GST_MAP_READ)) {
      GST_ERROR_OBJECT (self, "failed to map buffer");
      goto done;
    }
    if (!gst_video_frame_map (&outframe, &self->src_vinfo, outbuf,
            GST_MAP_WRITE)) {
      GST_ERROR_OBJECT (self, "failed to map buffer");
      gst_video_frame_unmap (&inframe);
      goto done;
    }
    success = gst_video_frame_copy (&outframe, &inframe);
    gst_video_frame_unmap (&outframe);
    gst_video_frame_unmap (&inframe);
    if (!success) {
      GST_ERROR_OBJECT (self, "failed to upload buffer");
      goto done;
    }
  }
  /* else outbuf was already backed by src_buffer */

  ret = GST_FLOW_OK;

done:
  gst_buffer_replace (&self->src_buffer, NULL);
  for (i = 0; i < bufi; ++i)
    gst_buffer_unref (bufs[i]);
  GST_OBJECT_UNLOCK (self);
  if (out_sync_file != -1)
    close (out_sync_file);

  GST_TRACE_OBJECT (self, "Leaving aggregation with status %d", ret);

  return ret;
}

static GstFlowReturn
gst_kms_compositor_create_src_buffer (GstVideoAggregator * vagg,
    GstBuffer ** outbuf)
{
  GstKMSCompositor *self = GST_KMS_COMPOSITOR (vagg);

  if (self->src_buffer) {
    GST_ERROR_OBJECT (self, "Previous output not processed yet");
    return GST_FLOW_ERROR;
  }
  /* in any case, we need to create some internal KMS memory where to put the
   * writeback data */
  if (!self->src_pool) {
    GST_ERROR_OBJECT (self, "No KMS pool configured");
    return GST_FLOW_ERROR;
  }
  if (gst_buffer_pool_acquire_buffer (self->src_pool, &self->src_buffer,
          NULL) != GST_FLOW_OK) {
    GST_ERROR_OBJECT (self, "failed to create output KMS buffer");
    return GST_FLOW_ERROR;
  }
  /* then we need to know about the output capabilities */
  if (!self->src_needs_copy) {
    /* output is fine with KMS memory or DMA and if !src_needs_copy, the
     * src_pool was already created to deliver DMABuf */
    *outbuf = gst_buffer_ref (self->src_buffer);
    return GST_FLOW_OK;
  } else {
    /* we also need to create a plain buffer */
    GstFlowReturn ret =
        GST_VIDEO_AGGREGATOR_CLASS (parent_class)->create_output_buffer (vagg,
        outbuf);
    if (ret != GST_FLOW_OK) {
      gst_buffer_replace (&self->src_buffer, NULL);
    }
    return ret;
  }
}

static GObject *
gst_kms_compositor_child_proxy_get_child_by_index (GstChildProxy * child_proxy,
    guint index)
{
  GstKMSCompositor *self = GST_KMS_COMPOSITOR (child_proxy);
  GObject *pad;

  GST_OBJECT_LOCK (self);
  pad = g_list_nth_data (GST_ELEMENT_CAST (self)->sinkpads, index);
  if (pad)
    gst_object_ref (pad);
  GST_OBJECT_UNLOCK (self);

  return pad;
}

static guint
gst_kms_compositor_child_proxy_get_children_count (GstChildProxy * child_proxy)
{
  GstKMSCompositor *self = GST_KMS_COMPOSITOR (child_proxy);
  guint count;

  GST_OBJECT_LOCK (self);
  count = GST_ELEMENT_CAST (self)->numsinkpads;
  GST_OBJECT_UNLOCK (self);
  GST_INFO_OBJECT (self, "Children Count: %d", count);

  return count;
}

static void
gst_kms_compositor_child_proxy_init (gpointer g_iface, gpointer iface_data)
{
  GstChildProxyInterface *iface = g_iface;

  iface->get_child_by_index = gst_kms_compositor_child_proxy_get_child_by_index;
  iface->get_children_count = gst_kms_compositor_child_proxy_get_children_count;
}

/* source-related */
static gboolean
gst_kms_compositor_ensure_src_caps (GstKMSCompositor * self,
    drmModeConnector * conn, drmModeRes * res)
{
  GstCaps *format_caps, *dimension_caps, *out_caps;
  guint32 i, j;

  assert (!self->allowed_src_caps);
  if (!conn)
    return FALSE;

  format_caps = gst_caps_new_empty ();
  if (!format_caps)
    return FALSE;
  {
    drmModeObjectProperties *props =
        drmModeObjectGetProperties (self->fd, conn->connector_id,
        DRM_MODE_OBJECT_CONNECTOR);

    for (i = 0; i < props->count_props; ++i) {
      /* we only need the name */
      struct drm_mode_get_property prop;

      prop.count_values = prop.count_enum_blobs = 0;
      prop.prop_id = props->props[i];
      if (!drmIoctl (self->fd, DRM_IOCTL_MODE_GETPROPERTY, &prop) &&
          !strcmp (prop.name, "WRITEBACK_PIXEL_FORMATS")) {
        drmModePropertyBlobPtr blob =
            drmModeGetPropertyBlob (self->fd, props->prop_values[i]);
        guint32 *pixel_formats = blob->data;
        guint32 count_pixel_formats = blob->length / sizeof (guint32);

        for (j = 0; j < count_pixel_formats; ++j) {
          GstVideoFormat fmt;
          GstCaps *caps;

          if (!pixel_formats[j])
            continue;           /* may be a modifier (perhaps?) */
          fmt = gst_video_format_from_drm (pixel_formats[j]);
          if (fmt == GST_VIDEO_FORMAT_UNKNOWN) {
            GST_INFO_OBJECT (self, "ignoring output format %" GST_FOURCC_FORMAT,
                GST_FOURCC_ARGS (pixel_formats[j]));
            continue;
          }
          caps = gst_caps_new_simple ("video/x-raw",
              "format", G_TYPE_STRING, gst_video_format_to_string (fmt), NULL);
          if (caps)
            format_caps = gst_caps_merge (format_caps, caps);
        }
        drmModeFreePropertyBlob (blob);
        break;
      }
    }
    drmModeFreeObjectProperties (props);
  }
  format_caps = gst_caps_simplify (format_caps);
  /* 3. also get all possible dimensions and framerates */
  dimension_caps = gst_caps_new_empty ();
  if (!dimension_caps) {
    gst_caps_unref (format_caps);
    return FALSE;
  }
  for (i = 0; i < conn->count_modes; ++i) {
    drmModeModeInfo *mode = &conn->modes[i];
    GstCaps *caps = gst_caps_new_simple ("video/x-raw",
        "width", G_TYPE_INT, mode->hdisplay,
        "height", G_TYPE_INT, mode->vdisplay,
        "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);

    if (caps)
      dimension_caps = gst_caps_merge (dimension_caps, caps);
  }
  dimension_caps = gst_caps_simplify (dimension_caps);
  /* 4. and obtain the intersection */
  out_caps = gst_caps_intersect (format_caps, dimension_caps);
  gst_caps_unref (format_caps);
  gst_caps_unref (dimension_caps);
  if (gst_caps_is_empty (out_caps)) {
    GST_DEBUG_OBJECT (self, "allowed output caps are empty");
    gst_caps_unref (out_caps);
    return FALSE;
  }
  /* 5. finally, if we can export DMABuf, then duplicate the caps and add the
   * memory option */
  if (self->has_prime_export) {
    guint n = gst_caps_get_size (out_caps);

    for (guint i = 0; i < n; ++i) {
      gst_caps_append_structure_full (out_caps,
          gst_structure_copy (gst_caps_get_structure (out_caps, i)),
          gst_caps_features_new_single (GST_CAPS_FEATURE_MEMORY_DMABUF));
    }
  }
  /* done */
  self->allowed_src_caps = gst_caps_simplify (out_caps);
  GST_DEBUG_OBJECT (self, "allowed output caps = %" GST_PTR_FORMAT,
      self->allowed_src_caps);

  return TRUE;
}

static GstCaps *
gst_kms_compositor_update_caps (GstVideoAggregator * vagg, GstCaps * filter)
{
  GstKMSCompositor *self = GST_KMS_COMPOSITOR (vagg);
  GstCaps *caps = self->allowed_src_caps;

  if (!caps)
    return NULL;

  GST_DEBUG_OBJECT (self, "Proposing caps %" GST_PTR_FORMAT, caps);

  if (filter)
    return gst_caps_intersect_full (caps, filter, GST_CAPS_INTERSECT_FIRST);
  else {
    return gst_caps_ref (caps);
    /* do we need this?
       GstCaps *out_caps = gst_caps_make_writable(caps)
       gst_caps_unref(caps);
       return out_caps */
  }
}

static GstCaps *
gst_kms_compositor_fixate_src_caps (GstAggregator * agg, GstCaps * caps)
{
  /* do not use the fixate implmentation of videoaggregator, which will resort
   * to the largest input frames. Our output size is determined by the chosen
   * mode. However, for the framerate, consider that the writeback adapter
   * itself has a certain framerate, i.e., this cannot be surpassed. But
   * individually, we cannot be faster than the fastest framerate (this should
   * actually be a simplification, we cannot be faster than the greatest common
   * divisor (in a sense): if one framerate is 4 Hz and the other 3 Hz, then we
   * don't get one frame every 1/4 second, but actually at 1/4, 1/3, 2/4, 2/3,
   * 3/4, 4/4 of a second, and to represent this, our framerate must be 12 Hz -
   * however, compositor also does the simplification and just uses the fastest
   * rate; in the end, we won't miss too much unless the framerates are really
   * this low). */
  gint best_fps_n = -1, best_fps_d = -1;
  gdouble best_fps = 0.;
  GList *l;
  GstCaps *ret;
  GstStructure *s;

  GST_OBJECT_LOCK (agg);
  for (l = GST_ELEMENT (agg)->sinkpads; l; l = l->next) {
    GstVideoAggregatorPad *vaggpad = GST_VIDEO_AGGREGATOR_PAD (l->data);
    gdouble cur_fps;
    gint fps_n, fps_d;

    if (gst_aggregator_pad_is_inactive (GST_AGGREGATOR_PAD (vaggpad)))
      continue;
    fps_n = GST_VIDEO_INFO_FPS_N (&vaggpad->info);
    fps_d = GST_VIDEO_INFO_FPS_D (&vaggpad->info);
    if (fps_d == 0)
      cur_fps = 0.0;
    else
      gst_util_fraction_to_double (fps_n, fps_d, &cur_fps);
    if (best_fps < cur_fps) {
      best_fps = cur_fps;
      best_fps_n = fps_n;
      best_fps_d = fps_d;
    }
  }
  GST_OBJECT_UNLOCK (agg);

  /* TODO: do we want to query vrefresh (or more precise, very detailed
   * calculation in the spirit of
   * https://github.com/egnor/pivid/blob/main/display_mode.cpp) of the chosen
   * mode? Then we could upper bound the output framerate to what is actually
   * feasible (though probably no input will be faster than KMS can write it
   * back...). And we could give a prediction in case there is no default. The
   * below is just copied from compositor, but with KMS, we could be way faster
   * than 25 fps... */
  if (best_fps_n <= 0 || best_fps_d <= 0 || best_fps == 0.0) {
    best_fps_n = 25;
    best_fps_d = 1;
    best_fps = 25.0;
  }

  ret = gst_caps_make_writable (caps);
  s = gst_caps_get_structure (ret, 0);
  gst_structure_fixate_field_nearest_fraction (s, "framerate", best_fps_n,
      best_fps_d);
  return gst_caps_fixate (ret);
}

static gboolean
gst_kms_compositor_negotiated_src_caps (GstAggregator * agg, GstCaps * caps)
{
  GstKMSCompositor *self = GST_KMS_COMPOSITOR (agg);
  drmModeConnector *conn = NULL;
  gboolean ret = FALSE;
  GstVideoInfo *vinfo;
  drmModeModeInfo *mode;
  guint32 i;

  GST_OBJECT_LOCK (self);

  if (self->conn_id <= 0) {
    GST_ERROR_OBJECT (self, "no connector set up");
    goto done;
  }
  GST_INFO_OBJECT (self, "configuring mode setting");
  vinfo = &self->src_vinfo;
  if (!gst_video_info_from_caps (vinfo, caps)) {
    GST_ERROR_OBJECT (self, "unable to get video info from caps");
    goto done;
  }
  /* we don't do the modesetting here, but just store the information so that it
   * can be passed on in the atomic transaction */
  conn = drmModeGetConnector (self->fd, self->conn_id);
  if (!conn) {
    GST_ERROR_OBJECT (self, "Could not find a valid monitor connector");
    goto done;
  }

  mode = NULL;
  for (i = 0; i < conn->count_modes; i++) {
    if (conn->modes[i].vdisplay == GST_VIDEO_INFO_HEIGHT (vinfo) &&
        conn->modes[i].hdisplay == GST_VIDEO_INFO_WIDTH (vinfo)) {
      mode = &conn->modes[i];
      break;
    }
  }
  if (!mode) {
    GST_ERROR_OBJECT (self, "cannot find appropriate mode");
    goto done;
  }

  if (self->mode_id) {
    drmModeDestroyPropertyBlob (self->fd, self->mode_id);
    self->mode_id = 0;
  }
  if (drmModeCreatePropertyBlob (self->fd, mode, sizeof (*mode),
          &self->mode_id)) {
    GST_ERROR_OBJECT (self, "cannot create mode blob");
    goto done;
  }

  self->need_modesetting = TRUE;

  if (self->force_dma) {
    self->src_needs_copy = FALSE;
  } else {
    GstCapsFeatures *features = gst_caps_get_features (caps, 0);

    if (features) {
      if (gst_caps_features_contains (features, GST_CAPS_FEATURE_MEMORY_DMABUF)) {
        GST_DEBUG_OBJECT (self, "Negotiated with DMA memory caps");
        self->src_needs_copy = FALSE;
      } else {
        self->src_needs_copy = TRUE;
      }
    } else {
      self->src_needs_copy = TRUE;
    }
  }

  ret = TRUE;

done:
  if (conn)
    drmModeFreeConnector (conn);

  GST_OBJECT_UNLOCK (self);

  return ret
      && GST_AGGREGATOR_CLASS (parent_class)->negotiated_src_caps (agg, caps);
}

static gboolean
gst_kms_compositor_decide_allocation (GstAggregator * agg, GstQuery * query)
{
  GstKMSCompositor *self = GST_KMS_COMPOSITOR (agg);
  GstCaps *caps;
  GstVideoInfo vinfo;

  gst_query_parse_allocation (query, &caps, NULL);      /* we give a pool in any case */
  if (!caps) {
    GST_ERROR_OBJECT (self, "No output caps");
    return FALSE;
  }
  if (!gst_video_info_from_caps (&vinfo, caps)) {
    GST_ERROR_OBJECT (self, "Invalid caps");
    return FALSE;
  }

  if (self->src_pool) {
    GstStructure *config;

    /* we can re-use the pool, we just need to re-configure it */
    gst_buffer_pool_set_active (self->src_pool, FALSE);
    config = gst_buffer_pool_get_config (self->src_pool);
    if (self->src_needs_copy) {
      /* if the PRIME_EXPORT option is set, we need to remove it. Unfortunately,
       * there's no built-in method and the interals are hidden, so start
       * over. */
      if (gst_buffer_pool_config_has_option (config,
              GST_BUFFER_POOL_OPTION_KMS_PRIME_EXPORT))
        goto acquire_buffer;
    } else if (self->has_prime_export)
      gst_buffer_pool_config_add_option (config,
          GST_BUFFER_POOL_OPTION_KMS_PRIME_EXPORT);
    gst_buffer_pool_config_set_params (config, caps, vinfo.size, 2, 0);
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);
    if (!gst_buffer_pool_set_config (self->src_pool, config)) {
      GST_ERROR_OBJECT (self, "failed to re-configure buffer pool");
      gst_object_replace ((GstObject **) & self->src_pool, NULL);
    }
  }
  if (!self->src_pool) {
  acquire_buffer:
    self->src_pool =
        gst_kms_compositor_create_pool (GST_OBJECT (self), self, caps,
        vinfo.size);
    if (!self->src_pool)
      return GST_FLOW_ERROR;    /* message already in create_pool */
    if (self->has_prime_export && !self->src_needs_copy) {
      GstStructure *config = gst_buffer_pool_get_config (self->src_pool);

      gst_buffer_pool_config_add_option (config,
          GST_BUFFER_POOL_OPTION_KMS_PRIME_EXPORT);
      if (!gst_buffer_pool_set_config (self->src_pool, config)) {
        GST_WARNING_OBJECT (self, "failed to activate prime export");
        self->src_needs_copy = TRUE;
      }
    }
  }
  if (!gst_buffer_pool_set_active (self->src_pool, TRUE)) {
    GST_ERROR_OBJECT (self, "failed to activate buffer pool");
    gst_object_replace ((GstObject **) & self->src_pool, NULL);
    return FALSE;
  }

  if (self->src_needs_copy) {
    /* but the src_pool is only a helper for aggregation, we really need to
     * return a general pool */
    return GST_AGGREGATOR_CLASS (parent_class)->decide_allocation (agg, query);
  } else {
    /* in this case, we can use the src pool directly. Adding it increases the
     * refs. */
    if (gst_query_get_n_allocation_pools (query) > 0)
      gst_query_set_nth_allocation_pool (query, 0, self->src_pool, vinfo.size,
          2, 0);
    else
      gst_query_add_allocation_pool (query, self->src_pool, vinfo.size, 2, 0);
    return TRUE;
  }
}

/* sink-related */
static gboolean
gst_kms_compositor_sink_query (GstAggregator * agg, GstAggregatorPad * bpad,
    GstQuery * query)
{
  GstKMSCompositorPad *pad = GST_KMS_COMPOSITOR_PAD (bpad);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *filter, *result;

      assert (pad->allowed_caps);
      gst_query_parse_caps (query, &filter);
      if (filter) {
        result =
            gst_caps_intersect_full (pad->allowed_caps, filter,
            GST_CAPS_INTERSECT_FIRST);
      } else {
        result = gst_caps_ref (pad->allowed_caps);
      }
      gst_query_set_caps_result (query, result);
      return TRUE;
    }
    case GST_QUERY_ACCEPT_CAPS:
    {
      GstCaps *caps;

      assert (pad->allowed_caps);
      gst_query_parse_accept_caps (query, &caps);
      gst_query_set_accept_caps_result (query,
          gst_caps_can_intersect (pad->allowed_caps, caps));
      return TRUE;
    }
    default:                   /* to avoid warnings */
  }
  return GST_AGGREGATOR_CLASS (parent_class)->sink_query (agg, bpad, query);
}

static gboolean
gst_kms_compositor_sink_event (GstAggregator * agg, GstAggregatorPad * bpad,
    GstEvent * event)
{
  GstKMSCompositorPad *pad = GST_KMS_COMPOSITOR_PAD (bpad);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      GST_OBJECT_LOCK (pad);
      if (pad->pool) {
        gst_buffer_pool_set_active (pad->pool, FALSE);
        gst_object_unref (pad->pool);
        pad->pool = NULL;
      }
      if (!gst_video_info_from_caps (&pad->vinfo, caps)) {
        GST_OBJECT_UNLOCK (pad);
        gst_event_unref (event);
        GST_ERROR_OBJECT (pad, "unable to get video info from caps");
        return FALSE;
      }
      GST_OBJECT_UNLOCK (pad);

      GST_DEBUG_OBJECT (pad, "negotiated caps = %" GST_PTR_FORMAT, caps);
      break;                    /* we still want to propagate up */
    }
    default:
  }
  return GST_AGGREGATOR_CLASS (parent_class)->sink_event (agg, bpad, event);
}

static gboolean
gst_kms_compositor_propose_allocation (GstAggregator * agg,
    GstAggregatorPad * pad, GstQuery * decide_query, GstQuery * query)
{
  GstKMSCompositor *self = GST_KMS_COMPOSITOR (agg);
  GstCaps *caps;
  gboolean need_pool;
  GstVideoInfo vinfo;
  gsize size;
  GstBufferPool *pool;

  GST_DEBUG_OBJECT (self, "propose allocation");

  gst_query_parse_allocation (query, &caps, &need_pool);
  if (!caps) {
    GST_DEBUG_OBJECT (self, "no caps specified");
    return FALSE;
  }
  if (!gst_video_info_from_caps (&vinfo, caps)) {
    GST_DEBUG_OBJECT (self, "invalid caps specified");
    return FALSE;
  }

  size = GST_VIDEO_INFO_SIZE (&vinfo);

  pool = NULL;
  if (need_pool) {
    pool = gst_kms_compositor_create_pool (GST_OBJECT (self), self, caps, size);
    if (!pool)
      return FALSE;             /* Already warned in create_pool */

    /* Only export for pool used upstream */
    if (self->has_prime_export) {
      GstStructure *config = gst_buffer_pool_get_config (pool);

      gst_buffer_pool_config_add_option (config,
          GST_BUFFER_POOL_OPTION_KMS_PRIME_EXPORT);
      gst_buffer_pool_set_config (pool, config);
    }
  }

  gst_query_add_allocation_pool (query, pool, size, 2, 0);
  if (pool)
    gst_object_unref (pool);

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);
  gst_query_add_allocation_meta (query, GST_VIDEO_CROP_META_API_TYPE, NULL);

  return TRUE;
}

/* kms-related */
static gboolean
gst_kms_compositor_set_crtc (GstKMSCompositor * self, drmModeRes * res,
    drmModeConnector * conn)
{
  guint32 i, crtcs_for_connector;

  assert (self->fd);
  /* try to find an active encoder for the current connector */
  if (conn->encoder_id) {
    struct drm_mode_get_encoder enc;

    enc.encoder_id = conn->encoder_id;
    if (!drmIoctl (self->fd, DRM_IOCTL_MODE_GETENCODER, &enc)) {
      for (i = 0; i < res->count_crtcs; ++i) {
        if (enc.crtc_id == res->crtcs[i]) {
          self->crtc_id = enc.crtc_id;
          self->pipe = i;
          return TRUE;
        }
      }
    }
  }
  /* but if it didn't work, pick the first possible crtc */
  crtcs_for_connector = 0;
  for (i = 0; i < conn->count_encoders; ++i) {
    struct drm_mode_get_encoder enc;

    enc.encoder_id = conn->encoders[i];
    if (!drmIoctl (self->fd, DRM_IOCTL_MODE_GETENCODER, &enc)) {
      crtcs_for_connector |= enc.possible_crtcs;
    }
  }
  if (crtcs_for_connector) {
    self->pipe = ffs (crtcs_for_connector) - 1;
    self->crtc_id = res->crtcs[self->pipe];
    return TRUE;
  }

  return FALSE;
}

static drmModeConnector *
gst_kms_compositor_set_connector (GstKMSCompositor * self, drmModeRes * res)
{
  guint32 i;

  assert (self->fd && !self->crtc_id);
  /* Getting the connector accurately with all information is tricky, plus we
   * return it, so it must be heap-allocated - best use drmModeGetConnector
   * which does all the work of repeating and ensuring no race condition
   * happened. However, we might need to probe a couple of connectors until we
   * found the correct one, and we only need the full details once the connector
   * type is verified. Therefore, use stack-allocated simple IOCTLs first (these
   * are force-probing, as they are the first call); afterwards, use
   * non-force-probing details heap-allocated calls. */
  if (self->conn_id == -1) {
    /* try to find the first active one */
    for (i = 0; i < res->count_connectors; ++i) {
      struct drm_mode_get_connector conn;

      conn.connector_id = res->connectors[i];
      conn.count_props = conn.count_encoders = conn.count_modes = 0;    /* force-probe */
      if (!drmIoctl (self->fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn)) {
        if (conn.connector_type == DRM_MODE_CONNECTOR_WRITEBACK) {
          /* now we need detailed data, but no need to force-probe again */
          drmModeConnector *wb_conn =
              drmModeGetConnectorCurrent (self->fd, conn.connector_id);

          if (wb_conn) {
            if (gst_kms_compositor_set_crtc (self, res, wb_conn)) {
              struct drm_mode_crtc crtc;

              crtc.crtc_id = self->crtc_id;
              if (!drmIoctl (self->fd, DRM_IOCTL_MODE_GETCRTC, &crtc)) {
                if (crtc.fb_id) {
                  self->conn_id = conn.connector_id;
                  GST_INFO_OBJECT (self, "Found active connector %d",
                      conn.connector_id);
                  return wb_conn;
                }
              }
            }
            drmModeFreeConnector (wb_conn);
          }
        }
      }
    }
    /* but if there's no active one, just find the first */
    for (guint32 i = 0; i < res->count_connectors; ++i) {
      /* all connectors were already force-probed */
      struct drm_mode_get_connector conn;
      struct drm_mode_modeinfo stack_mode;

      conn.connector_id = res->connectors[i];
      conn.count_props = conn.count_encoders = 0;
      conn.count_modes = 1;
      conn.modes_ptr = (guint64) & stack_mode;
      if (!drmIoctl (self->fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn) &&
          conn.connector_type == DRM_MODE_CONNECTOR_WRITEBACK) {
        drmModeConnector *wb_conn =
            drmModeGetConnectorCurrent (self->fd, conn.connector_id);

        if (wb_conn) {
          if (gst_kms_compositor_set_crtc (self, res, wb_conn)) {
            self->conn_id = conn.connector_id;
            GST_INFO_OBJECT (self, "Found first available connector %d",
                conn.connector_id);
            return wb_conn;
          }
          drmModeFreeConnector (wb_conn);
        }
      }
    }
  } else {
    drmModeConnector *conn = drmModeGetConnector (self->fd, self->conn_id);

    if (conn) {
      if (conn->connector_type == DRM_MODE_CONNECTOR_WRITEBACK) {
        if (gst_kms_compositor_set_crtc (self, res, conn)) {
          GST_INFO_OBJECT (self, "Assigned user-defined connector %d",
              conn->connector_id);
          return conn;
        }
      } else {
        GST_ERROR_OBJECT (self,
            "User-defined connector %d is not a writeback connector",
            conn->connector_id);
      }
      drmModeFreeConnector (conn);
    }
  }
  self->crtc_id = 0;            /* failure is complete failure */
  return NULL;
}

static gboolean
gst_kms_compositor_update_planes_for_crtc (GstKMSCompositor * self,
    drmModeRes * res)
{
  /* on input, the compositor must not have any sink pad; they are created by
   * this function. */
  gboolean ret = FALSE;
  drmModePlaneRes *pres = NULL;
  guint32 plane_mask;
  GstPadTemplate *templ;
  int found;                    /* hack, remove */
  guint32 i, j;

  if (GST_ELEMENT (self)->numsinkpads) {
    GST_ERROR_OBJECT (self, "Object already initialized");
    goto done;
  }

  plane_mask = 1 << self->pipe;

  pres = drmModeGetPlaneResources (self->fd);
  if (!pres)
    goto fail;

  templ =
      gst_element_class_get_pad_template (g_type_class_peek
      (GST_TYPE_KMS_COMPOSITOR), "sink_%u");
  if (!templ)
    goto fail;

  /* TODO: just a quick hack to expose the first two planes, hardcoded ugliness.
   * Replace according to decision in merge request. */
  found = 0;
  for (i = 0; found < 2 && i < pres->count_planes; ++i) {
    drmModePlane *plane = drmModeGetPlane (self->fd, pres->planes[i]);

    if (plane->possible_crtcs & plane_mask) {
      drmModeObjectProperties *props = drmModeObjectGetProperties (self->fd,
          plane->plane_id, DRM_MODE_OBJECT_PLANE);

      if (props) {
        gboolean found_type = FALSE;

        for (j = 0; !found_type && j < props->count_props; ++j) {
          /* we only need the name */
          struct drm_mode_get_property prop;

          prop.count_values = prop.count_enum_blobs = 0;
          prop.prop_id = props->props[j];
          if (!drmIoctl (self->fd, DRM_IOCTL_MODE_GETPROPERTY, &prop)
              && !strcmp (prop.name, "type")) {
            guint64 type = props->prop_values[j];

            if (type == DRM_PLANE_TYPE_OVERLAY
                || type == DRM_PLANE_TYPE_PRIMARY) {
              GstPad *pad = gst_pad_new_from_template (templ, NULL);

              if (pad) {
                if (gst_kms_compositor_pad_assign_plane (GST_KMS_COMPOSITOR_PAD
                        (pad), self->fd, res, props, plane)) {
                  gst_element_add_pad ((GstElement *) self, pad);
                  gst_child_proxy_child_added (GST_CHILD_PROXY ((GstElement *)
                          self), G_OBJECT (pad), GST_OBJECT_NAME (pad));
                  ++found;
                } else {
                  gst_object_unref (pad);
                }
              }
            }
            found_type = TRUE;
          }
        }
        drmModeFreeObjectProperties (props);
        if (!found_type)
          GST_WARNING_OBJECT (self,
              "Plane %d is missing property type, ignored", plane->plane_id);
      }
    }
    drmModeFreePlane (plane);
  }
  ret = TRUE;

  goto done;

fail:
  GST_ERROR_OBJECT (self, "Unable to obtain plane information");
done:
  if (pres)
    drmModeFreePlaneResources (pres);

  return ret;
}

void
gst_kms_compositor_ensure_kms_allocator (GstKMSCompositor * self)
{
  if (self->allocator)
    return;
  self->allocator = gst_kms_allocator_new (self->fd);
}

GstBufferPool *
gst_kms_compositor_create_pool (GstObject * self, GstKMSCompositor * comp,
    GstCaps * caps, gsize size)
{
  GstBufferPool *pool = gst_kms_buffer_pool_new ();
  GstStructure *config;

  if (!pool) {
    GST_ERROR_OBJECT (self, "failed to create buffer pool");
    return NULL;
  }

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps, size, 2, 0);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  gst_kms_compositor_ensure_kms_allocator (comp);
  gst_buffer_pool_config_set_allocator (config, comp->allocator, NULL);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR_OBJECT (self, "failed to set config");
    gst_object_unref (pool);
    return NULL;
  }

  return pool;
}

static gboolean
gst_kms_compositor_start (GstAggregator * agg)
{
  GstKMSCompositor *self = GST_KMS_COMPOSITOR (agg);
  drmModeRes *res;
  drmModeConnector *conn;
  gboolean ret;
  GList *l;

  assert (!self->mode_id);

  /* open our own internal device fd if application did not supply its own */
  if (self->is_internal_fd) {
    if (self->devname || self->bus_id)
      self->fd = drmOpen (self->devname, self->bus_id);
    else
      self->fd = kms_open (&self->devname);
  }

  if (self->fd < 0) {
    GST_ERROR_OBJECT (self, "Could not open DRM module %s, error %s (%d)",
        GST_STR_NULL (self->devname), g_strerror (errno), errno);
    return FALSE;
  }

  res = NULL;
  conn = NULL;
  ret = FALSE;

  log_drm_version (GST_OBJECT (self), self->fd, self->devname);
  if (!get_drm_caps (GST_OBJECT (self), self->fd,
          &self->has_prime_import, &self->has_prime_export, NULL))
    goto done;

  if (drmSetClientCap (self->fd, DRM_CLIENT_CAP_ATOMIC, 1) ||
      drmSetClientCap (self->fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) ||
      drmSetClientCap (self->fd, DRM_CLIENT_CAP_WRITEBACK_CONNECTORS, 1)) {
    GST_ERROR_OBJECT (self, "Could not set required capabilities");
    goto done;
  }

  self->resources = res = drmModeGetResources (self->fd);
  if (!res) {
    GST_ERROR_OBJECT (self, "drmModeGetResources failed, reason %s (%d)",
        g_strerror (errno), errno);
    goto done;
  }

  conn = gst_kms_compositor_set_connector (self, res);  /* this also sets the CRTC */
  if (!conn) {
    GST_ERROR_OBJECT (self, "Could not find a valid writeback connector");
    goto done;
  }

  if (!gst_kms_compositor_update_planes_for_crtc (self, res)) {
    GST_ERROR_OBJECT (self, "Could not find planes for crtc");
    goto reset_planes;
  }

  if (!gst_kms_compositor_ensure_src_caps (self, conn, res)) {
    GST_ERROR_OBJECT (self, "Could not get allowed output caps");
    goto reset_planes;
  }
  /* we need modesetting, but we don't know yet to which mode. So by setting
   * this to FALSE, no modesetting will be done unless the caps were determined
   * correctly. Which most likely means that the connector won't be activated
   * and therefore aggregation will fail, as it should. */
  self->need_modesetting = FALSE;

  ret = TRUE;
  goto done;

reset_planes:
  /* maybe some pads were already created? */
  for (l = GST_ELEMENT (self)->sinkpads; l;) {
    GList *next = l->next;
    GstPad *pad = l->data;

    GST_DEBUG_OBJECT (self, "release pad %s:%s", GST_DEBUG_PAD_NAME (pad));
    gst_child_proxy_child_removed (GST_CHILD_PROXY (self), G_OBJECT (pad),
        GST_OBJECT_NAME (pad));
    gst_element_remove_pad ((GstElement *) self, pad);
    l = next;
  }
done:
  if (conn)
    drmModeFreeConnector (conn);
  if (!ret && self->fd >= 0) {
    if (res)
      drmModeFreeResources (res);
    self->resources = NULL;
    if (self->is_internal_fd)
      drmClose (self->fd);
    self->fd = -1;
  }

  return ret && GST_AGGREGATOR_CLASS (parent_class)->start (agg);
}

static gboolean
gst_kms_compositor_stop (GstAggregator * agg)
{
  GstKMSCompositor *self = GST_KMS_COMPOSITOR (agg);
  GList *l;

  if (self->mode_id) {
    drmModeDestroyPropertyBlob (self->fd, self->mode_id);
    self->mode_id = 0;
  }
  if (self->resources) {
    drmModeFreeResources (self->resources);
    self->resources = NULL;
  }
  self->crtc_id = 0;

  for (l = GST_ELEMENT (self)->sinkpads; l;) {
    GList *next = l->next;
    GstPad *pad = l->data;

    GST_DEBUG_OBJECT (self, "release pad %s:%s", GST_DEBUG_PAD_NAME (pad));
    gst_child_proxy_child_removed (GST_CHILD_PROXY (self), G_OBJECT (pad),
        GST_OBJECT_NAME (pad));
    gst_element_remove_pad ((GstElement *) self, pad);
    l = next;
  }
  gst_caps_replace (&self->allowed_src_caps, NULL);
  gst_object_replace ((GstObject **) & self->src_pool, NULL);
  if (self->allocator)
    gst_kms_allocator_clear_cache (self->allocator);
  gst_object_replace ((GstObject **) & self->allocator, NULL);

  if (self->fd >= 0) {
    if (self->is_internal_fd)
      drmClose (self->fd);
    self->fd = -1;
  }

  return GST_AGGREGATOR_CLASS (parent_class)->stop (agg);
}

/* properties */
static void
gst_kms_compositor_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstKMSCompositor *self = GST_KMS_COMPOSITOR (object);

  GST_DEBUG_OBJECT (self, "get_property");

  switch (prop_id) {
    case PROP_DRIVER_NAME:
      g_value_set_string (value, self->devname);
      break;
    case PROP_BUS_ID:
      g_value_set_string (value, self->bus_id);
      break;
    case PROP_CONNECTOR_ID:
      g_value_set_int (value, self->conn_id);
      break;
    case PROP_CONNECTOR_PROPS:
      gst_value_set_structure (value, self->connector_props);
      break;
    case PROP_FD:
      g_value_set_int (value, self->fd);
      break;
    case PROP_FORCE_DMA:
      g_value_set_boolean (value, self->force_dma);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_kms_compositor_validate_and_set_external_fd (GstKMSCompositor * self,
    gint fd)
{
  if (self->devname) {
    GST_WARNING_OBJECT (self, "Can't set fd... %s already set.",
        g_param_spec_get_name (g_properties[PROP_DRIVER_NAME]));
    return;
  }

  if (self->bus_id) {
    GST_WARNING_OBJECT (self, "Can't set fd... %s already set.",
        g_param_spec_get_name (g_properties[PROP_BUS_ID]));
    return;
  }

  if (self->fd >= 0) {
    GST_WARNING_OBJECT (self, "Can't set fd... it is already set.");
    return;
  }

  if (fd >= 0) {
    self->devname = drmGetDeviceNameFromFd (fd);
    if (!self->devname) {
      GST_WARNING_OBJECT (self, "Failed to verify fd is a DRM fd.");
      return;
    }

    self->fd = fd;
    self->is_internal_fd = FALSE;
  }
}

static void
gst_kms_compositor_invalidate_external_fd (GstKMSCompositor * self,
    GParamSpec * pspec)
{
  if (self->is_internal_fd)
    return;

  GST_WARNING_OBJECT (self, "Unsetting fd... %s has priority.",
      g_param_spec_get_name (pspec));

  self->fd = -1;
  self->is_internal_fd = TRUE;
}

static void
gst_kms_compositor_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstKMSCompositor *self = GST_KMS_COMPOSITOR (object);

  GST_DEBUG_OBJECT (self, "set_property");

  /* TODO: only allow changes when not running */
  switch (prop_id) {
    case PROP_DRIVER_NAME:
      gst_kms_compositor_invalidate_external_fd (self, pspec);
      g_free (self->devname);
      self->devname = g_value_dup_string (value);
      break;
    case PROP_BUS_ID:
      gst_kms_compositor_invalidate_external_fd (self, pspec);
      g_free (self->bus_id);
      self->bus_id = g_value_dup_string (value);
      break;
    case PROP_CONNECTOR_ID:
      self->conn_id = g_value_get_int (value);
      break;
    case PROP_CONNECTOR_PROPS:
    {
      const GstStructure *s = gst_value_get_structure (value);

      g_clear_pointer (&self->connector_props, gst_structure_free);

      if (s)
        self->connector_props = gst_structure_copy (s);

      break;
    }
    case PROP_FD:
      gst_kms_compositor_validate_and_set_external_fd (self,
          g_value_get_int (value));
      break;
    case PROP_FORCE_DMA:
      self->force_dma = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* constructor and destructor */
static void
gst_kms_compositor_finalize (GObject * object)
{
  GstKMSCompositor *self = GST_KMS_COMPOSITOR (object);

  GST_DEBUG_OBJECT (self, "finalize");

  g_free ((guint32 *) self->atomic.objs_ptr);
  g_free ((guint32 *) self->atomic.count_props_ptr);
  g_free ((guint32 *) self->atomic.props_ptr);
  g_free ((guint64 *) self->atomic.prop_values_ptr);
  g_clear_pointer (&self->devname, g_free);
  g_clear_pointer (&self->bus_id, g_free);
  g_clear_pointer (&self->connector_props, gst_structure_free);

  G_OBJECT_CLASS (gst_kms_compositor_parent_class)->finalize (object);
}

static void
gst_kms_compositor_init (GstKMSCompositor * self)
{
  self->fd = -1;
  self->is_internal_fd = TRUE;
  self->conn_id = -1;
  assert (!self->atomic.objs_ptr);
  /* self->atomic.objs_ptr = NULL; */
  gst_video_info_init (&self->src_vinfo);
}

static void
gst_kms_compositor_class_init (GstKMSCompositorClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstAggregatorClass *agg_class = GST_AGGREGATOR_CLASS (klass);
  GstVideoAggregatorClass *videoaggregator_class =
      GST_VIDEO_AGGREGATOR_CLASS (klass);

  gst_element_class_set_static_metadata (element_class, "KMS video compositor",
      "Filter/Editor/Video/Compositor", GST_PLUGIN_DESC,
      "Benjamin Desef <projekter-git@yahoo.de>");

  gobject_class->set_property = gst_kms_compositor_set_property;
  gobject_class->get_property = gst_kms_compositor_get_property;
  gobject_class->finalize = gst_kms_compositor_finalize;

  {
    GstCaps *caps = gst_kms_sink_caps_template_fill ();
    guint n, i;

    n = gst_caps_get_size (caps);
    for (i = 0; i < n; ++i) {
      gst_caps_append_structure_full (caps,
          gst_structure_copy (gst_caps_get_structure (caps, i)),
          gst_caps_features_new_single (GST_CAPS_FEATURE_MEMORY_DMABUF));
    }
    gst_element_class_add_pad_template (element_class,
        gst_pad_template_new_with_gtype ("sink_%u", GST_PAD_SINK,
            GST_PAD_SOMETIMES, caps, GST_TYPE_KMS_COMPOSITOR_PAD));
    gst_element_class_add_pad_template (element_class,
        gst_pad_template_new_with_gtype ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
            caps, GST_TYPE_AGGREGATOR_PAD));
    gst_caps_unref (caps);
  }

  agg_class->fixate_src_caps = gst_kms_compositor_fixate_src_caps;
  agg_class->negotiated_src_caps =
      GST_DEBUG_FUNCPTR (gst_kms_compositor_negotiated_src_caps);
  agg_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_kms_compositor_propose_allocation);
  agg_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_kms_compositor_decide_allocation);
  agg_class->sink_query = GST_DEBUG_FUNCPTR (gst_kms_compositor_sink_query);
  agg_class->sink_event = GST_DEBUG_FUNCPTR (gst_kms_compositor_sink_event);
  agg_class->start = GST_DEBUG_FUNCPTR (gst_kms_compositor_start);
  agg_class->stop = GST_DEBUG_FUNCPTR (gst_kms_compositor_stop);
  videoaggregator_class->aggregate_frames =
      GST_DEBUG_FUNCPTR (gst_kms_compositor_aggregate_frames);
  videoaggregator_class->update_caps =
      GST_DEBUG_FUNCPTR (gst_kms_compositor_update_caps);
  videoaggregator_class->create_output_buffer =
      GST_DEBUG_FUNCPTR (gst_kms_compositor_create_src_buffer);

  /**
   * kmscompositor:driver-name:
   *
   * If you have a system with multiple GPUs, you can choose which GPU
   * to use setting the DRM device driver name. Otherwise, the first
   * one from an internal list is used.
   */
  g_properties[PROP_DRIVER_NAME] =
      g_param_spec_string ("driver-name", "device name",
      "DRM device driver name", NULL,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);

  /**
   * kmscompositor:bus-id:
   *
   * If you have a system with multiple displays for the same driver-name,
   * you can choose which display to use by setting the DRM bus ID. Otherwise,
   * the driver decides which one.
   */
  g_properties[PROP_BUS_ID] =
      g_param_spec_string ("bus-id", "Bus ID", "DRM bus ID", NULL,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);

  /**
   * kmscompositor:connector-id:
   *
   * By default the first writeback connector is used, but if
   * multiple are available, another one may be specified.
   */
  g_properties[PROP_CONNECTOR_ID] = g_param_spec_int ("connector-id",
      "Connector ID", "DRM connector id for output", -1, G_MAXINT32, -1,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);

  /**
   * kmscompositor:connector-properties:
   *
   * Additional properties for the connector. Keys are strings and values
   * unsigned 64 bits integers.
   */
  g_properties[PROP_CONNECTOR_PROPS] =
      g_param_spec_boxed ("connector-properties", "Connector Properties",
      "Additional properties for the connector",
      GST_TYPE_STRUCTURE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * kmscompositor:fd:
   *
   * You can supply your own DRM file descriptor.  By default, the sink will
   * open its own DRM file descriptor.
   */
  g_properties[PROP_FD] =
      g_param_spec_int ("fd", "File Descriptor",
      "DRM file descriptor", -1, G_MAXINT, -1,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);

  /**
   * kmscompositor:force-dma:
   *
   * Forces the source to use DMA memory. This is useful if the subsequent
   * elements don't announce their DMA capabilities.
   */
  g_properties[PROP_FORCE_DMA] =
      g_param_spec_boolean ("force-dma", "Force source DMA memory",
      "Corresponds to output-io-mode=dmabuf", FALSE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class, PROP_N, g_properties);
}
