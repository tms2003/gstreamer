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

/**
 * SECTION:gstsuboverlay
 * @title: GstSubOverlay
 * @short_description: Base class for subsidiary overlay elements
 *
 * This base class is for overlay elements that overlay a subsidiary stream
 * (typically some form of subtitles or caption) onto video streams, especially
 * when the overlay data is provided by a #GstVideoOverlayComposition.
 *
 * GstSubOverlay and subclass should cooperate as follows.
 *
 * ## Configuration
 *
 *   * Initially, GstSubOverlay calls @start when the decoder element
 *     is activated, which allows subclass to perform any global setup.
 *     Some parameters that influence baseclass can be set here
 *     if not already done at instance initialization time.
 *   * GstSubOverlay calls @set_format to inform subclass of the format
 *     of sub data that it is about to receive, and similarly so for the video
 *     format using @set_format_video.
 *   * GstSubOverlay calls @stop at end of all processing.
 *
 * ## Data processing
 *
 * As the baseclass handles the video stream processing, it should be mostly
 * considered as pass-through by the subclass.  The latter, however, obviously
 * does need to tend to sub stream data.
 *
 *     * Input sub buffer is provided to subclass' @handle_buffer.
 *     * In simple cases, subclass can directly pass this to a call to
 *       @gst_sub_overlay_update_sub_buffer to provide this to baseclass.
 *       However, it need not, and can alternatively parse and process
 *       input data and update and manage internal state.
 *       It could keep this state internal, or provide some processed buffer
 *       data (as opposed to input) to @gst_sub_overlay_update_sub_buffer
 *       (with suitable stream timestamps and duration).
 *       If it wishes to support waiting of video, it should at least update
 *       baseclass using @gst_sub_overlay_update_sub_position at suitable stage
 *       (of processing of available sub data).
 *     * As video data is received, baseclass calls @advance.  In case of
 *       internal state, the latter can be updated using provided time.
 *       Alternatively, if buffers were provided to baseclass, it need not do
 *       much (if anything) as the baseclass will match the current video time
 *       to provided sub buffer time and advance accordingly.  That is, it will
 *       release an old sub buffer and decide whether a pending sub buffer
 *       needs to be applied to current video.
 *     * If no overlay composition is currently active, it will call @render.
 *       At least, if a provided sub buffer is considered active (or optionally
 *       anyway).
 *     * During the latter call (or at other times), subclass should call
 *       @gst_sub_overlay_set_composition to set current composition.
 *     * Prior to actually pushing a buffer downstream, a current
 *       composition is either blended onto or attached to outgoing video.
 *       Just prior to that, @pre_apply is called to allow subclass
 *       to cancel the aforementioned and/or to supplement such.
 *
 * In summary, a subclass typically roughly either;
 *
 *     * provides (properly timestamped) buffers to baseclass, which the latter
 *       then suitably matches to incoming video and requests subclass
 *       to turn into an overlay composition (using @render)
 *     * manages internal state updated using input data and @advance,
 *       which is then used in @render to produce an overlay composition
 *
 * Evidently, minor variations on above ends of the scale are also possible.
 *
 * When it comes to matching (timestamps), the latter are (evidently) converted
 * to running time to do so.  A missing time (e.g. _NONE duration) tends to
 * be interpreted as some form of (extending) infinite (whenever such is
 * not nonsensical or problematic).
 *
 * As multiple streaming threads are involved, a STREAM_LOCK is fairly broadly
 * held while invoking most subclass methods, except e.g. @handle_buffer.
 * The latter is left up to subclass (to allow for more optimal processing)
 * as needed when invoking API functions, all of which should be invoked
 * with the STREAM_LOCK held (unless otherwise noted).
 *
 * ## Shutdown phase
 *
 *   * GstSubOverlay class calls @stop to inform the subclass that data
 *     parsing will be stopped.
 *
 * Subclass should call @gst_sub_overlay_class_add_pad_templates during
 * class initialization to setup some default incoming and outgoing video pad
 * templates.  It should also provide a pad template for input sub.
 *
 * Things that subclass needs to take care of:
 *
 *   * Provide pad templates
 *   * Accept data in @handle_buffer and provide results to
 *      @gst_sub_overlay_update_sub_buffer and/or
 *      @gst_sub_overlay_set_composition.
 *   * Manage STREAM_LOCK as needed, e.g. when invoking API functions
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define GST_USE_UNSTABLE_API

#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>

#include "gstsuboverlay.h"
#include <string.h>
#include <math.h>

#define DEFAULT_PROP_VISIBLE	TRUE
#define DEFAULT_PROP_WAIT_SUB	FALSE

enum
{
  PROP_0,
  PROP_VISIBLE,
  PROP_WAIT_SUB,
};

GST_DEBUG_CATEGORY_STATIC (sub_overlay_debug);
#define GST_CAT_DEFAULT sub_overlay_debug

#define VIDEO_FORMATS GST_VIDEO_OVERLAY_COMPOSITION_BLEND_FORMATS

#define SUB_OVERLAY_CAPS GST_VIDEO_CAPS_MAKE (VIDEO_FORMATS)

#define SUB_OVERLAY_ALL_CAPS SUB_OVERLAY_CAPS ";" \
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES ("ANY", GST_VIDEO_FORMATS_ANY)

static GstStaticCaps sw_template_caps = GST_STATIC_CAPS (SUB_OVERLAY_CAPS);

static GstStaticPadTemplate src_template_factory =
GST_STATIC_PAD_TEMPLATE (GST_SUB_OVERLAY_SRC_NAME,
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SUB_OVERLAY_ALL_CAPS)
    );

static GstStaticPadTemplate video_sink_template_factory =
GST_STATIC_PAD_TEMPLATE (GST_SUB_OVERLAY_VIDEO_SINK_NAME,
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SUB_OVERLAY_ALL_CAPS)
    );

/* parts that are used on either video or sub are protected by _STREAM_LOCK;
 * other parts are only used on one of the above, and so are implicitly
 * protected by the respective pad's STREAM_LOCK
 * if that may not fully cover it, OBJECT_LOCK may also be used */

/* NOTE; _STREAM_LOCK is not held when pushing downstream,
 * changing that also needs changes to FLUSH processing, etc*/

struct _GstSubOverlayPrivate
{
  /* deduced from video caps */
  GstVideoInfo info;

  /* window dimension, reported in the composition meta params 0
   * set to 0 if missing */
  gint window_width;
  gint window_height;

  /* various state */

  gboolean sub_linked;
  /* (optional) updated sub buffer provided by subclass */
  GstBuffer *sub_buffer;
  /* sub buffer update might be waiting */
  gboolean sub_waiting;
  /* was a sub buffer ever provided */
  gboolean got_sub_buffer;
  /* ... to update to a buffer with this running time */
  GstClockTime sub_next_run_ts;

  /* (optional) reference of last video buffer */
  GstBuffer *video_buffer;

  /* on-going flush, eos event state */
  gboolean video_flushing;
  gboolean video_eos;
  gboolean sub_flushing;
  gboolean sub_eos;

  /* casually protected with OBJECT_LOCK */
  /* whether negotiation concluded to attach (rather than blend) */
  gboolean attach_compo_to_buffer;
  /* composition (to be) set by subclass */
  GstVideoOverlayComposition *composition;
  /* non-owned upstream composition; marker pointer */
  GstVideoOverlayComposition *upstream_composition_p;
  /* owned copy of upstream (so as not to affect writable unnecessarily) */
  GstVideoOverlayComposition *upstream_composition;
  /* merged combination of above compositions */
  GstVideoOverlayComposition *merged_composition;
  /* whether either composition changed, so a new merge is needed */
  gboolean need_merge;

  /* behavioral properties/settings for subclass to configure */
  /* clip video buffer timestamps */
  gboolean preserve_ts;
  /* keep a reference to last video in video_buffer */
  gboolean keep_video;
  /* do sparse (gap) handling using video_buffer */
  gboolean sparse_video;
  /* also invoke render() if buffer is NULL */
  gboolean render_no_buffer;

  /* properties */
  gboolean visible;
  gboolean wait_sub;

  /* to signal removal of a queued sub buffer, arrival of a sub buffer,
   * a sub segment update, or a change in status (e.g. shutdown, flushing) */
  GCond evt_cond;
  /* a GCond can not be used with a GRecMutex,
   * so a separate GMutex is used and tied together in following way
   * (see helper macros below) */
  GMutex evt_lock;
  gboolean evt_cookie;
};

#define GST_SUB_OVERLAY_GET_EVT_COND(overlay) (&((GstSubOverlay *)overlay)->priv->evt_cond)
#define GST_SUB_OVERLAY_GET_EVT_LOCK(overlay) (&((GstSubOverlay *)overlay)->priv->evt_lock)
/* to be used while holding_STREAM_LOCK */
#define GST_SUB_OVERLAY_WAIT(overlay) G_STMT_START {    \
  guint32 cookie = ((GstSubOverlay *) overlay)->priv->evt_cookie; \
  GST_SUB_OVERLAY_STREAM_UNLOCK (overlay); \
  g_mutex_lock (GST_SUB_OVERLAY_GET_EVT_LOCK (overlay));            \
  /* should work unless a lot of event'ing and thread starvation */\
  while (cookie == ((GstSubOverlay *) overlay)->priv->evt_cookie)         \
    g_cond_wait (GST_SUB_OVERLAY_GET_EVT_COND (overlay),            \
        GST_SUB_OVERLAY_GET_EVT_LOCK (overlay));                    \
  g_mutex_unlock (GST_SUB_OVERLAY_GET_EVT_LOCK (overlay));          \
  GST_SUB_OVERLAY_STREAM_LOCK (overlay); \
} G_STMT_END
/* to be used while holding_STREAM_LOCK */
#define GST_SUB_OVERLAY_BROADCAST(overlay) G_STMT_START {       \
  g_mutex_lock (GST_SUB_OVERLAY_GET_EVT_LOCK (overlay));            \
  /* never mind wrap-around */                                     \
  ++(((GstSubOverlay *) overlay)->priv->evt_cookie);                      \
  g_cond_broadcast (GST_SUB_OVERLAY_GET_EVT_COND (overlay));        \
  g_mutex_unlock (GST_SUB_OVERLAY_GET_EVT_LOCK (overlay));          \
} G_STMT_END


static GstElementClass *parent_class = NULL;
static gint private_offset = 0;

static void gst_sub_overlay_class_init (GstSubOverlayClass * klass);
static void gst_sub_overlay_init (GstSubOverlay * overlay,
    GstSubOverlayClass * klass);

