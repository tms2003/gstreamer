/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2003> David Schleef <ds@schleef.org>
 * Copyright (C) <2006> Julien Moutte <julien@moutte.net>
 * Copyright (C) <2006> Zeeshan Ali <zeeshan.ali@nokia.com>
 * Copyright (C) <2006-2008> Tim-Philipp Müller <tim centricular net>
 * Copyright (C) <2009> Young-Ho Cha <ganadist@gmail.com>
 * Copyright (C) <2024> Mark Nauwelaerts <mnauw@users.sourceforge.net>
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

#ifndef __GST_SUB_OVERLAY_H__
#define __GST_SUB_OVERLAY_H__

#if 0
/* either a separate lib or this needs to be defined for whole of video ?? */
#ifndef GST_USE_UNSTABLE_API
#warning "GstSubOverlay is unstable API and may change in future."
#warning "You can define GST_USE_UNSTABLE_API to avoid this warning."
#endif
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/video-overlay-composition.h>

G_BEGIN_DECLS

#define GST_TYPE_SUB_OVERLAY            (gst_sub_overlay_get_type())
#define GST_SUB_OVERLAY(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),\
                                         GST_TYPE_SUB_OVERLAY, GstSubOverlay))
#define GST_SUB_OVERLAY_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),\
                                         GST_TYPE_SUB_OVERLAY,GstSubOverlayClass))
#define GST_SUB_OVERLAY_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),\
                                         GST_TYPE_SUB_OVERLAY, GstSubOverlayClass))
#define GST_IS_SUB_OVERLAY(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),\
                                         GST_TYPE_SUB_OVERLAY))
#define GST_IS_SUB_OVERLAY_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),\
                                         GST_TYPE_SUB_OVERLAY))
#define GST_SUB_OVERLAY_CAST(obj)       ((GstSubOverlay *)(obj))

/**
 * GST_SUB_OVERLAY_VIDEO_SINK_NAME:
 *
 * The default name of the templates for the video sink pad.
 */
#define GST_SUB_OVERLAY_VIDEO_SINK_NAME     "video_sink"
/**
 * GST_SUB_OVERLAY_SUB_SINK_NAME:
 *
 * The default name of the templates for the sub sink pad.
 */
#define GST_SUB_OVERLAY_SUB_SINK_NAME       "text_sink"
/**
 * GST_SUB_OVERLAY_SRC_NAME:
 *
 * The default name of the templates for the source pad.
 */
#define GST_SUB_OVERLAY_SRC_NAME            "src"

/**
 * GST_SUB_OVERLAY_SRC_PAD:
 * @obj: base audio codec instance
 *
 * Gives the pointer to the source #GstPad object of the element.
 */
#define GST_SUB_OVERLAY_SRC_PAD(obj)         (((GstSubOverlay *) (obj))->srcpad)

/**
 * GST_SUB_OVERLAY_VIDEO_SINK_PAD:
 * @obj: base audio codec instance
 *
 * Gives the pointer to the video sink #GstPad object of the element.
 */
#define GST_SUB_OVERLAY_VIDEO_SINK_PAD(obj)  (((GstSubOverlay *) (obj))->video_sinkpad)

/**
 * GST_SUB_OVERLAY_SUB_SINK_PAD:
 * @obj: base audio codec instance
 *
 * Gives the pointer to the video sink #GstPad object of the element.
 */
#define GST_SUB_OVERLAY_SUB_SINK_PAD(obj)  (((GstSubOverlay *) (obj))->sub_sinkpad)

/**
 * GST_AUDIO_DECODER_INPUT_SEGMENT:
 * @obj: suboverlay instance
 *
 * Gives the input segment of the main video pad of the element.
 */
#define GST_SUB_OVERLAY_VIDEO_SEGMENT(obj)   (GST_SUB_OVERLAY_CAST (obj)->segment)

/**
 * GST_AUDIO_DECODER_OUTPUT_SEGMENT:
 * @obj: suboverlay instance
 *
 * Gives the input segment of the sub pad of the element.
 */
