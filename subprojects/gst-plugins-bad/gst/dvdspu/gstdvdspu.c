/* GStreamer DVD Sub-Picture Unit
 * Copyright (C) 2007 Fluendo S.A. <info@fluendo.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */
/**
 * SECTION:element-dvdspu
 * @title: dvdspu
 *
 * DVD sub picture overlay element.
 *
 * ## Example launch line
 * |[
 * FIXME: gst-launch-1.0 ...
 * ]| FIXME: description for the sample launch pipeline
 *
 */
#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib/gi18n-lib.h>
#include <gst/video/video.h>
#include <gst/video/video-overlay-composition.h>
#include <gst/video/gstvideometa.h>

#include <string.h>

#include <gst/gst.h>

#include "gstdvdspu.h"

GST_DEBUG_CATEGORY (dvdspu_debug);
#define GST_CAT_DEFAULT dvdspu_debug

GstDVDSPUDebugFlags dvdspu_debug_flags;

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

static GstStaticPadTemplate subpic_sink_factory =
    GST_STATIC_PAD_TEMPLATE ("subpicture",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("subpicture/x-dvd; subpicture/x-pgs")
    );

static gboolean dvd_spu_element_init (GstPlugin * plugin);

#define gst_dvd_spu_parent_class parent_class
G_DEFINE_TYPE (GstDVDSpu, gst_dvd_spu, GST_TYPE_SUB_OVERLAY);
GST_ELEMENT_REGISTER_DEFINE_CUSTOM (dvdspu, dvd_spu_element_init);

static void gst_dvd_spu_dispose (GObject * object);
static void gst_dvd_spu_finalize (GObject * object);

static gboolean gst_dvd_spu_set_format (GstSubOverlay * suboverlay,
    GstCaps * caps);
static gboolean gst_dvd_spu_set_format_video (GstSubOverlay * suboverlay,
    GstCaps * caps, GstVideoInfo * info, gint window_width, gint window_height);
static GstFlowReturn gst_dvd_spu_handle_buffer (GstSubOverlay * sub_overlay,
    GstBuffer * buf);
static void gst_dvd_spu_advance (GstSubOverlay * sub_overlay,
    GstBuffer * buffer, GstClockTime run_ts, GstClockTime run_ts_end);
static void gst_dvd_spu_render (GstSubOverlay * sub_overlay, GstBuffer * buf);
static gboolean gst_dvd_spu_start (GstSubOverlay * overlay);
static gboolean gst_dvd_spu_stop (GstSubOverlay * overlay);
static gboolean gst_dvd_spu_flush (GstSubOverlay * overlay);
static gboolean gst_dvd_spu_video_sink_event (GstSubOverlay * overlay,
    GstEvent * event);
static gboolean gst_dvd_spu_sub_sink_event (GstSubOverlay * overlay,
    GstEvent * event);

static void gst_dvd_spu_check_still_updates (GstDVDSpu * dvdspu);

static void gst_dvd_spu_clear (GstDVDSpu * dvdspu);
static void gst_dvd_spu_flush_spu_info (GstDVDSpu * dvdspu,
    gboolean process_events);

static void
gst_dvd_spu_class_init (GstDVDSpuClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstSubOverlayClass *gstsuboverlay_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstsuboverlay_class = (GstSubOverlayClass *) klass;

  gobject_class->dispose = (GObjectFinalizeFunc) gst_dvd_spu_dispose;
  gobject_class->finalize = (GObjectFinalizeFunc) gst_dvd_spu_finalize;

  gst_sub_overlay_class_add_pad_templates (gstsuboverlay_class, "video",
      NULL, NULL, NULL);
  gst_element_class_add_static_pad_template (gstelement_class,
      &subpic_sink_factory);

  gstsuboverlay_class->start = gst_dvd_spu_start;
  gstsuboverlay_class->stop = gst_dvd_spu_stop;
  gstsuboverlay_class->flush = gst_dvd_spu_flush;
  gstsuboverlay_class->set_format = gst_dvd_spu_set_format;
  gstsuboverlay_class->set_format_video = gst_dvd_spu_set_format_video;
  gstsuboverlay_class->handle_buffer = gst_dvd_spu_handle_buffer;
  gstsuboverlay_class->advance = gst_dvd_spu_advance;
  gstsuboverlay_class->render = gst_dvd_spu_render;
  gstsuboverlay_class->sub_sink_event = gst_dvd_spu_sub_sink_event;
  gstsuboverlay_class->video_sink_event = gst_dvd_spu_video_sink_event;

  gst_element_class_set_static_metadata (gstelement_class,
      "Sub-picture Overlay", "Mixer/Video/Overlay/SubPicture/DVD/Bluray",
      "Parses Sub-Picture command streams and renders the SPU overlay "
      "onto the video as it passes through",
      "Jan Schmidt <thaytan@noraisin.net>");
}