static GstStateChangeReturn gst_sub_overlay_change_state (GstElement *
    element, GstStateChange transition);

static GstCaps *gst_sub_overlay_get_videosink_caps (GstPad * pad,
    GstSubOverlay * overlay, GstCaps * filter);
static GstCaps *gst_sub_overlay_get_src_caps (GstPad * pad,
    GstSubOverlay * overlay, GstCaps * filter);
static gboolean gst_sub_overlay_setcaps (GstSubOverlay * overlay,
    GstCaps * caps);
static gboolean gst_sub_overlay_setcaps_sub (GstSubOverlay * overlay,
    GstCaps * caps);
static gboolean gst_sub_overlay_src_event (GstPad * pad,
    GstObject * parent, GstEvent * event);
static gboolean gst_sub_overlay_src_query (GstPad * pad,
    GstObject * parent, GstQuery * query);

static gboolean gst_sub_overlay_video_event (GstPad * pad,
    GstObject * parent, GstEvent * event);
static gboolean gst_sub_overlay_video_query (GstPad * pad,
    GstObject * parent, GstQuery * query);
static GstFlowReturn gst_sub_overlay_video_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buffer);

static gboolean gst_sub_overlay_sub_event (GstPad * pad,
    GstObject * parent, GstEvent * event);
static GstFlowReturn gst_sub_overlay_sub_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buffer);
static GstPadLinkReturn gst_sub_overlay_sub_pad_link (GstPad * pad,
    GstObject * parent, GstPad * peer);
static void gst_sub_overlay_sub_pad_unlink (GstPad * pad, GstObject * parent);
static void gst_sub_overlay_pop_sub (GstSubOverlay * overlay);

static void gst_sub_overlay_finalize (GObject * object);
static void gst_sub_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_sub_overlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_sub_overlay_video_sink_event (GstSubOverlay * overlay,
    GstEvent * event);
static gboolean gst_sub_overlay_sub_sink_event (GstSubOverlay * overlay,
    GstEvent * event);

GType
gst_sub_overlay_get_type (void)
{
  static gsize sub_overlay_type = 0;

  if (g_once_init_enter ((gsize *) & sub_overlay_type)) {
    GType _type;
    static const GTypeInfo sub_overlay_info = {
      sizeof (GstSubOverlayClass),
      (GBaseInitFunc) NULL,
      NULL,
      (GClassInitFunc) gst_sub_overlay_class_init,
      NULL,
      NULL,
      sizeof (GstSubOverlay),
      0,
      (GInstanceInitFunc) gst_sub_overlay_init,
    };

    _type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstSubOverlay", &sub_overlay_info, G_TYPE_FLAG_ABSTRACT);

    private_offset =
        g_type_add_instance_private (_type, sizeof (GstSubOverlayPrivate));

    g_once_init_leave (&sub_overlay_type, _type);
  }

  return (GType) sub_overlay_type;
}

static inline GstSubOverlayPrivate *
gst_sub_overlay_get_instance_private (GstSubOverlay * self)
{
  return (G_STRUCT_MEMBER_P (self, private_offset));
}

static GstPadTemplate *
gst_sub_overlay_class_make_pad_template (const gchar * name,
    GstStaticPadTemplate * templ)
{
  GstPadTemplate *result;

  if (!name) {
    result = gst_static_pad_template_get (templ);
  } else {
    GstCaps *caps = gst_static_caps_get (&templ->static_caps);
    result =
        gst_pad_template_new (name, templ->direction, templ->presence, caps);
    gst_caps_unref (caps);
  }

  return result;
}

/* alternatively;
 * API could require subclass to add templates with known/fixed name
 * but this way a subclass can choose (and preserve legacy) template/sink names
 */

/**
 * gst_sub_overlay_class_add_pad_templates:
 * @klass: #GstSubOverlay class
 * @video_templ_name: (nullable): name of video template and pad
 * @video_templ: (nullable): video pad temmplate
 * @src_templ_name: (nullable): name of src pad template
 * @src_templ: (nullable): srcpad temmplate
 *
 * Add video sink and src pads.  Defaults are used if parameters are NULL,
 * so it typically suffices to only provide some names, if so desired or needed
 * to maintain legacy names.
 */
void
gst_sub_overlay_class_add_pad_templates (GstSubOverlayClass * klass,
    const gchar * video_templ_name, GstPadTemplate * video_templ,
    const gchar * src_templ_name, GstPadTemplate * src_templ)
{
  /* sink */
  if (!video_templ) {
    if (!video_templ_name)
      video_templ_name = GST_SUB_OVERLAY_VIDEO_SINK_NAME;
    video_templ = gst_sub_overlay_class_make_pad_template (video_templ_name,
        &video_sink_template_factory);
  }

  klass->video_template = video_templ;
  gst_element_class_add_pad_template (GST_ELEMENT_CLASS (klass), video_templ);

  /* src */
  if (!src_templ) {
    if (!src_templ_name)
      src_templ_name = GST_SUB_OVERLAY_SRC_NAME;
    src_templ = gst_sub_overlay_class_make_pad_template (src_templ_name,
        &src_template_factory);
  }

  klass->src_template = src_templ;
  gst_element_class_add_pad_template (GST_ELEMENT_CLASS (klass), src_templ);
}

static void
gst_sub_overlay_class_init (GstSubOverlayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  GST_DEBUG_CATEGORY_INIT (sub_overlay_debug, "suboverlay", 0, "Sub Overlay");

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  if (private_offset != 0)
    g_type_class_adjust_private_offset (klass, &private_offset);

  gobject_class->finalize = gst_sub_overlay_finalize;
  gobject_class->set_property = gst_sub_overlay_set_property;
  gobject_class->get_property = gst_sub_overlay_get_property;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_sub_overlay_change_state);

  klass->video_sink_event = gst_sub_overlay_video_sink_event;
  klass->sub_sink_event = gst_sub_overlay_sub_sink_event;

  /**
   * GstSubOverlay:visible:
   *
   * If set, no text is rendered. Useful to switch off text rendering
   * temporarily without removing the textoverlay element from the pipeline.
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_VISIBLE,
      g_param_spec_boolean ("visible", "Visible",
          "Whether to render the overlay",
          DEFAULT_PROP_VISIBLE,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

  /**
   * GstSubOverlay:wait-text:
   *
   * If set, the video will block until a subtitle is received on the text pad.
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_WAIT_SUB,
      g_param_spec_boolean ("wait-sub", "Wait Sub",
          "Whether to wait for subtitles",
          DEFAULT_PROP_WAIT_SUB, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_sub_overlay_reset (GstSubOverlay * overlay)
{
  GST_DEBUG_OBJECT (overlay, "reset");

  overlay->priv->upstream_composition_p = NULL;
  if (overlay->priv->upstream_composition) {
    gst_video_overlay_composition_unref (overlay->priv->upstream_composition);
    overlay->priv->upstream_composition = NULL;
  }

  if (overlay->priv->composition) {
    gst_video_overlay_composition_unref (overlay->priv->composition);
    overlay->priv->composition = NULL;
  }

  if (overlay->priv->merged_composition) {
    gst_video_overlay_composition_unref (overlay->priv->merged_composition);
    overlay->priv->merged_composition = NULL;
  }

  overlay->priv->window_width = 0;
  overlay->priv->window_height = 0;

  overlay->priv->sub_flushing = FALSE;
  overlay->priv->video_flushing = FALSE;
  overlay->priv->video_eos = FALSE;
  overlay->priv->sub_eos = FALSE;

  gst_buffer_replace (&overlay->priv->sub_buffer, NULL);
  gst_buffer_replace (&overlay->priv->video_buffer, NULL);

  gst_segment_init (&overlay->segment, GST_FORMAT_TIME);
  gst_segment_init (&overlay->sub_segment, GST_FORMAT_TIME);
  gst_video_info_init (&overlay->priv->info);
}

static void
gst_sub_overlay_finalize (GObject * object)
{
  GstSubOverlay *overlay = GST_SUB_OVERLAY (object);

  gst_sub_overlay_reset (overlay);

  g_mutex_clear (&overlay->priv->evt_lock);
  g_cond_clear (&overlay->priv->evt_cond);
  g_rec_mutex_clear (&overlay->lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_sub_overlay_init (GstSubOverlay * overlay, GstSubOverlayClass * klass)
{
  GstPadTemplate *template;

  GST_DEBUG_OBJECT (overlay, "init");

  overlay->priv = gst_sub_overlay_get_instance_private (overlay);

  /* video sink */
  template = klass->video_template;
  g_return_if_fail (template != NULL);

  overlay->video_sinkpad = gst_pad_new_from_template (template,
      GST_PAD_TEMPLATE_NAME_TEMPLATE (template));
  gst_pad_set_event_function (overlay->video_sinkpad,
      GST_DEBUG_FUNCPTR (gst_sub_overlay_video_event));
  gst_pad_set_chain_function (overlay->video_sinkpad,
      GST_DEBUG_FUNCPTR (gst_sub_overlay_video_chain));
  gst_pad_set_query_function (overlay->video_sinkpad,
      GST_DEBUG_FUNCPTR (gst_sub_overlay_video_query));
  GST_PAD_SET_PROXY_ALLOCATION (overlay->video_sinkpad);
  gst_element_add_pad (GST_ELEMENT (overlay), overlay->video_sinkpad);

  /* sub sink */
  /* find the sub template; the one that is neither video nor source :-) */
  {
    template = NULL;
    GList *templates =
        gst_element_class_get_pad_template_list (GST_ELEMENT_CLASS (klass));
    while (templates) {
      if (templates->data != klass->video_template &&
          templates->data != klass->src_template) {
        template = templates->data;
        break;
      }
      templates = templates->next;
    }
  }

  if (template) {
    overlay->sub_sinkpad = gst_pad_new_from_template (template,
        GST_PAD_TEMPLATE_NAME_TEMPLATE (template));

    gst_pad_set_event_function (overlay->sub_sinkpad,
        GST_DEBUG_FUNCPTR (gst_sub_overlay_sub_event));
    gst_pad_set_chain_function (overlay->sub_sinkpad,
        GST_DEBUG_FUNCPTR (gst_sub_overlay_sub_chain));
    gst_pad_set_link_function (overlay->sub_sinkpad,
        GST_DEBUG_FUNCPTR (gst_sub_overlay_sub_pad_link));
    gst_pad_set_unlink_function (overlay->sub_sinkpad,
        GST_DEBUG_FUNCPTR (gst_sub_overlay_sub_pad_unlink));
    gst_element_add_pad (GST_ELEMENT (overlay), overlay->sub_sinkpad);
  }

  /* (video) source */
  template = klass->src_template;
  g_return_if_fail (template != NULL);

  overlay->srcpad = gst_pad_new_from_template (template,
      GST_PAD_TEMPLATE_NAME_TEMPLATE (template));
  gst_pad_set_event_function (overlay->srcpad,
      GST_DEBUG_FUNCPTR (gst_sub_overlay_src_event));
  gst_pad_set_query_function (overlay->srcpad,
      GST_DEBUG_FUNCPTR (gst_sub_overlay_src_query));
  gst_element_add_pad (GST_ELEMENT (overlay), overlay->srcpad);

  overlay->priv->wait_sub = DEFAULT_PROP_WAIT_SUB;
  overlay->priv->visible = DEFAULT_PROP_VISIBLE;

  g_rec_mutex_init (&overlay->lock);
  g_mutex_init (&overlay->priv->evt_lock);
  g_cond_init (&overlay->priv->evt_cond);
}