#define GST_SUB_OVERLAY_SUB_SEGMENT(obj)     (GST_SUB_OVERLAY_CAST (obj)->sub_segment)

#define GST_SUB_OVERLAY_GET_STREAM_LOCK(ov) (&GST_SUB_OVERLAY (ov)->lock)
#define GST_SUB_OVERLAY_STREAM_LOCK(ov)     (g_rec_mutex_lock (GST_SUB_OVERLAY_GET_STREAM_LOCK (ov)))
#define GST_SUB_OVERLAY_STREAM_UNLOCK(ov)   (g_rec_mutex_unlock (GST_SUB_OVERLAY_GET_STREAM_LOCK (ov)))

typedef struct _GstSubOverlay GstSubOverlay;
typedef struct _GstSubOverlayClass GstSubOverlayClass;

typedef struct _GstSubOverlayPrivate GstSubOverlayPrivate;

/**
 * GstSubOverlay:
 *
 * Opaque suboverlay object structure
 */
struct _GstSubOverlay {
  GstElement               element;

  /*< protected >*/
  /* source and sink pads */
  GstPad                  *video_sinkpad;
  GstPad                  *sub_sinkpad;
  GstPad                  *srcpad;

  /* serializes data processing among video and sub
   * (e.g. chain and serialized events)
   * locked during most calls to subclass methods */
  GRecMutex                lock;

  /* MT-protected (with STREAM_LOCK) */
  GstSegment               segment;
  GstSegment               sub_segment;

  /*< private >*/
  GstSubOverlayPrivate    *priv;

  gpointer       _gst_reserved[GST_PADDING_LARGE];
};

/**
 * GstAudioDecoderClass:
 * @element_class:  The parent class structure
 * @start:          Optional.  With STREAM_LOCK.
 *                  Called when the element starts processing.
 *                  Allows opening external resources.
 * @stop:           Optional.  With STREAM_LOCK.
 *                  Called when the element stops processing.
 *                  Allows closing external resources.
 * @set_format:     Optional.  With STREAM_LOCK.
 *                  Notifies subclass of incoming data format (caps).
 * @set_format_video: Optional.  With STREAM_LOCK.
 *                    Notifies subclass of incoming data format (caps).
 * @flush:          Optional.  With STREAM_LOCK.
 *                  Instructs subclass to clear any caches.
 * @handle_buffer:  Provides input data to subclass.
 * @advance:        Optional.  With STREAM_LOCK.
 *                  Called with received video to advance subclass (state)
 *                  to specified time.
 * @advance:        Optional.  With STREAM_LOCK.
 *                  Called with received video to advance subclass (state)
 *                  to specified time.
 * @render:         Optional.  With STREAM_LOCK.
 *                  Called to generate composition.
 *                  It is only called if there is no current composition
 *                  (and possibly other conditions).
 * @pre_apply:      Optional.  With STREAM_LOCK.
 *                  Called just prior to blending or attaching composition.
 *                  Returning FALSE will abort the latter (but not pushing).
 * @video_sink_event: Optional.
 *                    Event handler on the video sink pad.
 *                    Subclasses should chain up to the parent implementation
 *                    to invoke the default handler.
 * @sub_sink_event:   Optional.
 *                    Event handler on the subsink pad.
 *                    Subclasses should chain up to the parent implementation
 *                    to invoke the default handler.
 *
 * Subclasses can override any of the available virtual methods or not, as
 * needed. At minimum @handle_buffer (and likely @set_format) needs to be
 * overridden.
 */
struct _GstSubOverlayClass {
  GstElementClass                    parent_class;

  /*< private >*/
  GstPadTemplate                    *video_template;
  GstPadTemplate                    *src_template;

  /*< public >*/
  /* virtual methods for subclasses */

  gboolean      (*start)              (GstSubOverlay *overlay);

  gboolean      (*stop)               (GstSubOverlay *overlay);

  gboolean      (*set_format)         (GstSubOverlay *overlay,
                                       GstCaps *caps);