static void
gst_dvd_spu_init (GstDVDSpu * dvdspu)
{
  dvdspu->pending_spus = g_queue_new ();

  gst_dvd_spu_clear (dvdspu);

  /* no buffers are ever provided to baseclass, so request render always */
  gst_sub_overlay_set_render_no_buffer (GST_SUB_OVERLAY (dvdspu), TRUE);
}

static void
gst_dvd_spu_reset_composition (GstDVDSpu * dvdspu)
{
  gst_sub_overlay_set_composition (GST_SUB_OVERLAY (dvdspu), NULL);
}

static void
gst_dvd_spu_clear (GstDVDSpu * dvdspu)
{
  gst_dvd_spu_flush_spu_info (dvdspu, FALSE);

  dvdspu->spu_input_type = SPU_INPUT_TYPE_NONE;

  dvdspu->spu_state.info.fps_n = 25;
  dvdspu->spu_state.info.fps_d = 1;
}

static void
gst_dvd_spu_dispose (GObject * object)
{
  GstDVDSpu *dvdspu = GST_DVD_SPU (object);

  /* need to hold the SPU lock in case other stuff is still running... */
  DVD_SPU_LOCK (dvdspu);
  gst_dvd_spu_clear (dvdspu);
  DVD_SPU_UNLOCK (dvdspu);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_dvd_spu_finalize (GObject * object)
{
  GstDVDSpu *dvdspu = GST_DVD_SPU (object);

  g_queue_free (dvdspu->pending_spus);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_dvd_spu_start (GstSubOverlay * overlay)
{
  GstDVDSpu *dvdspu = GST_DVD_SPU (overlay);

  /* arrange to keep last video around for still/gap hanlding */
  gst_sub_overlay_set_keep_video (GST_SUB_OVERLAY (dvdspu), TRUE);
  /* enable gap handling (by default when not in still) */
  gst_sub_overlay_set_sparse_video (GST_SUB_OVERLAY (dvdspu), TRUE);
  /* pass buffers without segment dropping or clipping */
  gst_sub_overlay_set_preserve_ts (GST_SUB_OVERLAY (dvdspu), TRUE);

  return TRUE;
}

static gboolean
gst_dvd_spu_stop (GstSubOverlay * overlay)
{
  GstDVDSpu *dvdspu = GST_DVD_SPU (overlay);

  gst_dvd_spu_clear (dvdspu);

  return TRUE;
}

/* With SPU lock held, clear the queue of SPU packets */
static void
gst_dvd_spu_flush_spu_info (GstDVDSpu * dvdspu, gboolean keep_events)
{
  SpuPacket *packet;
  SpuState *state = &dvdspu->spu_state;
  GQueue tmp_q = G_QUEUE_INIT;

  GST_INFO_OBJECT (dvdspu, "Flushing SPU information");

  if (dvdspu->partial_spu) {
    gst_buffer_unref (dvdspu->partial_spu);
    dvdspu->partial_spu = NULL;
  }

  packet = (SpuPacket *) g_queue_pop_head (dvdspu->pending_spus);
  while (packet != NULL) {
    if (packet->buf) {
      gst_buffer_unref (packet->buf);
      g_assert (packet->event == NULL);
      g_free (packet);
    } else if (packet->event) {
      if (keep_events) {
        g_queue_push_tail (&tmp_q, packet);
      } else {
        gst_event_unref (packet->event);
        g_free (packet);
      }
    }
    packet = (SpuPacket *) g_queue_pop_head (dvdspu->pending_spus);
  }
  /* Push anything we decided to keep back onto the pending_spus list */
  for (packet = g_queue_pop_head (&tmp_q); packet != NULL;
      packet = g_queue_pop_head (&tmp_q))
    g_queue_push_tail (dvdspu->pending_spus, packet);

  state->flags &= ~(SPU_STATE_FLAGS_MASK);
  state->next_ts = GST_CLOCK_TIME_NONE;

  switch (dvdspu->spu_input_type) {
    case SPU_INPUT_TYPE_VOBSUB:
      gstspu_vobsub_flush (dvdspu);
      break;
    case SPU_INPUT_TYPE_PGS:
      gstspu_pgs_flush (dvdspu);
      break;
    default:
      break;
  }

  gst_dvd_spu_reset_composition (dvdspu);
}

static gboolean
gst_dvd_spu_set_format_video (GstSubOverlay * overlay, GstCaps * caps,
    GstVideoInfo * info, gint window_width, gint window_height)
{
  GstDVDSpu *dvdspu = GST_DVD_SPU (overlay);

  dvdspu->spu_state.info = *info;

  return TRUE;
}

/* called *without* (STREAM_)LOCK */
static GstBuffer *
gst_dvd_spu_push_still (GstDVDSpu * dvdspu)
{
  GstBuffer *buf;

  DVD_SPU_LOCK (dvdspu);
  gst_sub_overlay_get_buffers (GST_SUB_OVERLAY (dvdspu), &buf, NULL);
  DVD_SPU_UNLOCK (dvdspu);
  if (buf) {
    buf = gst_buffer_ref (buf);
    buf = gst_buffer_make_writable (buf);
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);
    GST_BUFFER_TIMESTAMP (buf) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_DURATION (buf) = GST_CLOCK_TIME_NONE;

    GST_DEBUG_OBJECT (dvdspu, "push still frame");
    /* submit this video buffer to usual processing
     * re-use chain processing to that end;
     * it will go for the STREAM_LOCK, check flushing, etc
     * and then into baseclass usual flow, a.o. render/attach, etc */
    gst_pad_chain (GST_SUB_OVERLAY_VIDEO_SINK_PAD (dvdspu), buf);
  }

  return buf;
}

static gboolean
gst_dvd_spu_video_sink_event (GstSubOverlay * overlay, GstEvent * event)
{
  GstDVDSpu *dvdspu = (GstDVDSpu *) overlay;
  SpuState *state = &dvdspu->spu_state;
  gboolean res = TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CUSTOM_DOWNSTREAM:
    case GST_EVENT_CUSTOM_DOWNSTREAM_OOB:
    {
      gboolean in_still;

      /* in any case, forward (first) */
      GST_DEBUG_OBJECT (dvdspu,
          "Custom event %" GST_PTR_FORMAT " on video pad", event);
      res =
          GST_SUB_OVERLAY_CLASS (parent_class)->video_sink_event (overlay,
          event);

      if (gst_video_event_parse_still_frame (event, &in_still)) {
        GST_DEBUG_OBJECT (dvdspu,
            "Still frame event on video pad: in-still = %d", in_still);

        DVD_SPU_LOCK (dvdspu);
        /* only enable sparse handling when not in still */
        gst_sub_overlay_set_sparse_video (overlay, !in_still);
        if (in_still) {
          state->flags |= SPU_STATE_STILL_FRAME;
          /* Entering still. Advance the SPU to make sure the state is 
           * up to date */
          gst_dvd_spu_check_still_updates (dvdspu);
        } else {
          state->flags &= ~(SPU_STATE_STILL_FRAME);
        }
        DVD_SPU_UNLOCK (dvdspu);

        /* And re-draw the still frame to make sure it appears on
         * screen, otherwise the last frame  might have been discarded
         * by QoS */
        if (in_still)
          gst_dvd_spu_push_still (dvdspu);
      }
      break;
    }
    default:
      res =
          GST_SUB_OVERLAY_CLASS (parent_class)->video_sink_event (overlay,
          event);
      break;
  }

  return res;
}