static gboolean
gst_sub_overlay_setcaps_sub (GstSubOverlay * overlay, GstCaps * caps)
{
  GstSubOverlayClass *klass = GST_SUB_OVERLAY_GET_CLASS (overlay);
  gboolean res = TRUE;

  GST_DEBUG_OBJECT (overlay, "caps: %" GST_PTR_FORMAT, caps);

  GST_SUB_OVERLAY_STREAM_LOCK (overlay);

  if (klass->set_format)
    res = klass->set_format (overlay, caps);

  GST_SUB_OVERLAY_STREAM_UNLOCK (overlay);

  return res;
}

static gboolean
gst_sub_overlay_set_format_video (GstSubOverlay * overlay, GstCaps * caps)
{
  GstSubOverlayClass *klass = GST_SUB_OVERLAY_GET_CLASS (overlay);
  gboolean res = TRUE;

  if (klass->set_format_video)
    res = klass->set_format_video (overlay, caps, &overlay->priv->info,
        overlay->priv->window_width, overlay->priv->window_height);

  return res;
}

static gboolean
gst_sub_overlay_can_handle_caps (GstCaps * incaps)
{
  gboolean ret;
  GstCaps *caps;

  caps = gst_static_caps_get (&sw_template_caps);
  ret = gst_caps_is_subset (incaps, caps);
  gst_caps_unref (caps);

  return ret;
}

/* only negotiate/query video overlay composition support for now */
static gboolean
gst_sub_overlay_negotiate (GstSubOverlay * overlay, GstCaps * caps)
{
  gboolean upstream_has_meta = FALSE;
  gboolean caps_has_meta = FALSE;
  gboolean alloc_has_meta = FALSE;
  gboolean attach = FALSE;
  gboolean ret = TRUE;
  guint width, height;
  GstCapsFeatures *f;
  GstCaps *overlay_caps;
  GstQuery *query;
  guint alloc_index;

  GST_DEBUG_OBJECT (overlay, "performing negotiation");

  /* Clear any pending reconfigure to avoid negotiating twice */
  gst_pad_check_reconfigure (overlay->srcpad);

  if (!caps)
    caps = gst_pad_get_current_caps (overlay->video_sinkpad);
  else
    gst_caps_ref (caps);

  if (!caps || gst_caps_is_empty (caps))
    goto no_format;

  /* Check if upstream caps have meta */
  if ((f = gst_caps_get_features (caps, 0))) {
    upstream_has_meta = gst_caps_features_contains (f,
        GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION);
  }

  /* Initialize dimensions */
  width = 0;
  height = 0;

  if (upstream_has_meta) {
    overlay_caps = gst_caps_ref (caps);
  } else {
    GstCaps *peercaps;

    /* BaseTransform requires caps for the allocation query to work */
    overlay_caps = gst_caps_copy (caps);
    f = gst_caps_get_features (overlay_caps, 0);
    gst_caps_features_add (f,
        GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION);

    /* Then check if downstream accept overlay composition in caps */
    /* FIXME: We should probably check if downstream *prefers* the
     * overlay meta, and only enforce usage of it if we can't handle
     * the format ourselves and thus would have to drop the overlays.
     * Otherwise we should prefer what downstream wants here.
     */
    peercaps = gst_pad_peer_query_caps (overlay->srcpad, overlay_caps);
    caps_has_meta = !gst_caps_is_empty (peercaps);
    gst_caps_unref (peercaps);

    GST_DEBUG_OBJECT (overlay, "caps have overlay meta %d", caps_has_meta);
  }

  if (upstream_has_meta || caps_has_meta) {
    /* Send caps immediately, it's needed by GstBaseTransform to get a reply
     * from allocation query */
    ret = gst_pad_set_caps (overlay->srcpad, overlay_caps);

    /* First check if the allocation meta has compositon */
    query = gst_query_new_allocation (overlay_caps, FALSE);

    if (!gst_pad_peer_query (overlay->srcpad, query)) {
      /* no problem, we use the query defaults */
      GST_DEBUG_OBJECT (overlay, "ALLOCATION query failed");

      /* In case we were flushing, mark reconfigure and fail this method,
       * will make it retry */
      if (overlay->priv->video_flushing)
        ret = FALSE;
    }

    alloc_has_meta = gst_query_find_allocation_meta (query,
        GST_VIDEO_OVERLAY_COMPOSITION_META_API_TYPE, &alloc_index);

    GST_DEBUG_OBJECT (overlay, "sink alloc has overlay meta %d",
        alloc_has_meta);

    if (alloc_has_meta) {
      const GstStructure *params;

      gst_query_parse_nth_allocation_meta (query, alloc_index, &params);
      if (params) {
        if (gst_structure_get (params, "width", G_TYPE_UINT, &width,
                "height", G_TYPE_UINT, &height, NULL)) {
          GST_DEBUG_OBJECT (overlay, "received window size: %dx%d", width,
              height);
          g_assert (width != 0 && height != 0);
        }
      }
    }

    gst_query_unref (query);
  }

  /* For backward compatibility, we will prefer blitting if downstream
   * allocation does not support the meta. In other case we will prefer
   * attaching, and will fail the negotiation in the unlikely case we are
   * force to blit, but format isn't supported. */

  if (upstream_has_meta) {
    attach = TRUE;
  } else if (caps_has_meta) {
    if (alloc_has_meta) {
      attach = TRUE;
    } else {
      /* Don't attach unless we cannot handle the format */
      attach = !gst_sub_overlay_can_handle_caps (caps);
    }
  } else {
    ret = gst_sub_overlay_can_handle_caps (caps);
  }

  /* If we attach, then pick the overlay caps */
  if (attach) {
    GST_DEBUG_OBJECT (overlay, "Using caps %" GST_PTR_FORMAT, overlay_caps);
    /* Caps where already sent */
  } else if (ret) {
    GST_DEBUG_OBJECT (overlay, "Using caps %" GST_PTR_FORMAT, caps);
    ret = gst_pad_set_caps (overlay->srcpad, caps);
  }

  overlay->priv->attach_compo_to_buffer = attach;

  if (!ret) {
    GST_DEBUG_OBJECT (overlay, "negotiation failed, schedule reconfigure");
    gst_pad_mark_reconfigure (overlay->srcpad);
  }

  gst_caps_unref (overlay_caps);
  gst_caps_unref (caps);

  GST_SUB_OVERLAY_STREAM_LOCK (overlay);
  /* track obtained info */
  overlay->priv->window_width = width;
  overlay->priv->window_height = height;
  /* also optionally inform subclass */
  if (ret)
    ret = gst_sub_overlay_set_format_video (overlay, caps);
  GST_SUB_OVERLAY_STREAM_UNLOCK (overlay);

  return ret;

no_format:
  {
    if (caps)
      gst_caps_unref (caps);
    gst_pad_mark_reconfigure (overlay->srcpad);
    return FALSE;
  }
}

static gboolean
gst_sub_overlay_setcaps (GstSubOverlay * overlay, GstCaps * caps)
{
  GstSubOverlayPrivate *priv = overlay->priv;
  GstVideoInfo info;
  gboolean ret = FALSE;

  if (!gst_video_info_from_caps (&info, caps))
    goto invalid_caps;

  GST_SUB_OVERLAY_STREAM_LOCK (overlay);
  /* clear composition if size situation has changed */
  if (GST_VIDEO_INFO_WIDTH (&info) != GST_VIDEO_INFO_WIDTH (&priv->info) ||
      GST_VIDEO_INFO_HEIGHT (&info) != GST_VIDEO_INFO_HEIGHT (&priv->info))
    gst_sub_overlay_set_composition (overlay, NULL);

  priv->info = info;
  GST_SUB_OVERLAY_STREAM_UNLOCK (overlay);

  /* drop lock while sending/doing lots upstream and downstream */
  ret = gst_sub_overlay_negotiate (overlay, caps);

  GST_SUB_OVERLAY_STREAM_LOCK (overlay);
  if (!priv->attach_compo_to_buffer && !gst_sub_overlay_can_handle_caps (caps)) {
    GST_DEBUG_OBJECT (overlay, "unsupported caps %" GST_PTR_FORMAT, caps);
    ret = FALSE;
  }
  GST_SUB_OVERLAY_STREAM_UNLOCK (overlay);

  return ret;

  /* ERRORS */
invalid_caps:
  {
    GST_DEBUG_OBJECT (overlay, "could not parse caps");
    return FALSE;
  }
}

static void
gst_sub_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSubOverlay *overlay = GST_SUB_OVERLAY (object);

  switch (prop_id) {
    case PROP_VISIBLE:
      overlay->priv->visible = g_value_get_boolean (value);
      break;
    case PROP_WAIT_SUB:
      overlay->priv->wait_sub = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_sub_overlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstSubOverlay *overlay = GST_SUB_OVERLAY (object);

  switch (prop_id) {
    case PROP_VISIBLE:
      g_value_set_boolean (value, overlay->priv->visible);
      break;
    case PROP_WAIT_SUB:
      g_value_set_boolean (value, overlay->priv->wait_sub);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_sub_overlay_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean ret = FALSE;
  GstSubOverlay *overlay;

  overlay = GST_SUB_OVERLAY (parent);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *filter, *caps;

      gst_query_parse_caps (query, &filter);
      caps = gst_sub_overlay_get_src_caps (pad, overlay, filter);
      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);
      ret = TRUE;
      break;
    }
    default:
      ret = gst_pad_query_default (pad, parent, query);
      break;
  }

  return ret;
}