  gboolean      (*set_format_video)   (GstSubOverlay *overlay,
                                       GstCaps *caps,
                                       GstVideoInfo *info,
                                       gint window_width, gint window_height);

  gboolean      (*flush)              (GstSubOverlay *overlay);

  /**
   * GstSubOverlay::handle_frame:
   * @buffer: (transfer full): input sub buffer
   */
  GstFlowReturn (*handle_buffer)      (GstSubOverlay *overlay,
                                       GstBuffer *buffer);

  void           (*advance)           (GstSubOverlay *overlay,
                                       GstBuffer *video,
                                       GstClockTime run_ts,
                                       GstClockTime run_ts_end);

  void           (*render)            (GstSubOverlay *overlay,
                                       GstBuffer *sub);

  gboolean       (*pre_apply)         (GstSubOverlay *overlay,
                                       GstBuffer *video,
                                       GstVideoOverlayComposition *comp,
                                       GstVideoOverlayComposition *merged,
                                       gboolean attach);

  /**
   * GstSubOverlay::video_sink_event:
   * @event: (transfer full): input event
   */
  gboolean       (*video_sink_event)  (GstSubOverlay *overlay,
                                       GstEvent *event);

  /**
   * GstSubOverlay::sub_sink_event:
   * @event: (transfer full): input event
   */
  gboolean       (*sub_sink_event)    (GstSubOverlay *overlay,
                                       GstEvent *event);
};

GST_VIDEO_API
GType            gst_sub_overlay_get_type (void);

GST_VIDEO_API
void             gst_sub_overlay_class_add_pad_templates (GstSubOverlayClass * klass,
                                                          const gchar * video_templ_name,
                                                          GstPadTemplate * video_templ,
                                                          const gchar * src_templ_name,
                                                          GstPadTemplate * src_templ);

GST_VIDEO_API
GstFlowReturn    gst_sub_overlay_update_sub_buffer (GstSubOverlay * overlay,
                                                    GstBuffer * buffer,
                                                    gboolean force);

GST_VIDEO_API
void             gst_sub_overlay_update_sub_position (GstSubOverlay * overlay,
                                                      GstClockTime ts);

GST_VIDEO_API
void             gst_sub_overlay_set_composition (GstSubOverlay * overlay,
                                                  GstVideoOverlayComposition * composition);


GST_VIDEO_API
void             gst_sub_overlay_get_output_format (GstSubOverlay * overlay,
                                                    GstVideoInfo ** info,
                                                    gint * ww, gint * wh);

GST_VIDEO_API
void             gst_sub_overlay_get_buffers (GstSubOverlay * overlay,
                                              GstBuffer ** video,
                                              GstBuffer ** sub);

GST_VIDEO_API
gboolean         gst_sub_overlay_get_linked (GstSubOverlay * overlay);

GST_VIDEO_API
void             gst_sub_overlay_set_visible (GstSubOverlay * overlay,
                                              gboolean enable);

GST_VIDEO_API
gboolean         gst_sub_overlay_get_visible (GstSubOverlay * overlay);

GST_VIDEO_API
void             gst_sub_overlay_set_wait (GstSubOverlay * overlay,
                                           gboolean enable);

GST_VIDEO_API
gboolean         gst_sub_overlay_get_wait (GstSubOverlay * overlay);

GST_VIDEO_API
void             gst_sub_overlay_set_preserve_ts (GstSubOverlay * overlay,
                                                  gboolean enable);

GST_VIDEO_API
void             gst_sub_overlay_set_keep_video (GstSubOverlay * overlay,
                                                 gboolean enable);

GST_VIDEO_API
void             gst_sub_overlay_set_sparse_video (GstSubOverlay * overlay,
                                                   gboolean enable);

GST_VIDEO_API
void             gst_sub_overlay_set_render_no_buffer (GstSubOverlay * overlay,
                                                       gboolean enable);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstSubOverlay, gst_object_unref)

G_END_DECLS

#endif /* __GST_SUB_OVERLAY_H */