/*
 * Transform the overlay composition rectangle to fit completely in the video.
 * This is needed to work with ripped videos, which might be cropped and scaled
 * compared to the original (for example to remove black borders). The same
 * transformations were probably not applied to the SPU data, so we need to fit
 * the rendered SPU to the video.
 */
static gboolean
gstspu_fit_overlay_rectangle (GstDVDSpu * dvdspu, GstVideoRectangle * rect,
    gint spu_width, gint spu_height, gboolean keep_aspect)
{
  gint video_width = GST_VIDEO_INFO_WIDTH (&dvdspu->spu_state.info);
  gint video_height = GST_VIDEO_INFO_HEIGHT (&dvdspu->spu_state.info);
  GstVideoRectangle r;

  r = *rect;

  /*
   * Compute scale first, so that the SPU window size matches the video size.
   * If @keep_aspect is %TRUE, the overlay rectangle aspect is kept and
   * centered around the video.
   */
  if (spu_width != video_width || spu_height != video_height) {
    gdouble hscale, vscale;

    hscale = (gdouble) video_width / (gdouble) spu_width;
    vscale = (gdouble) video_height / (gdouble) spu_height;

    if (keep_aspect) {
      if (vscale < hscale)
        vscale = hscale;
      else if (hscale < vscale)
        hscale = vscale;
    }

    r.x *= hscale;
    r.y *= vscale;
    r.w *= hscale;
    r.h *= vscale;

    if (keep_aspect) {
      r.x += (video_width - (spu_width * hscale)) / 2;
      r.y += (video_height - (spu_height * vscale)) / 2;
    }
  }

  /*
   * Next fit the overlay rectangle inside the video, to avoid cropping.
   */
  if (r.x + r.w > video_width)
    r.x = video_width - r.w;

  if (r.x < 0) {
    r.x = 0;
    if (r.w > video_width)
      r.w = video_width;
  }

  if (r.y + r.h > video_height)
    r.y = video_height - r.h;

  if (r.y < 0) {
    r.y = 0;
    if (r.h > video_height)
      r.h = video_height;
  }

  if (r.x != rect->x || r.y != rect->y || r.w != rect->w || r.h != rect->h) {
    *rect = r;
    return TRUE;
  }

  return FALSE;
}