static gboolean
gst_sub_overlay_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstSubOverlay *overlay;
  gboolean ret;

  overlay = GST_SUB_OVERLAY (parent);

  if (overlay->priv->sub_linked) {
    gst_event_ref (event);
    ret = gst_pad_push_event (overlay->video_sinkpad, event);
    gst_pad_push_event (overlay->sub_sinkpad, event);
  } else {
    ret = gst_pad_push_event (overlay->video_sinkpad, event);
  }

  return ret;
}

/* gst_sub_overlay_add_feature_and_intersect:
 *
 * Creates a new #GstCaps containing the (given caps +
 * given caps feature) + (given caps intersected by the
 * given filter).
 *
 * Returns: the new #GstCaps
 */
static GstCaps *
gst_sub_overlay_add_feature_and_intersect (GstCaps * caps,
    const gchar * feature, GstCaps * filter)
{
  int i, caps_size;
  GstCaps *new_caps;

  new_caps = gst_caps_copy (caps);

  caps_size = gst_caps_get_size (new_caps);
  for (i = 0; i < caps_size; i++) {
    GstCapsFeatures *features = gst_caps_get_features (new_caps, i);

    if (!gst_caps_features_is_any (features)) {
      gst_caps_features_add (features, feature);
    }
  }

  gst_caps_append (new_caps, gst_caps_intersect_full (caps,
          filter, GST_CAPS_INTERSECT_FIRST));

  return new_caps;
}

/* gst_sub_overlay_intersect_by_feature:
 *
 * Creates a new #GstCaps based on the following filtering rule.
 *
 * For each individual caps contained in given caps, if the
 * caps uses the given caps feature, keep a version of the caps
 * with the feature and an another one without. Otherwise, intersect
 * the caps with the given filter.
 *
 * Returns: the new #GstCaps
 */
static GstCaps *
gst_sub_overlay_intersect_by_feature (GstCaps * caps,
    const gchar * feature, GstCaps * filter)
{
  int i, caps_size;
  GstCaps *new_caps;

  new_caps = gst_caps_new_empty ();

  caps_size = gst_caps_get_size (caps);
  for (i = 0; i < caps_size; i++) {
    GstStructure *caps_structure = gst_caps_get_structure (caps, i);
    GstCapsFeatures *caps_features =
        gst_caps_features_copy (gst_caps_get_features (caps, i));
    GstCaps *filtered_caps;
    GstCaps *simple_caps =
        gst_caps_new_full (gst_structure_copy (caps_structure), NULL);
    gst_caps_set_features (simple_caps, 0, caps_features);

    if (gst_caps_features_contains (caps_features, feature)) {
      gst_caps_append (new_caps, gst_caps_copy (simple_caps));

      gst_caps_features_remove (caps_features, feature);
      filtered_caps = gst_caps_ref (simple_caps);
    } else {
      filtered_caps = gst_caps_intersect_full (simple_caps, filter,
          GST_CAPS_INTERSECT_FIRST);
    }

    gst_caps_unref (simple_caps);
    gst_caps_append (new_caps, filtered_caps);
  }

  return new_caps;
}

