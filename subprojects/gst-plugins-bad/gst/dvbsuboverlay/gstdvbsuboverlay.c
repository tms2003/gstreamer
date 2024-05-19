/* GStreamer DVB subtitles overlay
 * Copyright (c) 2010 Mart Raudsepp <mart.raudsepp@collabora.co.uk>
 * Copyright (c) 2010 ONELAN Ltd.
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
 * SECTION:element-dvbsuboverlay
 * @title: dvbsuboverlay
 *
 * Renders DVB subtitles on top of a video stream.
 *
 * ## Example launch line
 * |[ FIXME
 * gst-launch-1.0 -v filesrc location=/path/to/ts ! mpegtsdemux name=d ! queue ! mpegaudioparse ! mpg123audiodec ! audioconvert ! autoaudiosink \
 *     d. ! queue ! mpegvideoparse ! mpeg2dec ! videoconvert ! r. \
 *     d. ! queue ! "subpicture/x-dvb" ! dvbsuboverlay name=r ! videoconvert ! autovideosink
 * ]| This pipeline demuxes a MPEG-TS file with MPEG2 video, MP3 audio and embedded DVB subtitles and renders the subtitles on top of the video.
 *
 */


#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/glib-compat-private.h>
#include "gstdvbsuboverlay.h"

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_dvbsub_overlay_debug);
#define GST_CAT_DEFAULT gst_dvbsub_overlay_debug

/* Filter signals and props */
enum
{
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_ENABLE,
  PROP_MAX_PAGE_TIMEOUT,
  PROP_FORCE_END
};

#define DEFAULT_ENABLE (TRUE)
#define DEFAULT_MAX_PAGE_TIMEOUT (0)
#define DEFAULT_FORCE_END (FALSE)

static GstStaticPadTemplate text_sink_factory =
GST_STATIC_PAD_TEMPLATE ("text_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("subpicture/x-dvb")
    );

static void gst_dvbsub_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_dvbsub_overlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_dvbsub_overlay_finalize (GObject * object);

static gboolean gst_dvbsub_overlay_flush (GstSubOverlay * overlay);
static void gst_dvbsub_overlay_render (GstSubOverlay * sub_overlay,
    GstBuffer * buf);
static GstFlowReturn gst_dvbsub_overlay_handle_buffer (GstSubOverlay *
    sub_overlay, GstBuffer * buffer);
static void gst_dvbsub_overlay_advance (GstSubOverlay * sub_overlay,
    GstBuffer * buffer, GstClockTime run_ts, GstClockTime run_ts_end);


#define gst_dvbsub_overlay_parent_class parent_class
G_DEFINE_TYPE (GstDVBSubOverlay, gst_dvbsub_overlay, GST_TYPE_SUB_OVERLAY);
GST_ELEMENT_REGISTER_DEFINE_WITH_CODE (dvbsuboverlay, "dvbsuboverlay",
    GST_RANK_PRIMARY, GST_TYPE_DVBSUB_OVERLAY,
    GST_DEBUG_CATEGORY_INIT (gst_dvbsub_overlay_debug, "dvbsuboverlay", 0,
        "DVB subtitle overlay");
    );

static void new_dvb_subtitles_cb (DvbSub * dvb_sub, DVBSubtitles * subs,
    gpointer user_data);