static GstVideoOverlayComposition *
gstspu_render_composition (GstDVDSpu * dvdspu)
{
  GstBuffer *buffer;
  GstVideoInfo overlay_info;
  GstVideoFormat format;
  GstVideoFrame frame;
  GstVideoOverlayRectangle *rectangle;
  GstVideoOverlayComposition *composition;
  GstVideoRectangle win;
  gint spu_w, spu_h;
  gsize size;

  format = GST_VIDEO_OVERLAY_COMPOSITION_FORMAT_RGB;

  switch (dvdspu->spu_input_type) {
    case SPU_INPUT_TYPE_PGS:
      gstspu_pgs_get_render_geometry (dvdspu, &spu_w, &spu_h, &win);
      break;
    case SPU_INPUT_TYPE_VOBSUB:
      gstspu_vobsub_get_render_geometry (dvdspu, &spu_w, &spu_h, &win);
      break;
    default:
      return NULL;
  }

  if (win.w <= 0 || win.h <= 0 || spu_w <= 0 || spu_h <= 0) {
    GST_DEBUG_OBJECT (dvdspu, "skip render of empty window");
    return NULL;
  }

  gst_video_info_init (&overlay_info);
  gst_video_info_set_format (&overlay_info, format, win.w, win.h);
  size = GST_VIDEO_INFO_SIZE (&overlay_info);

  buffer = gst_buffer_new_and_alloc (size);
  if (!buffer) {
    GST_WARNING_OBJECT (dvdspu, "failed to allocate overlay buffer");
    return NULL;
  }

  gst_buffer_add_video_meta (buffer, GST_VIDEO_FRAME_FLAG_NONE,
      format, win.w, win.h);

  if (!gst_video_frame_map (&frame, &overlay_info, buffer, GST_MAP_READWRITE))
    goto map_failed;

  memset (GST_VIDEO_FRAME_PLANE_DATA (&frame, 0), 0,
      GST_VIDEO_FRAME_PLANE_STRIDE (&frame, 0) *
      GST_VIDEO_FRAME_HEIGHT (&frame));

  switch (dvdspu->spu_input_type) {
    case SPU_INPUT_TYPE_VOBSUB:
      gstspu_vobsub_render (dvdspu, &frame);
      break;
    case SPU_INPUT_TYPE_PGS:
      gstspu_pgs_render (dvdspu, &frame);
      break;
    default:
      break;
  }

  gst_video_frame_unmap (&frame);

  GST_DEBUG_OBJECT (dvdspu, "Overlay rendered for video size %dx%d, "
      "spu display size %dx%d, window geometry %dx%d+%d%+d",
      GST_VIDEO_INFO_WIDTH (&dvdspu->spu_state.info),
      GST_VIDEO_INFO_HEIGHT (&dvdspu->spu_state.info),
      spu_w, spu_h, win.w, win.h, win.x, win.y);

  if (gstspu_fit_overlay_rectangle (dvdspu, &win, spu_w, spu_h,
          dvdspu->spu_input_type == SPU_INPUT_TYPE_PGS))
    GST_DEBUG_OBJECT (dvdspu, "Adjusted window to fit video: %dx%d%+d%+d",
        win.w, win.h, win.x, win.y);

  rectangle = gst_video_overlay_rectangle_new_raw (buffer, win.x, win.y,
      win.w, win.h, GST_VIDEO_OVERLAY_FORMAT_FLAG_PREMULTIPLIED_ALPHA);

  gst_buffer_unref (buffer);

  composition = gst_video_overlay_composition_new (rectangle);
  gst_video_overlay_rectangle_unref (rectangle);

  return composition;

map_failed:
  GST_ERROR_OBJECT (dvdspu, "failed to map buffer");
  gst_buffer_unref (buffer);
  return NULL;
}