static GstCaps *
gst_sub_overlay_get_videosink_caps (GstPad * pad,
    GstSubOverlay * overlay, GstCaps * filter)
{
  GstPad *srcpad = overlay->srcpad;
  GstCaps *peer_caps = NULL, *caps = NULL, *overlay_filter = NULL;

  if (filter) {
    /* filter caps + composition feature + filter caps
     * filtered by the software caps. */
    GstCaps *sw_caps = gst_static_caps_get (&sw_template_caps);
    overlay_filter = gst_sub_overlay_add_feature_and_intersect (filter,
        GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION, sw_caps);
    gst_caps_unref (sw_caps);

    GST_DEBUG_OBJECT (overlay, "overlay filter %" GST_PTR_FORMAT,
        overlay_filter);
  }

  peer_caps = gst_pad_peer_query_caps (srcpad, overlay_filter);

  if (overlay_filter)
    gst_caps_unref (overlay_filter);

  if (peer_caps) {

    GST_DEBUG_OBJECT (pad, "peer caps  %" GST_PTR_FORMAT, peer_caps);

    if (gst_caps_is_any (peer_caps)) {
      /* if peer returns ANY caps, return filtered src pad template caps */
      caps = gst_caps_copy (gst_pad_get_pad_template_caps (srcpad));
    } else {

      /* duplicate caps which contains the composition into one version with
       * the meta and one without. Filter the other caps by the software caps */
      GstCaps *sw_caps = gst_static_caps_get (&sw_template_caps);
      caps = gst_sub_overlay_intersect_by_feature (peer_caps,
          GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION, sw_caps);
      gst_caps_unref (sw_caps);
    }

    gst_caps_unref (peer_caps);

  } else {
    /* no peer, our padtemplate is enough then */
    caps = gst_pad_get_pad_template_caps (pad);
  }

  if (filter) {
    GstCaps *intersection = gst_caps_intersect_full (filter, caps,
        GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (caps);
    caps = intersection;
  }

  GST_DEBUG_OBJECT (overlay, "returning  %" GST_PTR_FORMAT, caps);

  return caps;
}

static GstCaps *
gst_sub_overlay_get_src_caps (GstPad * pad, GstSubOverlay * overlay,
    GstCaps * filter)
{
  GstPad *sinkpad = overlay->video_sinkpad;
  GstCaps *peer_caps = NULL, *caps = NULL, *overlay_filter = NULL;

  if (filter) {
    /* duplicate filter caps which contains the composition into one version
     * with the meta and one without. Filter the other caps by the software
     * caps */
    GstCaps *sw_caps = gst_static_caps_get (&sw_template_caps);
    overlay_filter =
        gst_sub_overlay_intersect_by_feature (filter,
        GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION, sw_caps);
    gst_caps_unref (sw_caps);
  }

  peer_caps = gst_pad_peer_query_caps (sinkpad, overlay_filter);

  if (overlay_filter)
    gst_caps_unref (overlay_filter);

  if (peer_caps) {

    GST_DEBUG_OBJECT (pad, "peer caps  %" GST_PTR_FORMAT, peer_caps);

    if (gst_caps_is_any (peer_caps)) {

      /* if peer returns ANY caps, return filtered sink pad template caps */
      caps = gst_caps_copy (gst_pad_get_pad_template_caps (sinkpad));

    } else {

      /* return upstream caps + composition feature + upstream caps
       * filtered by the software caps. */
      GstCaps *sw_caps = gst_static_caps_get (&sw_template_caps);
      caps = gst_sub_overlay_add_feature_and_intersect (peer_caps,
          GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION, sw_caps);
      gst_caps_unref (sw_caps);
    }

    gst_caps_unref (peer_caps);

  } else {
    /* no peer, our padtemplate is enough then */
    caps = gst_pad_get_pad_template_caps (pad);
  }

  if (filter) {
    GstCaps *intersection;

    intersection =
        gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (caps);
    caps = intersection;
  }
  GST_DEBUG_OBJECT (overlay, "returning  %" GST_PTR_FORMAT, caps);

  return caps;
}

static gint
composition_n_rectangles (GstVideoOverlayComposition * comp)
{
  if (comp) {
    return gst_video_overlay_composition_n_rectangles (comp);
  } else {
    return 0;
  }
}

static void
gst_sub_overlay_merge_compositions (GstSubOverlay * overlay)
{
  GstSubOverlayPrivate *priv = overlay->priv;

  GST_OBJECT_LOCK (overlay);
  if (G_LIKELY (!priv->need_merge))
    goto exit;

  if (priv->merged_composition) {
    GST_LOG_OBJECT (overlay, "clear merged composition");
    gst_video_overlay_composition_unref (priv->merged_composition);
    priv->merged_composition = NULL;
  }

  if (!priv->upstream_composition) {
    if (priv->composition) {
      priv->merged_composition =
          gst_video_overlay_composition_copy (priv->composition);
      GST_LOG_OBJECT (overlay, "merged = copy provided %p",
          priv->merged_composition);
    }
  } else {
    priv->merged_composition =
        gst_video_overlay_composition_copy (priv->upstream_composition);
    if (priv->composition) {
      GstVideoOverlayComposition *comp = priv->composition;
      gint count = gst_video_overlay_composition_n_rectangles (comp);

      GST_LOG_OBJECT (overlay, "add %d rectangles", count);
      for (gint i = 0; i < count; ++i) {
        GstVideoOverlayRectangle *rect =
            gst_video_overlay_composition_get_rectangle (comp, i);
        GstVideoOverlayRectangle *copy =
            gst_video_overlay_rectangle_copy (rect);

        gst_video_overlay_composition_add_rectangle (priv->merged_composition,
            copy);
        gst_video_overlay_rectangle_unref (copy);
      }
    }
    GST_LOG_OBJECT (overlay, "merged into %p [%d]", priv->merged_composition,
        gst_video_overlay_composition_n_rectangles (priv->merged_composition));
  }

  /* normalize empty composition */
  if (priv->merged_composition &&
      gst_video_overlay_composition_n_rectangles (priv->merged_composition) ==
      0) {
    gst_video_overlay_composition_unref (priv->merged_composition);
    priv->merged_composition = NULL;
  }
  /* merged ok now */
  priv->need_merge = FALSE;

exit:
  GST_OBJECT_UNLOCK (overlay);
}

/**
 * gst_sub_overlay_set_composition:
 * @overlay: a #GstSubOverlay
 * @composition: a #GstSubOverlay to set as current
 *
 * Sets the provided composition as current or active composition.
 * That is, it is blended onto or attached to outgoing buffers
 * (along with possibly compositions provided by upstream).
 *
 * The baseclass will clear the current composition of provided buffers
 * become inactive.  If no buffers are provided, such (clearing) is entirely
 * up to subclass.
 */
void
gst_sub_overlay_set_composition (GstSubOverlay * overlay,
    GstVideoOverlayComposition * composition)
{
  GstSubOverlayPrivate *priv = overlay->priv;

  GST_OBJECT_LOCK (overlay);
  if (priv->composition == composition)
    goto exit;

  /* it might happen frequently if (time)overlay changes every buffer */
  GST_LOG_OBJECT (overlay, "update composition %p [%d] -> %p [%d]",
      priv->composition, composition_n_rectangles (priv->composition),
      composition, composition_n_rectangles (composition));

  if (priv->composition)
    gst_video_overlay_composition_unref (priv->composition);
  /* take ownership */
  priv->composition = composition;
  /* update combined */
  priv->need_merge = TRUE;

exit:
  GST_OBJECT_UNLOCK (overlay);
}

/* called without _STREAM_LOCK */
static GstFlowReturn
gst_sub_overlay_push_frame (GstSubOverlay * overlay, GstBuffer * video_frame)
{
  GstSubOverlayPrivate *priv = overlay->priv;
  GstSubOverlayClass *klass = GST_SUB_OVERLAY_GET_CLASS (overlay);
  gboolean apply = TRUE;
  GstVideoFrame frame;

  /* only called here from video pad processing,
   * so the latter's STREAM_LOCK covers much */
  gst_sub_overlay_merge_compositions (overlay);

  if (priv->merged_composition == NULL)
    goto done;

  video_frame = gst_buffer_make_writable (video_frame);

  /* subclass convenience; call method with lock held */
  GST_SUB_OVERLAY_STREAM_LOCK (overlay);
  if (klass->pre_apply)
    apply = klass->pre_apply (overlay, video_frame, priv->composition,
        priv->merged_composition, priv->attach_compo_to_buffer);
  GST_SUB_OVERLAY_STREAM_UNLOCK (overlay);

  if (G_UNLIKELY (!apply)) {
    GST_DEBUG_OBJECT (overlay, "skip apply");
    goto done;
  }

  if (priv->attach_compo_to_buffer) {
    /* if there was an upstream composition,
     * then remove it and add the single merged composition,
     * downstream is not likely to search for or expect several metas */
    if (priv->upstream_composition_p) {
      GST_LOG_OBJECT (overlay, "clearing upstream overlay");
      GstVideoOverlayCompositionMeta *composition_meta =
          gst_buffer_get_video_overlay_composition_meta (video_frame);
      if (G_LIKELY (composition_meta))
        gst_buffer_remove_video_overlay_composition_meta (video_frame,
            composition_meta);
    }
    GST_LOG_OBJECT (overlay, "Attaching sub overlay image to video buffer");
    gst_buffer_add_video_overlay_composition_meta (video_frame,
        priv->merged_composition);
    goto done;
  }

  if (!gst_video_frame_map (&frame, &priv->info, video_frame,
          GST_MAP_READWRITE))
    goto invalid_frame;

  gst_video_overlay_composition_blend (priv->merged_composition, &frame);

  gst_video_frame_unmap (&frame);

done:
  return gst_pad_push (overlay->srcpad, video_frame);

  /* ERRORS */
invalid_frame:
  {
    gst_buffer_unref (video_frame);
    GST_DEBUG_OBJECT (overlay, "received invalid buffer");
    return GST_FLOW_OK;
  }
}

static void
gst_sub_overlay_update_upstream_composition (GstSubOverlay * overlay,
    GstVideoOverlayComposition * comp)
{
  GstSubOverlayPrivate *priv = overlay->priv;

  GST_OBJECT_LOCK (overlay);
  if (comp != priv->upstream_composition) {
    GST_DEBUG_OBJECT (overlay,
        "update upstream GstVideoOverlayCompositionMeta %p", comp);
    priv->upstream_composition_p = comp;
    if (priv->upstream_composition)
      gst_video_overlay_composition_unref (priv->upstream_composition);
    priv->upstream_composition =
        comp ? gst_video_overlay_composition_copy (comp) : comp;
    priv->need_merge = TRUE;
  }
  GST_OBJECT_UNLOCK (overlay);
}

static GstPadLinkReturn
gst_sub_overlay_sub_pad_link (GstPad * pad, GstObject * parent, GstPad * peer)
{
  GstSubOverlay *overlay = GST_SUB_OVERLAY (parent);

  if (G_UNLIKELY (!overlay))
    return GST_PAD_LINK_REFUSED;

  GST_DEBUG_OBJECT (overlay, "Sub pad linked");

  overlay->priv->sub_linked = TRUE;
  gst_sub_overlay_update_upstream_composition (overlay, NULL);

  return GST_PAD_LINK_OK;
}

static void
gst_sub_overlay_sub_pad_unlink (GstPad * pad, GstObject * parent)
{
  GstSubOverlay *overlay = GST_SUB_OVERLAY (parent);

  GST_DEBUG_OBJECT (overlay, "Sub pad unlinked");

  overlay->priv->sub_linked = FALSE;
  gst_sub_overlay_update_upstream_composition (overlay, NULL);

  gst_segment_init (&overlay->sub_segment, GST_FORMAT_TIME);
}

static gboolean
gst_sub_overlay_clip_buffer (GstSubOverlay * overlay, const GstSegment * seg,
    GstBuffer ** buf)
{
  GstClockTime clip_start = 0, clip_stop = 0;
  GstBuffer *buffer = *buf;
  gboolean in_seg;

  if (G_LIKELY (GST_BUFFER_TIMESTAMP_IS_VALID (buffer))) {
    GstClockTime stop;

    if (G_LIKELY (GST_BUFFER_DURATION_IS_VALID (buffer)))
      stop = GST_BUFFER_TIMESTAMP (buffer) + GST_BUFFER_DURATION (buffer);
    else
      stop = GST_CLOCK_TIME_NONE;

    in_seg = gst_segment_clip (seg, GST_FORMAT_TIME,
        GST_BUFFER_TIMESTAMP (buffer), stop, &clip_start, &clip_stop);
  } else {
    in_seg = TRUE;
  }

  if (in_seg) {
    /* about to change metadata */
    *buf = gst_buffer_make_writable (buffer);
    /* arrange for timestamps within segment;
     * so a later conversion to running_time does not yield _NONE */
    if (GST_BUFFER_TIMESTAMP_IS_VALID (buffer))
      GST_BUFFER_TIMESTAMP (buffer) = clip_start;
    if (GST_BUFFER_DURATION_IS_VALID (buffer))
      GST_BUFFER_DURATION (buffer) = clip_stop - clip_start;
  }

  return in_seg;
}

static gboolean
gst_sub_overlay_flush_sub (GstSubOverlay * overlay)
{
  GstSubOverlayClass *klass = GST_SUB_OVERLAY_GET_CLASS (overlay);
  gboolean ret = TRUE;

  GST_DEBUG_OBJECT (overlay, "flush");

  GST_SUB_OVERLAY_STREAM_LOCK (overlay);

  /* clear any pending EOS and segment */
  overlay->priv->sub_flushing = FALSE;
  overlay->priv->sub_eos = FALSE;
  gst_sub_overlay_pop_sub (overlay);
  gst_segment_init (&overlay->sub_segment, GST_FORMAT_TIME);

  if (klass->flush)
    ret = klass->flush (overlay);

  GST_SUB_OVERLAY_STREAM_UNLOCK (overlay);

  return ret;
}

static gboolean
gst_sub_overlay_sub_sink_event (GstSubOverlay * overlay, GstEvent * event)
{
  gboolean ret = FALSE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_STREAM_START:
      GST_INFO_OBJECT (overlay, "sub stream-start");
      gst_sub_overlay_flush_sub (overlay);
      gst_event_replace (&event, NULL);
      ret = TRUE;
      break;
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      ret = gst_sub_overlay_setcaps_sub (overlay, caps);
      gst_event_replace (&event, NULL);
      break;
    }
    case GST_EVENT_SEGMENT:
    {
      const GstSegment *segment;

      gst_event_parse_segment (event, &segment);

      GST_SUB_OVERLAY_STREAM_LOCK (overlay);
      overlay->priv->sub_eos = FALSE;
      if (segment->format == GST_FORMAT_TIME) {
        GstClockTime ts;

        gst_segment_copy_into (segment, &overlay->sub_segment);
        segment = &overlay->sub_segment;

        GST_DEBUG_OBJECT (overlay, "SUB SEGMENT now: %" GST_SEGMENT_FORMAT,
            segment);

        /* ensure position within segment */
        gst_segment_clip (segment, GST_FORMAT_TIME, segment->position,
            GST_CLOCK_TIME_NONE, &ts, NULL);
        overlay->sub_segment.position = ts;

        /* align stored buffer timestamp with updated segment;
         * avoid _NONE running time that way
         * also avoid overlay on wrong video */
        if (overlay->priv->sub_buffer)
          gst_sub_overlay_clip_buffer (overlay, segment,
              &overlay->priv->sub_buffer);

        /* wake up the video chain, it might be waiting for a sub buffer or
         * a sub segment update */
        GST_SUB_OVERLAY_BROADCAST (overlay);
      } else {
        GST_ELEMENT_WARNING (overlay, STREAM, MUX, (NULL),
            ("received non-TIME newsegment event on sub input"));
      }
      GST_SUB_OVERLAY_STREAM_UNLOCK (overlay);

      gst_event_replace (&event, NULL);
      ret = TRUE;
      break;
    }
    case GST_EVENT_GAP:
    {
      GstClockTime start, duration;

      GST_SUB_OVERLAY_STREAM_LOCK (overlay);
      gst_event_parse_gap (event, &start, &duration);
      if (GST_CLOCK_TIME_IS_VALID (duration))
        start += duration;
      /* we do not expect another buffer until after gap,
       * so that is our position now */
      overlay->sub_segment.position = start;

      /* wake up the video chain, it might be waiting for a sub buffer or
       * a sub segment update */
      GST_SUB_OVERLAY_BROADCAST (overlay);
      GST_SUB_OVERLAY_STREAM_UNLOCK (overlay);

      gst_event_replace (&event, NULL);
      ret = TRUE;
      break;
    }
    case GST_EVENT_FLUSH_STOP:
      GST_INFO_OBJECT (overlay, "sub flush stop");
      gst_sub_overlay_flush_sub (overlay);
      gst_event_replace (&event, NULL);
      ret = TRUE;
      break;
    case GST_EVENT_FLUSH_START:
      GST_SUB_OVERLAY_STREAM_LOCK (overlay);
      GST_INFO_OBJECT (overlay, "sub flush start");
      overlay->priv->sub_flushing = TRUE;
      GST_SUB_OVERLAY_BROADCAST (overlay);
      GST_SUB_OVERLAY_STREAM_UNLOCK (overlay);
      gst_event_replace (&event, NULL);
      ret = TRUE;
      break;
    case GST_EVENT_EOS:
      GST_SUB_OVERLAY_STREAM_LOCK (overlay);
      overlay->priv->sub_eos = TRUE;
      GST_INFO_OBJECT (overlay, "sub EOS");
      /* wake up the video chain, it might be waiting for a sub buffer or
       * a sub segment update */
      GST_SUB_OVERLAY_BROADCAST (overlay);
      GST_SUB_OVERLAY_STREAM_UNLOCK (overlay);
      gst_event_replace (&event, NULL);
      ret = TRUE;
      break;
    default:
      break;
  }

  if (event)
    ret = gst_pad_event_default (overlay->sub_sinkpad,
        GST_OBJECT_CAST (overlay), event);

  return ret;
}