/* initialize the plugin's class */
static void
gst_dvbsub_overlay_class_init (GstDVBSubOverlayClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstSubOverlayClass *gstsuboverlay_class = (GstSubOverlayClass *) klass;

  gobject_class->set_property = gst_dvbsub_overlay_set_property;
  gobject_class->get_property = gst_dvbsub_overlay_get_property;
  gobject_class->finalize = gst_dvbsub_overlay_finalize;

  gst_sub_overlay_class_add_pad_templates (gstsuboverlay_class, "video_sink",
      NULL, NULL, NULL);
  gst_element_class_add_static_pad_template (gstelement_class,
      &text_sink_factory);

  gstsuboverlay_class->handle_buffer = gst_dvbsub_overlay_handle_buffer;
  gstsuboverlay_class->advance = gst_dvbsub_overlay_advance;
  gstsuboverlay_class->render = gst_dvbsub_overlay_render;
  gstsuboverlay_class->flush = gst_dvbsub_overlay_flush;
  /* indeed, they act similary */
  gstsuboverlay_class->stop = gst_dvbsub_overlay_flush;

  g_object_class_install_property (gobject_class, PROP_ENABLE, g_param_spec_boolean ("enable", "Enable",        /* FIXME: "enable" vs "silent"? */
          "Enable rendering of subtitles", DEFAULT_ENABLE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MAX_PAGE_TIMEOUT,
      g_param_spec_int ("max-page-timeout", "max-page-timeout",
          "Limit maximum display time of a subtitle page (0 - disabled, value in seconds)",
          0, G_MAXINT, DEFAULT_MAX_PAGE_TIMEOUT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_FORCE_END,
      g_param_spec_boolean ("force-end", "Force End",
          "Assume PES-aligned subtitles and force end-of-display",
          DEFAULT_FORCE_END, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (gstelement_class,
      "DVB Subtitles Overlay",
      "Mixer/Video/Overlay/Subtitle",
      "Renders DVB subtitles", "Mart Raudsepp <mart.raudsepp@collabora.co.uk>");
}

static void
gst_dvbsub_overlay_reset (GstDVBSubOverlay * render, gboolean alloc)
{
  DVBSubtitles *subs;

  while ((subs = g_queue_pop_head (render->pending_subtitles))) {
    dvb_subtitles_free (subs);
  }

  if (render->current_subtitle)
    dvb_subtitles_free (render->current_subtitle);
  render->current_subtitle = NULL;

  if (render->dvb_sub)
    dvb_sub_free (render->dvb_sub);
  render->dvb_sub = NULL;

  if (alloc) {
    render->dvb_sub = dvb_sub_new ();
    DvbSubCallbacks dvbsub_callbacks = { &new_dvb_subtitles_cb, };
    dvb_sub_set_callbacks (render->dvb_sub, &dvbsub_callbacks, render);
  }

  render->last_text_pts = GST_CLOCK_TIME_NONE;
  render->pending_sub = FALSE;
}

static void
gst_dvbsub_overlay_flush_subtitles (GstDVBSubOverlay * render)
{
  gst_dvbsub_overlay_reset (render, TRUE);
}

static void
gst_dvbsub_overlay_init (GstDVBSubOverlay * render)
{
  GST_DEBUG_OBJECT (render, "init");

  render->current_subtitle = NULL;
  render->pending_subtitles = g_queue_new ();

  render->enable = DEFAULT_ENABLE;
  render->max_page_timeout = DEFAULT_MAX_PAGE_TIMEOUT;
  render->force_end = DEFAULT_FORCE_END;

  gst_dvbsub_overlay_flush_subtitles (render);

  GST_DEBUG_OBJECT (render, "init complete");
}

static void
gst_dvbsub_overlay_finalize (GObject * object)
{
  GstDVBSubOverlay *overlay = GST_DVBSUB_OVERLAY (object);

  gst_dvbsub_overlay_reset (overlay, FALSE);

  g_queue_free (overlay->pending_subtitles);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_dvbsub_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDVBSubOverlay *overlay = GST_DVBSUB_OVERLAY (object);

  switch (prop_id) {
    case PROP_ENABLE:
      g_atomic_int_set (&overlay->enable, g_value_get_boolean (value));
      break;
    case PROP_MAX_PAGE_TIMEOUT:
      g_atomic_int_set (&overlay->max_page_timeout, g_value_get_int (value));
      break;
    case PROP_FORCE_END:
      g_atomic_int_set (&overlay->force_end, g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_dvbsub_overlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstDVBSubOverlay *overlay = GST_DVBSUB_OVERLAY (object);

  switch (prop_id) {
    case PROP_ENABLE:
      g_value_set_boolean (value, g_atomic_int_get (&overlay->enable));
      break;
    case PROP_MAX_PAGE_TIMEOUT:
      g_value_set_int (value, g_atomic_int_get (&overlay->max_page_timeout));
      break;
    case PROP_FORCE_END:
      g_value_set_boolean (value, g_atomic_int_get (&overlay->force_end));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_dvbsub_overlay_flush (GstSubOverlay * overlay)
{
  GstDVBSubOverlay *render = GST_DVBSUB_OVERLAY (overlay);

  gst_dvbsub_overlay_flush_subtitles (render);

  return TRUE;
}

static void
gst_dvbsub_overlay_process_text (GstDVBSubOverlay * overlay, GstBuffer * buffer,
    guint64 pts)
{
  GstMapInfo map;

  GST_DEBUG_OBJECT (overlay,
      "Processing subtitles with PTS=%" G_GUINT64_FORMAT
      " which is a time of %" GST_TIME_FORMAT, pts, GST_TIME_ARGS (pts));

  gst_buffer_map (buffer, &map, GST_MAP_READ);

  GST_DEBUG_OBJECT (overlay, "Feeding %" G_GSIZE_FORMAT " bytes to libdvbsub",
      map.size);

  overlay->pending_sub = TRUE;
  dvb_sub_feed_with_pts (overlay->dvb_sub, pts, map.data, map.size);

  gst_buffer_unmap (buffer, &map);
  gst_buffer_unref (buffer);

  if (overlay->pending_sub && overlay->force_end) {
    GST_DEBUG_OBJECT (overlay, "forcing subtitle end");
    dvb_sub_feed_with_pts (overlay->dvb_sub, overlay->last_text_pts, NULL, 0);
    g_assert (overlay->pending_sub == FALSE);
  }
}

static void
new_dvb_subtitles_cb (DvbSub * dvb_sub, DVBSubtitles * subs, gpointer user_data)
{
  GstDVBSubOverlay *overlay = GST_DVBSUB_OVERLAY (user_data);
  GstSegment *subtitle_segment = &GST_SUB_OVERLAY_SUB_SEGMENT (dvb_sub);
  int max_page_timeout;
  guint64 start, stop;


  max_page_timeout = g_atomic_int_get (&overlay->max_page_timeout);
  if (max_page_timeout > 0)
    subs->page_time_out = MIN (subs->page_time_out, max_page_timeout);

  GST_INFO_OBJECT (overlay,
      "New DVB subtitles arrived with a page_time_out of %d and %d regions for "
      "PTS=%" G_GUINT64_FORMAT ", which should be at time %" GST_TIME_FORMAT,
      subs->page_time_out, subs->num_rects, subs->pts,
      GST_TIME_ARGS (subs->pts));

  /* spec says page_time_out is not to be taken very accurately anyway,
   * and 0 does not make useful sense anyway */
  if (!subs->page_time_out) {
    GST_WARNING_OBJECT (overlay, "overriding page_time_out 0");
    subs->page_time_out = 1;
  }

  /* clip and convert to running time */
  start = subs->pts;
  stop = subs->pts + subs->page_time_out;

  if (!(gst_segment_clip (subtitle_segment, GST_FORMAT_TIME,
              start, stop, &start, &stop)))
    goto out_of_segment;

  subs->page_time_out = stop - start;

  gst_segment_to_running_time (subtitle_segment, GST_FORMAT_TIME, start);
  g_assert (GST_CLOCK_TIME_IS_VALID (start));
  subs->pts = start;

  GST_DEBUG_OBJECT (overlay, "SUBTITLE real running time: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (start));

  g_queue_push_tail (overlay->pending_subtitles, subs);
  overlay->pending_sub = FALSE;

  return;

out_of_segment:
  {
    GST_DEBUG_OBJECT (overlay, "subtitle out of segment, discarding");
    dvb_subtitles_free (subs);
  }
}

static GstFlowReturn
gst_dvbsub_overlay_handle_buffer (GstSubOverlay * sub_overlay,
    GstBuffer * buffer)
{
  GstDVBSubOverlay *overlay = GST_DVBSUB_OVERLAY (sub_overlay);

  GST_INFO_OBJECT (overlay,
      "subpicture/x-dvb buffer with size %" G_GSIZE_FORMAT,
      gst_buffer_get_size (buffer));

  /* DVB subtitle packets are required to carry the PTS */
  if (G_UNLIKELY (!GST_BUFFER_TIMESTAMP_IS_VALID (buffer))) {
    GST_WARNING_OBJECT (overlay,
        "Text buffer without valid timestamp, dropping");
    gst_buffer_unref (buffer);
    return GST_FLOW_OK;
  }

  /* spec states multiple PES packets may have same PTS,
   * and same PTS packets make up a display set */
  if (overlay->pending_sub &&
      overlay->last_text_pts != GST_BUFFER_TIMESTAMP (buffer)) {
    GST_DEBUG_OBJECT (overlay, "finishing previous subtitle");
    dvb_sub_feed_with_pts (overlay->dvb_sub, overlay->last_text_pts, NULL, 0);
    overlay->pending_sub = FALSE;
  }

  overlay->last_text_pts = GST_BUFFER_TIMESTAMP (buffer);

  /* As the passed start and stop is equal, we shouldn't need to care about out of segment at all,
   * the subtitle data for the PTS is completely out of interest to us. A given display set must
   * carry the same PTS value. */
  /* FIXME: Consider with larger than 64kB display sets, which would be cut into multiple packets,
   * FIXME: does our waiting + render code work when there are more than one packets before
   * FIXME: rendering callback will get called? */

  GST_SUB_OVERLAY_STREAM_LOCK (overlay);

  gst_sub_overlay_update_sub_position (sub_overlay,
      GST_BUFFER_TIMESTAMP (buffer));

  gst_dvbsub_overlay_process_text (overlay, buffer,
      GST_BUFFER_TIMESTAMP (buffer));

  GST_SUB_OVERLAY_STREAM_UNLOCK (overlay);

  return GST_FLOW_OK;
}

static GstVideoOverlayComposition *
gst_dvbsub_overlay_subs_to_comp (GstDVBSubOverlay * overlay,
    DVBSubtitles * subs)
{
  GstVideoOverlayComposition *comp = NULL;
  GstVideoOverlayRectangle *rect;
  gint width, height, dw, dh, wx, wy;
  gint i;
  GstVideoInfo *info;

  g_return_val_if_fail (subs != NULL && subs->num_rects > 0, NULL);

  gst_sub_overlay_get_output_format (GST_SUB_OVERLAY (overlay), &info, NULL,
      NULL);

  width = GST_VIDEO_INFO_WIDTH (info);
  height = GST_VIDEO_INFO_HEIGHT (info);

  dw = subs->display_def.display_width;
  dh = subs->display_def.display_height;

  GST_LOG_OBJECT (overlay,
      "converting %d rectangles for display %dx%d -> video %dx%d",
      subs->num_rects, dw, dh, width, height);

  if (subs->display_def.window_flag) {
    wx = subs->display_def.window_x;
    wy = subs->display_def.window_y;
    GST_LOG_OBJECT (overlay, "display window %dx%d @ (%d, %d)",
        subs->display_def.window_width, subs->display_def.window_height,
        wx, wy);
  } else {
    wx = 0;
    wy = 0;
  }

  for (i = 0; i < subs->num_rects; i++) {
    DVBSubtitleRect *srect = &subs->rects[i];
    GstBuffer *buf;
    gint w, h;
    guint8 *in_data;
    guint32 *palette, *data;
    gint rx, ry, rw, rh, stride;
    gint k, l;
    GstMapInfo map;

    GST_LOG_OBJECT (overlay, "rectangle %d: %dx%d @ (%d, %d)", i,
        srect->w, srect->h, srect->x, srect->y);

    w = srect->w;
    h = srect->h;

    buf = gst_buffer_new_and_alloc (w * h * 4);
    gst_buffer_map (buf, &map, GST_MAP_WRITE);
    data = (guint32 *) map.data;
    in_data = srect->pict.data;
    palette = srect->pict.palette;
    stride = srect->pict.rowstride;
    for (k = 0; k < h; k++) {
      for (l = 0; l < w; l++) {
        guint32 ayuv;

        ayuv = palette[*in_data];
        GST_WRITE_UINT32_BE (data, ayuv);
        in_data++;
        data++;
      }
      in_data += stride - w;
    }
    gst_buffer_unmap (buf, &map);

    /* this is assuming the subtitle rectangle coordinates are relative
     * to the window (if there is one) within a display of specified dimension.
     * Coordinate wrt the latter is then scaled to the actual dimension of
     * the video we are dealing with here. */
    rx = gst_util_uint64_scale (wx + srect->x, width, dw);
    ry = gst_util_uint64_scale (wy + srect->y, height, dh);
    rw = gst_util_uint64_scale (srect->w, width, dw);
    rh = gst_util_uint64_scale (srect->h, height, dh);

    GST_LOG_OBJECT (overlay, "rectangle %d rendered: %dx%d @ (%d, %d)", i,
        rw, rh, rx, ry);

    gst_buffer_add_video_meta (buf, GST_VIDEO_FRAME_FLAG_NONE,
        GST_VIDEO_OVERLAY_COMPOSITION_FORMAT_YUV, w, h);
    rect = gst_video_overlay_rectangle_new_raw (buf, rx, ry, rw, rh, 0);
    g_assert (rect);
    if (comp) {
      gst_video_overlay_composition_add_rectangle (comp, rect);
    } else {
      comp = gst_video_overlay_composition_new (rect);
    }
    gst_video_overlay_rectangle_unref (rect);
    gst_buffer_unref (buf);
  }

  return comp;
}

static void
gst_dvbsub_overlay_render (GstSubOverlay * sub_overlay, GstBuffer * buf)
{
  GstDVBSubOverlay *overlay = (GstDVBSubOverlay *) sub_overlay;

  if (overlay->current_subtitle)
    gst_sub_overlay_set_composition (sub_overlay,
        gst_dvbsub_overlay_subs_to_comp (overlay, overlay->current_subtitle));
}

static void
gst_dvbsub_overlay_advance (GstSubOverlay * sub_overlay,
    GstBuffer * buffer, GstClockTime run_ts, GstClockTime run_ts_end)
{
  GstDVBSubOverlay *overlay = GST_DVBSUB_OVERLAY (sub_overlay);
  GstSegment *subtitle_segment = &GST_SUB_OVERLAY_SUB_SEGMENT (sub_overlay);
  GstClockTime vid_running_time, vid_running_time_end;

  vid_running_time = run_ts;
  vid_running_time_end = run_ts_end;

  if (!GST_CLOCK_TIME_IS_VALID (run_ts))
    goto missing_timestamp;

  if (!GST_CLOCK_TIME_IS_VALID (vid_running_time_end))
    vid_running_time_end = vid_running_time;

  if (!g_queue_is_empty (overlay->pending_subtitles)) {
    DVBSubtitles *tmp, *candidate = NULL;

    while (!g_queue_is_empty (overlay->pending_subtitles)) {
      tmp = g_queue_peek_head (overlay->pending_subtitles);

      if (tmp->pts > vid_running_time_end) {
        /* For a future video frame */
        break;
      } else if (tmp->num_rects == 0) {
        /* Clear screen */
        if (overlay->current_subtitle)
          dvb_subtitles_free (overlay->current_subtitle);
        overlay->current_subtitle = NULL;
        if (candidate)
          dvb_subtitles_free (candidate);
        candidate = NULL;
        g_queue_pop_head (overlay->pending_subtitles);
        dvb_subtitles_free (tmp);
        tmp = NULL;
        /* clear overlay */
        gst_sub_overlay_set_composition (sub_overlay, NULL);
      } else if (tmp->pts + tmp->page_time_out * GST_SECOND *
          ABS (subtitle_segment->rate) >= vid_running_time) {
        if (candidate)
          dvb_subtitles_free (candidate);
        candidate = tmp;
        g_queue_pop_head (overlay->pending_subtitles);
      } else {
        /* Too late */
        dvb_subtitles_free (tmp);
        tmp = NULL;
        g_queue_pop_head (overlay->pending_subtitles);
      }
    }

    if (candidate) {
      GST_DEBUG_OBJECT (overlay,
          "Time to show the next subtitle page (%" GST_TIME_FORMAT " >= %"
          GST_TIME_FORMAT ") - it has %u regions",
          GST_TIME_ARGS (vid_running_time), GST_TIME_ARGS (candidate->pts),
          candidate->num_rects);
      dvb_subtitles_free (overlay->current_subtitle);
      overlay->current_subtitle = candidate;
      /* trigger render */
      gst_sub_overlay_set_composition (sub_overlay, NULL);
    }
  }

  /* Check that we haven't hit the fallback timeout for current subtitle page */
  if (overlay->current_subtitle
      && vid_running_time >
      (overlay->current_subtitle->pts +
          overlay->current_subtitle->page_time_out * GST_SECOND *
          ABS (subtitle_segment->rate))) {
    GST_INFO_OBJECT (overlay,
        "Subtitle page not redefined before fallback page_time_out of %u seconds (missed data?) - deleting current page",
        overlay->current_subtitle->page_time_out);
    dvb_subtitles_free (overlay->current_subtitle);
    overlay->current_subtitle = NULL;
  }

  return;

missing_timestamp:
  {
    GST_WARNING_OBJECT (overlay, "missing timestamp, not advancing");
    return;
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return GST_ELEMENT_REGISTER (dvbsuboverlay, plugin);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    dvbsuboverlay,
    "DVB subtitle renderer",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