static void
gst_dvd_spu_render (GstSubOverlay * overlay, GstBuffer * buf)
{
  GstDVDSpu *dvdspu = GST_DVD_SPU (overlay);

  if ((dvdspu->spu_state.flags & SPU_STATE_FORCED_DSP) ||
      ((dvdspu->spu_state.flags & SPU_STATE_FORCED_ONLY) == 0 &&
          (dvdspu->spu_state.flags & SPU_STATE_DISPLAY))) {
    gst_sub_overlay_set_composition (overlay,
        gstspu_render_composition (GST_DVD_SPU (overlay)));
  }
}

/* event (transfer full) */
static gboolean
gst_dvd_spu_handle_dvd_event (GstDVDSpu * dvdspu, GstEvent * event)
{
  gboolean hl_change = FALSE;

  GST_INFO_OBJECT (dvdspu, "DVD event of type %s on subp pad OOB=%d",
      gst_structure_get_string (gst_event_get_structure (event), "event"),
      (GST_EVENT_TYPE (event) == GST_EVENT_CUSTOM_DOWNSTREAM_OOB));

  switch (dvdspu->spu_input_type) {
    case SPU_INPUT_TYPE_VOBSUB:
      hl_change = gstspu_vobsub_handle_dvd_event (dvdspu, event);
      break;
    case SPU_INPUT_TYPE_PGS:
      hl_change = gstspu_pgs_handle_dvd_event (dvdspu, event);
      break;
    default:
      break;
  }

  if (hl_change)
    gst_dvd_spu_reset_composition (dvdspu);

  return hl_change;
}

static gboolean
gstspu_execute_event (GstDVDSpu * dvdspu)
{
  switch (dvdspu->spu_input_type) {
    case SPU_INPUT_TYPE_VOBSUB:
      return gstspu_vobsub_execute_event (dvdspu);
      break;
    case SPU_INPUT_TYPE_PGS:
      return gstspu_pgs_execute_event (dvdspu);
      break;
    default:
      g_assert_not_reached ();
      break;
  }
  return FALSE;
}

/* Advance the SPU packet/command queue to a time. new_ts is in running time */
static void
gst_dvd_spu_advance_spu (GstDVDSpu * dvdspu, GstClockTime new_ts)
{
  SpuState *state = &dvdspu->spu_state;

  if (G_UNLIKELY (dvdspu->spu_input_type == SPU_INPUT_TYPE_NONE))
    return;

  while (state->next_ts == GST_CLOCK_TIME_NONE || state->next_ts <= new_ts) {
    GST_DEBUG_OBJECT (dvdspu,
        "Advancing SPU from TS %" GST_TIME_FORMAT " to %" GST_TIME_FORMAT,
        GST_TIME_ARGS (state->next_ts), GST_TIME_ARGS (new_ts));

    if (!gstspu_execute_event (dvdspu)) {
      GstSegment *video_seg = &GST_SUB_OVERLAY_VIDEO_SEGMENT (dvdspu);
      /* No current command buffer, try and get one */
      SpuPacket *packet = (SpuPacket *) g_queue_pop_head (dvdspu->pending_spus);

      if (packet == NULL)
        return;                 /* No SPU packets available */

      GST_LOG_OBJECT (dvdspu,
          "Popped new SPU packet with TS %" GST_TIME_FORMAT
          ". Video position=%" GST_TIME_FORMAT " (%" GST_TIME_FORMAT
          ") type %s",
          GST_TIME_ARGS (packet->event_ts),
          GST_TIME_ARGS (gst_segment_to_running_time (video_seg,
                  GST_FORMAT_TIME, video_seg->position)),
          GST_TIME_ARGS (video_seg->position),
          packet->buf ? "buffer" : "event");

      gst_dvd_spu_reset_composition (dvdspu);

      if (packet->buf) {
        switch (dvdspu->spu_input_type) {
          case SPU_INPUT_TYPE_VOBSUB:
            gstspu_vobsub_handle_new_buf (dvdspu, packet->event_ts,
                packet->buf);
            break;
          case SPU_INPUT_TYPE_PGS:
            gstspu_pgs_handle_new_buf (dvdspu, packet->event_ts, packet->buf);
            break;
          default:
            g_assert_not_reached ();
            break;
        }
        g_assert (packet->event == NULL);
      } else if (packet->event)
        gst_dvd_spu_handle_dvd_event (dvdspu, packet->event);

      g_free (packet);
      continue;
    }
  }
}