static gboolean
gst_sub_overlay_sub_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstSubOverlay *overlay = GST_SUB_OVERLAY (parent);
  GstSubOverlayClass *klass = GST_SUB_OVERLAY_GET_CLASS (overlay);
  gboolean ret = FALSE;

  GST_DEBUG_OBJECT (pad, "received event %s", GST_EVENT_TYPE_NAME (event));

  if (klass->sub_sink_event)
    ret = klass->sub_sink_event (overlay, event);
  else {
    gst_event_unref (event);
    ret = FALSE;
  }

  return ret;
}

static gboolean
gst_sub_overlay_check_video_after_sub (GstSubOverlay * overlay,
    GstClockTime sub_run_ts)
{
  GstClockTime vid_end_running_time;

  /* preferably represent end of video */
  vid_end_running_time =
      gst_segment_to_running_time (&overlay->segment, GST_FORMAT_TIME,
      overlay->segment.position);

  /* in unlikely case of no valid sub ts,
   * there is not that much sensible to do,
   * so the choice is to give it a one-shot chance (to overlay any video),
   * until some better comes along that can be properly matched */
  return !GST_CLOCK_TIME_IS_VALID (sub_run_ts) ||
      (GST_CLOCK_TIME_IS_VALID (vid_end_running_time) &&
      sub_run_ts < vid_end_running_time);
}

static void
gst_sub_overlay_update_video_position (GstSubOverlay * overlay, GstClockTime ts)
{
  GstSubOverlayPrivate *priv = overlay->priv;

  if (GST_CLOCK_TIME_IS_VALID (ts) && ts > overlay->segment.position) {
    overlay->segment.position = ts;
    /* video moved to new position;
     * signal sub which might be waiting to advance to next buffer
     * however, only do so if it will have an effect,
     * as opposed to superfluously waking up sub on each video buffer */
    if (priv->sub_waiting &&
        gst_sub_overlay_check_video_after_sub (overlay, priv->sub_next_run_ts))
      GST_SUB_OVERLAY_BROADCAST (overlay);
  }
}

static void
gst_sub_overlay_advance_video (GstSubOverlay * overlay, GstClockTime ts)
{
  GstSubOverlayPrivate *priv = overlay->priv;

  if (priv->sparse_video && GST_CLOCK_TIME_IS_VALID (ts) &&
      ts > overlay->segment.position && priv->video_buffer) {
    GstBuffer *buf = gst_buffer_copy (priv->video_buffer);

    /* in sparse video mode, we still have the last buffer around;
     * so stamp that one suitably and send that one */
    GST_BUFFER_TIMESTAMP (buf) = ts;
    gst_sub_overlay_video_chain (overlay->video_sinkpad,
        GST_OBJECT_CAST (overlay), buf);
  } else {
    /* otherwise simply update some state tracking */
    gst_sub_overlay_update_video_position (overlay, ts);
  }
}

static gboolean
gst_sub_overlay_flush_video (GstSubOverlay * overlay)
{
  gboolean ret = TRUE;

  GST_DEBUG_OBJECT (overlay, "flush");

  GST_SUB_OVERLAY_STREAM_LOCK (overlay);

  /* clear any pending EOS and segment */
  overlay->priv->video_flushing = FALSE;
  overlay->priv->video_eos = FALSE;
  gst_buffer_replace (&overlay->priv->video_buffer, NULL);
  gst_segment_init (&overlay->segment, GST_FORMAT_TIME);

  GST_SUB_OVERLAY_STREAM_UNLOCK (overlay);

  return ret;
}

static gboolean
gst_sub_overlay_video_sink_event (GstSubOverlay * overlay, GstEvent * event)
{
  gboolean ret = FALSE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_STREAM_START:
      /* Clear any EOS and segment on a new stream */
      GST_INFO_OBJECT (overlay, "video stream-start");
      gst_sub_overlay_flush_video (overlay);
      break;
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      ret = gst_sub_overlay_setcaps (overlay, caps);
      gst_event_unref (event);
      event = NULL;
      break;
    }
    case GST_EVENT_SEGMENT:
    {
      const GstSegment *segment;

      GST_DEBUG_OBJECT (overlay, "received new segment");
      gst_event_parse_segment (event, &segment);

      GST_SUB_OVERLAY_STREAM_LOCK (overlay);
      /* old style gap specified by updated segment.start */
      gst_sub_overlay_advance_video (overlay, segment->start);

      if (segment->format == GST_FORMAT_TIME) {
        GstClockTime ts;

        gst_segment_copy_into (segment, &overlay->segment);
        segment = &overlay->segment;
        GST_DEBUG_OBJECT (overlay, "VIDEO SEGMENT now: %" GST_SEGMENT_FORMAT,
            segment);

        /* ensure position within segment */
        gst_segment_clip (segment, GST_FORMAT_TIME, segment->position,
            GST_CLOCK_TIME_NONE, &ts, NULL);
        overlay->segment.position = ts;
      } else {
        GST_ELEMENT_WARNING (overlay, STREAM, MUX, (NULL),
            ("received non-TIME newsegment event on video input"));
      }
      GST_SUB_OVERLAY_STREAM_UNLOCK (overlay);
      break;
    }
    case GST_EVENT_GAP:
    {
      GstClockTime timestamp, duration;
      gst_event_parse_gap (event, &timestamp, &duration);
      if (GST_CLOCK_TIME_IS_VALID (duration))
        timestamp += duration;

      GST_SUB_OVERLAY_STREAM_LOCK (overlay);
      GST_LOG_OBJECT (overlay,
          "received video GAP; advancing to %" GST_TIME_FORMAT,
          GST_TIME_ARGS (timestamp));
      gst_sub_overlay_advance_video (overlay, timestamp);
      GST_SUB_OVERLAY_STREAM_UNLOCK (overlay);

      gst_event_unref (event);
      event = NULL;
      break;
    }
    case GST_EVENT_EOS:
      GST_SUB_OVERLAY_STREAM_LOCK (overlay);
      GST_INFO_OBJECT (overlay, "video EOS");
      overlay->priv->video_eos = TRUE;
      GST_SUB_OVERLAY_STREAM_UNLOCK (overlay);
      break;
    case GST_EVENT_FLUSH_START:
      GST_INFO_OBJECT (overlay, "video flush start");
      GST_SUB_OVERLAY_STREAM_LOCK (overlay);
      overlay->priv->video_flushing = TRUE;
      GST_SUB_OVERLAY_BROADCAST (overlay);
      GST_SUB_OVERLAY_STREAM_UNLOCK (overlay);
      break;
    case GST_EVENT_FLUSH_STOP:
      GST_INFO_OBJECT (overlay, "video flush stop");
      gst_sub_overlay_flush_video (overlay);
      break;
    default:
      break;
  }

  if (event)
    ret = gst_pad_event_default (overlay->video_sinkpad,
        GST_OBJECT_CAST (overlay), event);

  return ret;
}

static gboolean
gst_sub_overlay_video_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  gboolean ret = FALSE;
  GstSubOverlay *overlay = GST_SUB_OVERLAY (parent);
  GstSubOverlayClass *klass = GST_SUB_OVERLAY_GET_CLASS (overlay);

  GST_DEBUG_OBJECT (pad, "received event %s", GST_EVENT_TYPE_NAME (event));

  if (klass->video_sink_event)
    ret = klass->video_sink_event (overlay, event);
  else {
    gst_event_unref (event);
    ret = FALSE;
  }

  return ret;
}

static gboolean
gst_sub_overlay_video_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean ret = FALSE;
  GstSubOverlay *overlay;

  overlay = GST_SUB_OVERLAY (parent);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *filter, *caps;

      gst_query_parse_caps (query, &filter);
      caps = gst_sub_overlay_get_videosink_caps (pad, overlay, filter);
      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);
      ret = TRUE;
      break;
    }
    default:
      ret = gst_pad_query_default (pad, parent, query);
      break;
  }

  return ret;
}

/* Called with STREAM_LOCK held */
static void
gst_sub_overlay_pop_sub (GstSubOverlay * overlay)
{
  GstSubOverlayPrivate *priv = overlay->priv;

  if (priv->sub_buffer) {
    GST_DEBUG_OBJECT (overlay, "releasing sub buffer %p", priv->sub_buffer);
    gst_buffer_replace (&priv->sub_buffer, NULL);
  }
  /* Let the sub task know we used that buffer */
  GST_SUB_OVERLAY_BROADCAST (overlay);
}

/**
 * gst_sub_overlay_update_sub_position:
 * @overlay: a #GstSubOverlay
 * @ts: latest (stream) time of available sub data
 *
 * Notifies the subclass that (video) time can now advance to specified time.
 * As such, waiting for such is now no longer needed.
 *
 * This is implicitly done as part of gst_sub_overlay_update_sub_buffer().
 * While is not necessary for the subclass to invoke either of those, it is
 * highly recommended to support waiting of video (if so enabled).
 */
void
gst_sub_overlay_update_sub_position (GstSubOverlay * overlay, GstClockTime ts)
{
  if (GST_CLOCK_TIME_IS_VALID (ts))
    overlay->sub_segment.position = ts;
  /* in case the video chain is waiting for a sub buffer, wake it up */
  GST_SUB_OVERLAY_BROADCAST (overlay);
}


/**
 * gst_sub_overlay_update_sub_buffer:
 * @overlay: a #GstSubOverlay
 * @buffer: (transfer full) (nullable): a sub buffer
 * @force: whether to unconditionally replace any current sub buffer
 *
 * Updates the (essentially 1-element queue) sub buffer managed by baseclass.
 * The latter means that subclass will match the sub buffer's time against
 * advancing video to decide when it becomes active and can be dropped.
 * Usually, only an active buffer is provided to the @render method and a
 * current composition is cleared when no (provided) buffer is active.
 *
 * It is up to the subclass whether it considers the buffer when passed to
 * @render or whether it uses other (internal) state at that time.
 * In this regard, note that a #GstBuffer can be used as opaque data in many
 * ways.  It could carry actual input (e.g. text markup) optionally along
 * with (custom) #GstMeta.  Alternatively, it can wrap any custom structure
 * using @gst_buffer_new_wrapped_full or a (pre-computed)
 * #GstVideoOverlayComposition can be attached to a (pottentially dummy) buffer.
 * In the latter case, however, such could then not consider any relevant
 * video or window geometry, so the composition may have to be adjusted in
 * that regard later on (e.g. at @render time).
 *
 * Note that this function should be called with STREAM_LOCK and may wait
 * (if a buffer is already pending).  The latter will not occur if @force
 * is TRUE or @buffer has invalid time.
 *
 * Returns: a #GstFlowReturn, e.g. GST_FLOW_OK if provided buffer is now current
 */