static void
gst_dvd_spu_advance (GstSubOverlay * overlay, GstBuffer * buffer,
    GstClockTime new_ts, GstClockTime new_ts_end)
{
  gst_dvd_spu_advance_spu (GST_DVD_SPU (overlay), new_ts);
}

static void
gst_dvd_spu_check_still_updates (GstDVDSpu * dvdspu)
{
  GstClockTime sub_ts;
  GstClockTime vid_ts;

  if (dvdspu->spu_state.flags & SPU_STATE_STILL_FRAME) {
    GstSegment *video_seg = &GST_SUB_OVERLAY_VIDEO_SEGMENT (dvdspu);
    GstSegment *subp_seg = &GST_SUB_OVERLAY_SUB_SEGMENT (dvdspu);

    if (video_seg->format != GST_FORMAT_TIME)
      return;                   /* No video segment or frames yet */

    vid_ts = gst_segment_to_running_time (video_seg,
        GST_FORMAT_TIME, video_seg->position);
    sub_ts = gst_segment_to_running_time (subp_seg,
        GST_FORMAT_TIME, subp_seg->position);

    if (GST_CLOCK_TIME_IS_VALID (vid_ts) && GST_CLOCK_TIME_IS_VALID (sub_ts))
      vid_ts = MAX (vid_ts, sub_ts);

    GST_DEBUG_OBJECT (dvdspu,
        "In still frame - advancing TS to %" GST_TIME_FORMAT
        " to process SPU buffer", GST_TIME_ARGS (vid_ts));
    gst_dvd_spu_advance_spu (dvdspu, vid_ts);
  }
}

static void
submit_new_spu_packet (GstDVDSpu * dvdspu, GstBuffer * buf)
{
  SpuPacket *spu_packet;
  GstClockTime ts;
  GstClockTime run_ts = GST_CLOCK_TIME_NONE;

  GST_DEBUG_OBJECT (dvdspu,
      "Complete subpicture buffer of %" G_GSIZE_FORMAT " bytes with TS %"
      GST_TIME_FORMAT, gst_buffer_get_size (buf),
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));

  /* Decide whether to pass this buffer through to the rendering code */
  ts = GST_BUFFER_TIMESTAMP (buf);
  if (GST_CLOCK_TIME_IS_VALID (ts)) {
    GstSegment *subp_seg = &GST_SUB_OVERLAY_SUB_SEGMENT (dvdspu);

    if (ts < (GstClockTime) subp_seg->start) {
      GstClockTimeDiff diff = subp_seg->start - ts;

      /* Buffer starts before segment, see if we can calculate a running time */
      run_ts =
          gst_segment_to_running_time (subp_seg, GST_FORMAT_TIME,
          subp_seg->start);
      if (run_ts >= (GstClockTime) diff)
        run_ts -= diff;
      else
        run_ts = GST_CLOCK_TIME_NONE;   /* No running time possible for this subpic */
    } else {
      /* TS within segment, convert to running time */
      run_ts = gst_segment_to_running_time (subp_seg, GST_FORMAT_TIME, ts);
    }
  }

  if (GST_CLOCK_TIME_IS_VALID (run_ts)) {
    /* Complete SPU packet, push it onto the queue for processing when
     * video packets come past */
    spu_packet = g_new0 (SpuPacket, 1);
    spu_packet->buf = buf;

    /* Store the activation time of this buffer in running time */
    spu_packet->event_ts = run_ts;
    GST_INFO_OBJECT (dvdspu,
        "Pushing SPU buf with TS %" GST_TIME_FORMAT " running time %"
        GST_TIME_FORMAT, GST_TIME_ARGS (ts),
        GST_TIME_ARGS (spu_packet->event_ts));

    g_queue_push_tail (dvdspu->pending_spus, spu_packet);

    /* In a still frame condition, advance the SPU to make sure the state is 
     * up to date */
    gst_dvd_spu_check_still_updates (dvdspu);
  } else {
    gst_buffer_unref (buf);
  }
}

static GstFlowReturn
gst_dvd_spu_handle_buffer (GstSubOverlay * overlay, GstBuffer * buf)
{
  GstDVDSpu *dvdspu = (GstDVDSpu *) overlay;
  GstFlowReturn ret = GST_FLOW_OK;
  gsize size;

  g_return_val_if_fail (dvdspu != NULL, GST_FLOW_ERROR);

  GST_SUB_OVERLAY_STREAM_LOCK (overlay);

  GST_INFO_OBJECT (dvdspu, "Have subpicture buffer with timestamp %"
      GST_TIME_FORMAT " and size %" G_GSIZE_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)), gst_buffer_get_size (buf));

  gst_sub_overlay_update_sub_position (overlay, GST_BUFFER_TIMESTAMP (buf));

  if (GST_BUFFER_IS_DISCONT (buf) && dvdspu->partial_spu) {
    gst_buffer_unref (dvdspu->partial_spu);
    dvdspu->partial_spu = NULL;
  }

  if (dvdspu->partial_spu != NULL) {
    if (GST_BUFFER_TIMESTAMP_IS_VALID (buf))
      GST_WARNING_OBJECT (dvdspu,
          "Joining subpicture buffer with timestamp to previous");
    dvdspu->partial_spu = gst_buffer_append (dvdspu->partial_spu, buf);
  } else {
    /* If we don't yet have a buffer, wait for one with a timestamp,
     * since that will avoid collecting the 2nd half of a partial buf */
    if (GST_BUFFER_TIMESTAMP_IS_VALID (buf))
      dvdspu->partial_spu = buf;
    else
      gst_buffer_unref (buf);
  }

  if (dvdspu->partial_spu == NULL)
    goto done;

  size = gst_buffer_get_size (dvdspu->partial_spu);

  switch (dvdspu->spu_input_type) {
    case SPU_INPUT_TYPE_VOBSUB:
      if (size >= 2) {
        guint8 header[2];
        guint16 packet_size;

        gst_buffer_extract (dvdspu->partial_spu, 0, header, 2);
        packet_size = GST_READ_UINT16_BE (header);
        if (packet_size == size) {
          submit_new_spu_packet (dvdspu, dvdspu->partial_spu);
          dvdspu->partial_spu = NULL;
        } else if (packet_size == 0) {
          GST_LOG_OBJECT (dvdspu, "Discarding empty SPU buffer");
          gst_buffer_unref (dvdspu->partial_spu);
          dvdspu->partial_spu = NULL;
        } else if (packet_size < size) {
          /* Somehow we collected too much - something is wrong. Drop the
           * packet entirely and wait for a new one */
          GST_DEBUG_OBJECT (dvdspu,
              "Discarding invalid SPU buffer of size %" G_GSIZE_FORMAT, size);

          gst_buffer_unref (dvdspu->partial_spu);
          dvdspu->partial_spu = NULL;
        } else {
          GST_LOG_OBJECT (dvdspu,
              "SPU buffer claims to be of size %u. Collected %" G_GSIZE_FORMAT
              " so far.", packet_size, size);
        }
      }
      break;
    case SPU_INPUT_TYPE_PGS:{
      /* Collect until we have a command buffer that ends exactly at the size
       * we've collected */
      guint8 packet_type;
      guint16 packet_size;
      GstMapInfo map;
      guint8 *ptr, *end;
      gboolean invalid = FALSE;

      gst_buffer_map (dvdspu->partial_spu, &map, GST_MAP_READ);

      ptr = map.data;
      end = ptr + map.size;

      /* FIXME: There's no need to walk the command set each time. We can set a
       * marker and resume where we left off next time */
      /* FIXME: Move the packet parsing and sanity checking into the format-specific modules */
      while (ptr != end) {
        if (ptr + 3 > end)
          break;
        packet_type = *ptr++;
        packet_size = GST_READ_UINT16_BE (ptr);
        ptr += 2;
        if (ptr + packet_size > end)
          break;
        ptr += packet_size;
        /* 0x80 is the END command for PGS packets */
        if (packet_type == 0x80 && ptr != end) {
          /* Extra cruft on the end of the packet -> assume invalid */
          invalid = TRUE;
          break;
        }
      }
      gst_buffer_unmap (dvdspu->partial_spu, &map);

      if (invalid) {
        gst_buffer_unref (dvdspu->partial_spu);
        dvdspu->partial_spu = NULL;
      } else if (ptr == end) {
        GST_DEBUG_OBJECT (dvdspu,
            "Have complete PGS packet of size %" G_GSIZE_FORMAT ". Enqueueing.",
            map.size);
        submit_new_spu_packet (dvdspu, dvdspu->partial_spu);
        dvdspu->partial_spu = NULL;
      }
      break;
    }
    default:
      GST_ERROR_OBJECT (dvdspu, "Input type not configured before SPU passing");
      goto caps_not_set;
  }

done:
  GST_SUB_OVERLAY_STREAM_UNLOCK (overlay);

  return ret;

  /* ERRORS */