GstFlowReturn
gst_sub_overlay_update_sub_buffer (GstSubOverlay * overlay, GstBuffer * buffer,
    gboolean force)
{
  GstSubOverlayPrivate *priv = overlay->priv;
  GstFlowReturn ret = GST_FLOW_OK;
  gboolean in_seg = FALSE;

  /* mark API usage */
  priv->got_sub_buffer = TRUE;

  if (priv->sub_flushing) {
    GST_SUB_OVERLAY_STREAM_UNLOCK (overlay);
    ret = GST_FLOW_FLUSHING;
    GST_LOG_OBJECT (overlay, "sub flushing");
    goto beach;
  }

  if (priv->sub_eos) {
    GST_SUB_OVERLAY_STREAM_UNLOCK (overlay);
    ret = GST_FLOW_EOS;
    GST_LOG_OBJECT (overlay, "sub EOS");
    goto beach;
  }

  if (!buffer) {
    GST_DEBUG_OBJECT (overlay, "clear buffer");
    gst_sub_overlay_pop_sub (overlay);
    goto beach;
  }

  GST_LOG_OBJECT (overlay, "update force=%d ts=%" GST_TIME_FORMAT ", duration=%"
      GST_TIME_FORMAT, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)), force,
      GST_TIME_ARGS (GST_BUFFER_DURATION (buffer)));

  in_seg = gst_sub_overlay_clip_buffer (overlay, &overlay->sub_segment,
      &buffer);

  if (in_seg) {
    GstClockTime sub_running_time;

    sub_running_time =
        gst_segment_to_running_time (&overlay->sub_segment, GST_FORMAT_TIME,
        GST_BUFFER_TIMESTAMP (buffer));

    /* optimization; record what we are waiting for */
    priv->sub_next_run_ts = sub_running_time;

    while (priv->sub_buffer != NULL) {
      /* so there is a pending sub buffer
       * however, if video has moved beyond new buffer's start,
       * it's time to drop the previous one regardless */
      if (force ||
          gst_sub_overlay_check_video_after_sub (overlay, sub_running_time)) {
        gst_sub_overlay_pop_sub (overlay);
        continue;
      }

      GST_DEBUG_OBJECT (overlay, "sub buffer queued, waiting");
      priv->sub_waiting = TRUE;
      GST_SUB_OVERLAY_WAIT (overlay);
      GST_DEBUG_OBJECT (overlay, "resuming");
      priv->sub_waiting = FALSE;
      if (priv->sub_flushing) {
        GST_SUB_OVERLAY_STREAM_UNLOCK (overlay);
        ret = GST_FLOW_FLUSHING;
        goto beach;
      }
    }

    /* no longer waiting */
    priv->sub_next_run_ts = GST_CLOCK_TIME_NONE;

    /* commit to this buffer as latest sub state */
    gst_sub_overlay_update_sub_position (overlay,
        GST_BUFFER_TIMESTAMP (buffer));

    /* pass ownership */
    priv->sub_buffer = buffer;
    buffer = NULL;
    /* invaidate current overlay */
    gst_sub_overlay_set_composition (overlay, NULL);
  }

beach:
  if (buffer)
    gst_buffer_unref (buffer);

  return ret;
}

static GstFlowReturn
gst_sub_overlay_sub_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstSubOverlay *overlay = GST_SUB_OVERLAY (parent);
  GstSubOverlayClass *klass = GST_SUB_OVERLAY_GET_CLASS (overlay);

  g_return_val_if_fail (klass->handle_buffer, GST_FLOW_ERROR);

  GST_LOG_OBJECT (overlay, "%" GST_SEGMENT_FORMAT "  BUFFER: ts=%"
      GST_TIME_FORMAT ", end=%" GST_TIME_FORMAT, &overlay->segment,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)),
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer) +
          GST_BUFFER_DURATION (buffer)));

  /* called without STREAM_LOCK */
  ret = klass->handle_buffer (overlay, buffer);

  return ret;
}

static GstFlowReturn
gst_sub_overlay_video_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstSubOverlay *overlay = GST_SUB_OVERLAY (parent);
  GstSubOverlayPrivate *priv = overlay->priv;
  GstSubOverlayClass *klass = GST_SUB_OVERLAY_GET_CLASS (overlay);
  GstFlowReturn ret = GST_FLOW_OK;
  gboolean in_seg = FALSE;
  guint64 start, stop, clip_start = 0, clip_stop = 0;
  GstVideoOverlayCompositionMeta *composition_meta;
  GstClockTime vid_ts, vid_ts_end;
  gboolean render = TRUE;
  GstClockTime sub_ts = GST_CLOCK_TIME_NONE;
  GstClockTime sub_ts_end = GST_CLOCK_TIME_NONE;

  g_return_val_if_fail (klass->render, GST_FLOW_ERROR);

  composition_meta = gst_buffer_get_video_overlay_composition_meta (buffer);
  gst_sub_overlay_update_upstream_composition (overlay,
      composition_meta ? composition_meta->overlay : NULL);

  /* (re)negotiate if needed to obtain latest downstream geometry
   * so the latter is available prior to render below */
  if (gst_pad_check_reconfigure (overlay->srcpad)) {
    if (!gst_sub_overlay_negotiate (overlay, NULL)) {
      gst_pad_mark_reconfigure (overlay->srcpad);
      gst_buffer_unref (buffer);
      if (GST_PAD_IS_FLUSHING (overlay->srcpad))
        return GST_FLOW_FLUSHING;
      else
        return GST_FLOW_NOT_NEGOTIATED;
    }
  }

  /* track buffer in suitable mode */
  if (priv->sparse_video)
    gst_buffer_replace (&priv->video_buffer, buffer);

  /* sanitize time to compute running time */
  start = GST_BUFFER_TIMESTAMP (buffer);
  if (!GST_BUFFER_DURATION_IS_VALID (buffer)) {
    stop = GST_CLOCK_TIME_NONE;
  } else if (GST_BUFFER_TIMESTAMP_IS_VALID (buffer)) {
    stop = start + GST_BUFFER_DURATION (buffer);
  } else {
    stop = GST_CLOCK_TIME_NONE;
  }

  GST_LOG_OBJECT (overlay, "%" GST_SEGMENT_FORMAT "  BUFFER: ts=%"
      GST_TIME_FORMAT ", end=%" GST_TIME_FORMAT, &overlay->segment,
      GST_TIME_ARGS (start), GST_TIME_ARGS (stop));

  /* never mind combining both streams if video out-of-segment;
   * no sane running time to do so in that case */
  in_seg = gst_segment_clip (&overlay->segment, GST_FORMAT_TIME, start, stop,
      &clip_start, &clip_stop);
  if (!in_seg) {
    /* either drop or simply pass along */
    if (priv->preserve_ts) {
      GST_DEBUG_OBJECT (overlay, "buffer out of segment, pushing");
      return gst_pad_push (overlay->srcpad, buffer);
    } else {
      GST_DEBUG_OBJECT (overlay, "buffer out of segment, discarding");
      gst_buffer_unref (buffer);
      return GST_FLOW_OK;
    }
  }

  /* preferably; buffer timestamps are not changed
   * the job here is to match with overlay, not to affect otherwise,
   * still, optionally do so if requested */
  if (!priv->preserve_ts &&
      (clip_start != start || (stop != -1 && clip_stop != stop))) {
    GST_DEBUG_OBJECT (overlay, "clipping buffer timestamp/duration to segment");
    buffer = gst_buffer_make_writable (buffer);
    GST_BUFFER_TIMESTAMP (buffer) = clip_start;
    if (stop != -1)
      GST_BUFFER_DURATION (buffer) = clip_stop - clip_start;
  }

  /* regardless of the above, use computed values in the sequel */

  vid_ts = gst_segment_to_running_time (&overlay->segment, GST_FORMAT_TIME,
      clip_start);
  vid_ts_end = gst_segment_to_running_time (&overlay->segment, GST_FORMAT_TIME,
      clip_stop);
  GST_LOG_OBJECT (overlay,
      "video running %" GST_TIME_FORMAT " - %" GST_TIME_FORMAT,
      GST_TIME_ARGS (vid_ts), GST_TIME_ARGS (vid_ts_end));

  GST_SUB_OVERLAY_STREAM_LOCK (overlay);

  /* update to the end as the lastest known time;
   * so the most recent sub buffer can be used ASAP */
  gst_sub_overlay_update_video_position (overlay,
      GST_CLOCK_TIME_IS_VALID (clip_stop) ? clip_stop : clip_start);

  if (klass->advance && GST_CLOCK_TIME_IS_VALID (vid_ts))
    klass->advance (overlay, buffer, vid_ts, vid_ts_end);