caps_not_set:
  {
    GST_ELEMENT_ERROR (dvdspu, RESOURCE, NO_SPACE_LEFT,
        (_("Subpicture format was not configured before data flow")), (NULL));
    ret = GST_FLOW_ERROR;
    goto done;
  }
}

static gboolean
gst_dvd_spu_flush (GstSubOverlay * overlay)
{
  GstDVDSpu *dvdspu = GST_DVD_SPU (overlay);

  gst_dvd_spu_flush_spu_info (dvdspu, TRUE);

  return TRUE;
}

static gboolean
gst_dvd_spu_sub_sink_event (GstSubOverlay * overlay, GstEvent * event)
{
  GstDVDSpu *dvdspu = (GstDVDSpu *) overlay;
  gboolean res = TRUE;

  /* Some events on the subpicture sink pad just get ignored, like 
   * FLUSH_START */
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CUSTOM_DOWNSTREAM:
    case GST_EVENT_CUSTOM_DOWNSTREAM_STICKY:
    case GST_EVENT_CUSTOM_DOWNSTREAM_OOB:
    {
      const GstStructure *structure = gst_event_get_structure (event);
      const gchar *name = gst_structure_get_name (structure);
      gboolean hl_change = FALSE;

      if (!g_str_has_prefix (name, "application/x-gst-dvd"))
        break;

      DVD_SPU_LOCK (dvdspu);
      if (GST_EVENT_IS_SERIALIZED (event)) {
        SpuPacket *spu_packet = g_new0 (SpuPacket, 1);
        GST_DEBUG_OBJECT (dvdspu,
            "Enqueueing DVD event on subpicture pad for later");
        spu_packet->event = event;
        g_queue_push_tail (dvdspu->pending_spus, spu_packet);
      } else {

        hl_change = gst_dvd_spu_handle_dvd_event (dvdspu, event);
        event = NULL;
        /* event should generate a frame, make one based on last video */
        hl_change = hl_change &&
            (dvdspu->spu_state.flags & SPU_STATE_STILL_FRAME);
      }
      DVD_SPU_UNLOCK (dvdspu);
      if (hl_change)
        gst_dvd_spu_push_still (dvdspu);
      break;
    }
    default:
      break;
  }

  if (event)
    res = GST_SUB_OVERLAY_CLASS (parent_class)->sub_sink_event (overlay, event);

  return res;
}

static gboolean
gst_dvd_spu_set_format (GstSubOverlay * overlay, GstCaps * caps)
{
  GstDVDSpu *dvdspu = GST_DVD_SPU (overlay);
  gboolean res = FALSE;
  GstStructure *s;
  SpuInputType input_type;

  s = gst_caps_get_structure (caps, 0);

  if (gst_structure_has_name (s, "subpicture/x-dvd")) {
    input_type = SPU_INPUT_TYPE_VOBSUB;
  } else if (gst_structure_has_name (s, "subpicture/x-pgs")) {
    input_type = SPU_INPUT_TYPE_PGS;
  } else {
    goto done;
  }

  if (dvdspu->spu_input_type != input_type) {
    GST_INFO_OBJECT (dvdspu, "Incoming SPU packet type changed to %u",
        input_type);
    dvdspu->spu_input_type = input_type;
    gst_dvd_spu_flush_spu_info (dvdspu, TRUE);
  }

  res = TRUE;
done:
  return res;
}

static gboolean
dvd_spu_element_init (GstPlugin * plugin)
{
  const gchar *env;

  GST_DEBUG_CATEGORY_INIT (dvdspu_debug, "gstspu",
      0, "Sub-picture Overlay decoder/renderer");

  env = g_getenv ("GST_DVD_SPU_DEBUG");

  dvdspu_debug_flags = 0;
  if (env != NULL) {
    if (strstr (env, "render-rectangle") != NULL)
      dvdspu_debug_flags |= GST_DVD_SPU_DEBUG_RENDER_RECTANGLE;
    if (strstr (env, "highlight-rectangle") != NULL)
      dvdspu_debug_flags |= GST_DVD_SPU_DEBUG_HIGHLIGHT_RECTANGLE;
  }
  GST_INFO ("debug flags : 0x%02x", dvdspu_debug_flags);

  return gst_element_register (plugin, "dvdspu",
      GST_RANK_PRIMARY, GST_TYPE_DVD_SPU);
}

static gboolean
gst_dvd_spu_plugin_init (GstPlugin * plugin)
{
  return GST_ELEMENT_REGISTER (dvdspu, plugin);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    dvdspu,
    "DVD Sub-picture Overlay element",
    gst_dvd_spu_plugin_init,
    VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