wait_for_sub_buf:

  if (priv->video_flushing)
    goto flushing;

  if (priv->video_eos)
    goto have_eos;

  if (!overlay->priv->visible) {
    GST_SUB_OVERLAY_STREAM_UNLOCK (overlay);
    GST_LOG_OBJECT (overlay, "render disabled");
    ret = gst_pad_push (overlay->srcpad, buffer);
    return ret;
  }

  /* optional sub running time */
  if (priv->sub_buffer) {
    GstClockTime sub_start = GST_BUFFER_TIMESTAMP (priv->sub_buffer);
    GstClockTime sub_duration = GST_BUFFER_DURATION (priv->sub_buffer);

    if (GST_CLOCK_TIME_IS_VALID (sub_start)) {
      sub_ts =
          gst_segment_to_running_time (&overlay->sub_segment, GST_FORMAT_TIME,
          sub_start);
      if (GST_CLOCK_TIME_IS_VALID (sub_duration))
        sub_ts_end =
            gst_segment_to_running_time (&overlay->sub_segment,
            GST_FORMAT_TIME, sub_start + sub_duration);
    }
    GST_LOG_OBJECT (overlay,
        "sub running %" GST_TIME_FORMAT " - %" GST_TIME_FORMAT,
        GST_TIME_ARGS (sub_ts), GST_TIME_ARGS (sub_ts_end));
  }

  /* pop sub buffer if we moved past it */
  if (priv->sub_buffer) {
    if (GST_CLOCK_TIME_IS_VALID (vid_ts) &&
        GST_CLOCK_TIME_IS_VALID (sub_ts_end) && sub_ts_end <= vid_ts) {
      gst_sub_overlay_pop_sub (overlay);
    }
  }

  /* if needed, wait before moving along too far */
  if (priv->wait_sub && priv->sub_linked && !priv->sub_eos && !priv->sub_buffer) {
    GstClockTime sub_pos_run =
        gst_segment_to_running_time (&overlay->sub_segment, GST_FORMAT_TIME,
        overlay->sub_segment.position);

    GST_LOG_OBJECT (overlay, "no buffer, sub pos running %" GST_TIME_FORMAT,
        GST_TIME_ARGS (sub_pos_run));

    if (GST_CLOCK_TIME_IS_VALID (sub_pos_run) &&
        GST_CLOCK_TIME_IS_VALID (vid_ts) && vid_ts >= sub_pos_run) {
      GST_DEBUG_OBJECT (overlay, "no sub buffer, need to wait for one");
      GST_SUB_OVERLAY_WAIT (overlay);
      GST_DEBUG_OBJECT (overlay, "resuming");
      goto wait_for_sub_buf;
    }
  }

  /* some default advance (composition) management
   * if sub buffers are provided */
  if (priv->got_sub_buffer) {
    if (!priv->sub_buffer) {
      render = FALSE;
    } else if (GST_CLOCK_TIME_IS_VALID (sub_ts) &&
        ((GST_CLOCK_TIME_IS_VALID (vid_ts_end) && vid_ts_end <= sub_ts) ||
            (GST_CLOCK_TIME_IS_VALID (vid_ts) && vid_ts < sub_ts))) {
      /* check further above ensured that video is not past sub;
       * forego render if sub is ahead of video, otherwise there is overlap */
      /* NOTE; in the check above, ideally the buffer has a valid duration,
       * in which case we can consider the end time
       * however, if that is lacking, it makes not much sense in this check
       * to interpret that as infinite duration,
       * so it is more useful to simply check the start time
       * rather than also invent any fake duration
       * (which is typically much smaller than a sub overlay duration) */
      GST_LOG_OBJECT (overlay, "sub in future");
      render = FALSE;
    }
    /* no render also means also really no overlay either */
    if (!render)
      gst_sub_overlay_set_composition (overlay, NULL);
  }

  GST_LOG_OBJECT (overlay, "render:%d composition:%p", render,
      priv->composition);
  /* optionally do invoke render if so configured */
  if (!priv->composition &&
      ((render && priv->sub_buffer) || priv->render_no_buffer))
    klass->render (overlay, render ? priv->sub_buffer : NULL);

  GST_SUB_OVERLAY_STREAM_UNLOCK (overlay);

  ret = gst_sub_overlay_push_frame (overlay, buffer);
  return ret;

flushing:
  {
    GST_SUB_OVERLAY_STREAM_UNLOCK (overlay);
    GST_DEBUG_OBJECT (overlay, "flushing, discarding buffer");
    gst_buffer_unref (buffer);
    return GST_FLOW_FLUSHING;
  }
have_eos:
  {
    GST_SUB_OVERLAY_STREAM_UNLOCK (overlay);
    GST_DEBUG_OBJECT (overlay, "eos, discarding buffer");
    gst_buffer_unref (buffer);
    return GST_FLOW_EOS;
  }
}


static gboolean
gst_sub_overlay_stop (GstSubOverlay * overlay)
{
  GstSubOverlayClass *klass = GST_SUB_OVERLAY_GET_CLASS (overlay);
  gboolean ret = TRUE;

  GST_DEBUG_OBJECT (overlay, "stop");

  GST_SUB_OVERLAY_STREAM_LOCK (overlay);
  if (klass->stop)
    ret = klass->stop (overlay);
  GST_SUB_OVERLAY_STREAM_UNLOCK (overlay);

  /* clean up */
  gst_sub_overlay_reset (overlay);

  return ret;
}

static gboolean
gst_sub_overlay_start (GstSubOverlay * overlay)
{
  GstSubOverlayClass *klass = GST_SUB_OVERLAY_GET_CLASS (overlay);
  gboolean ret = TRUE;

  GST_DEBUG_OBJECT (overlay, "start");

  /* arrange clean state */
  gst_sub_overlay_reset (overlay);

  GST_SUB_OVERLAY_STREAM_LOCK (overlay);
  if (klass->start)
    ret = klass->start (overlay);
  GST_SUB_OVERLAY_STREAM_UNLOCK (overlay);

  return ret;
}

static GstStateChangeReturn
gst_sub_overlay_change_state (GstElement * element, GstStateChange transition)
{
  GstSubOverlay *overlay = GST_SUB_OVERLAY (element);
  GstSubOverlayPrivate *priv = overlay->priv;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_SUB_OVERLAY_STREAM_LOCK (overlay);
      /* unblock waiting */
      priv->sub_flushing = TRUE;
      priv->video_flushing = TRUE;
      GST_SUB_OVERLAY_BROADCAST (overlay);
      GST_SUB_OVERLAY_STREAM_UNLOCK (overlay);
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (!gst_sub_overlay_start (overlay)) {
        goto start_failed;
      }
      break;
    default:
      break;
  }

  ret = parent_class->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (!gst_sub_overlay_stop (overlay)) {
        goto stop_failed;
      }
      break;
    default:
      break;
  }

  return ret;

start_failed:
  {
    GST_ELEMENT_ERROR (overlay, LIBRARY, INIT, (NULL),
        ("Failed to start overlay"));
    return GST_STATE_CHANGE_FAILURE;
  }
stop_failed:
  {
    GST_ELEMENT_ERROR (overlay, LIBRARY, INIT, (NULL),
        ("Failed to stop overlay"));
    return GST_STATE_CHANGE_FAILURE;
  }
}

/**
 * gst_sub_overlay_get_output_format:
 * @overlay: a #GstSubOverlay
 * @info: (out): output set to video #GstVideoInfo
 * @ww: (out): set to render window width (0 if unknown)
 * @wh: (out): set to render window height (0 if unknown)
 *
 * Queries current negotiated configuration of video stream and
 * downstream render window (if available).
 */
void
gst_sub_overlay_get_output_format (GstSubOverlay * overlay,
    GstVideoInfo ** info, gint * ww, gint * wh)
{
  if (info)
    *info = &overlay->priv->info;
  if (ww)
    *ww = overlay->priv->window_width;
  if (wh)
    *wh = overlay->priv->window_height;
}

/**
 * gst_sub_overlay_get_linked:
 * @overlay: a #GstSubOverlay
 *
 * Reports whether sub pad is currently linked.
 *
 * Returns: whether sub pad is linked
 */
gboolean
gst_sub_overlay_get_linked (GstSubOverlay * overlay)
{
  return overlay->priv->sub_linked;
}

/**
 * gst_sub_overlay_get_buffers:
 * @overlay: a #GstSubOverlay
 * @video: a pointer to a #GstBuffer
 * @sub: a pointer to a #GstBuffer
 *
 * Returns most recent video and sub buffers, as supplied by upstream or
 * subclass.
 */
void
gst_sub_overlay_get_buffers (GstSubOverlay * overlay,
    GstBuffer ** video, GstBuffer ** sub)
{
  if (video)
    *video = overlay->priv->video_buffer;
  if (sub)
    *sub = overlay->priv->sub_buffer;
}

/**
 * gst_sub_overlay_set_visible:
 * @overlay: a #GstSubOverlay
 * @enable: new enabled state
 *
 * Sets enabled state of overlay rendering.
 */
void
gst_sub_overlay_set_visible (GstSubOverlay * overlay, gboolean enable)
{
  overlay->priv->visible = enable;
}

/**
 * gst_sub_overlay_get_visible:
 * @overlay: a #GstSubOverlay
 *
 * Queries overlay rendering enabled state
 *
 * Returns: TRUE if rendering enabled
 */
gboolean
gst_sub_overlay_get_visible (GstSubOverlay * overlay)
{
  return overlay->priv->visible;
}

/**
 * gst_sub_overlay_set_wait:
 * @overlay: a #GstSubOverlay
 * @enable: new state
 * @sub: a pointer to a #GstBuffer
 *
 * Sets whether video stream should wait for sub stream to advance up
 * to video time before proceeding.
 */
void
gst_sub_overlay_set_wait (GstSubOverlay * overlay, gboolean enable)
{
  overlay->priv->wait_sub = enable;
}

/**
 * gst_sub_overlay_get_wait:
 * @overlay: a #GstSubOverlay
 *
 * Queries waiting behavior of video stream.
 *
 * Returns: TRUE if waiting is enabled
 */
gboolean
gst_sub_overlay_get_wait (GstSubOverlay * overlay)
{
  return overlay->priv->wait_sub;
}

/**
 * gst_sub_overlay_set_keep_video:
 * @overlay: a #GstSubOverlay
 * @enable: new state
 *
 * Configures whether a reference to the most recent video buffer should be
 * retained.
 */
void
gst_sub_overlay_set_keep_video (GstSubOverlay * overlay, gboolean enable)
{
  overlay->priv->keep_video = enable;
}

/**
 * gst_sub_overlay_set_clip_ts:
 * @overlay: a #GstSubOverlay
 * @enable: new state
 *
 * Configures whether timestamps on incoming video are clipped to segment
 * (on outgoing buffer).
 */
void
gst_sub_overlay_set_preserve_ts (GstSubOverlay * overlay, gboolean enable)
{
  overlay->priv->preserve_ts = enable;
}

/**
 * gst_sub_overlay_set_sparse_video:
 * @overlay: a #GstSubOverlay
 * @enable: new state
 *
 * Configures whether baseclass should handle sparse video stream.
 * That is, handle video stream time updates from GAP and NEW_SEGMENT events
 * by re-sending the latest video (with updated meta).  In particular,
 * this requires that retaining a reference to the most recent video buffer
 * is enabled (see gst_sub_overlay_set_keep_video()).
 */
void
gst_sub_overlay_set_sparse_video (GstSubOverlay * overlay, gboolean enable)
{
  overlay->priv->sparse_video = enable;
}

/**
 * gst_sub_overlay_set_render_no_buffer:
 * @overlay: a #GstSubOverlay
 * @enable:; new state
 *
 * Configures whether the @render method should also be called with NULL
 * buffer (in case of no currently active provided sub buffer).
 */
void
gst_sub_overlay_set_render_no_buffer (GstSubOverlay * overlay, gboolean enable)
{
  overlay->priv->render_no_buffer = enable;
}
