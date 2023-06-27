/* GStreamer
 * Copyright (C) 2023 Seungha Yang <seungha@centricular.com>
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstbasetimeoverlay.h"

GST_DEBUG_CATEGORY_STATIC (base_time_overlay_debug);
#define GST_CAT_DEFAULT base_time_overlay_debug

typedef enum
{
  GST_BASE_TIME_OVERLAY_TIME_LINE_BUFFER_TIME,
  GST_BASE_TIME_OVERLAY_TIME_LINE_STREAM_TIME,
  GST_BASE_TIME_OVERLAY_TIME_LINE_RUNNING_TIME,
  GST_BASE_TIME_OVERLAY_TIME_LINE_TIME_CODE,
  GST_BASE_TIME_OVERLAY_TIME_LINE_ELAPSED_RUNNING_TIME,
  GST_BASE_TIME_OVERLAY_TIME_LINE_REFERENCE_TIMESTAMP,
  GST_BASE_TIME_OVERLAY_TIME_LINE_BUFFER_COUNT,
  GST_BASE_TIME_OVERLAY_TIME_LINE_BUFFER_OFFSET,
} GstBaseTimeOverlayTimeLine;

#define GST_TYPE_BASE_TIME_OVERLAY_TIME_LINE (gst_base_time_overlay_time_line_get_type ())
static GType
gst_base_time_overlay_time_line_get_type (void)
{
  static GType type = 0;
  static const GEnumValue modes[] = {
    {GST_BASE_TIME_OVERLAY_TIME_LINE_BUFFER_TIME,
        "buffer-time", "buffer-time"},
    {GST_BASE_TIME_OVERLAY_TIME_LINE_STREAM_TIME,
        "stream-time", "stream-time"},
    {GST_BASE_TIME_OVERLAY_TIME_LINE_RUNNING_TIME,
        "running-time", "running-time"},
    {GST_BASE_TIME_OVERLAY_TIME_LINE_TIME_CODE, "time-code", "time-code"},
    {GST_BASE_TIME_OVERLAY_TIME_LINE_ELAPSED_RUNNING_TIME,
        "elapsed-running-time", "elapsed-running-time"},
    {GST_BASE_TIME_OVERLAY_TIME_LINE_REFERENCE_TIMESTAMP,
        "reference-timestamp", "reference-timestamp"},
    {GST_BASE_TIME_OVERLAY_TIME_LINE_BUFFER_COUNT,
        "buffer-count", "buffer-count"},
    {GST_BASE_TIME_OVERLAY_TIME_LINE_BUFFER_OFFSET,
        "buffer-offset", "buffer-offset"},
    {0, NULL, NULL},
  };

  if (g_once_init_enter (&type)) {
    GType tmp = g_enum_register_static ("GstBaseTimeOverlayTimeLine", modes);
    g_once_init_leave (&type, tmp);
  }

  return type;
}

enum
{
  PROP_0,
  PROP_TIME_LINE,
  PROP_SHOW_TIMES_AS_DATES,
  PROP_DATETIME_EPOCH,
  PROP_DATETIME_FORMAT,
  PROP_REFERENCE_TIMESTAMP_CAPS,
};

#define DEFAULT_TIME_LINE GST_BASE_TIME_OVERLAY_TIME_LINE_BUFFER_TIME
#define DEFAULT_SHOW_TIMES_AS_DATES FALSE
#define DEFAULT_DATETIME_FORMAT "%F %T" /* YYYY-MM-DD hh:mm:ss */

static GstStaticCaps ntp_reference_timestamp_caps =
GST_STATIC_CAPS ("timestamp/x-ntp");

struct _GstBaseTimeOverlayPrivate
{
  GMutex lock;
  GstBaseTimeOverlayTimeLine time_line;
  gboolean show_times_as_dates;
  guint64 buffer_count;
  gchar *datetime_format;
  GDateTime *datetime_epoch;
  GstCaps *reference_timestamp_caps;
  GstClockTime first_running_time;
};

static void gst_base_time_overlay_finalize (GObject * object);
static void gst_base_time_overlay_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_base_time_overlay_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static gboolean gst_base_time_overlay_start (GstBaseTransform * trans);
static gboolean gst_base_time_overlay_sink_event (GstBaseTransform * trans,
    GstEvent * event);
static GstFlowReturn
gst_base_time_overlay_process_input (GstBaseTextLayoutOverlay * overlay,
    GstBuffer * buffer);
static GstFlowReturn
gst_base_time_overlay_generate_layout (GstBaseTextLayoutOverlay * overlay,
    const gchar * text, GstBuffer * buffer, GstTextLayout ** layout);

#define gst_base_time_overlay_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (GstBaseTimeOverlay,
    gst_base_time_overlay, GST_TYPE_BASE_TEXT_LAYOUT_OVERLAY);

static void
gst_base_time_overlay_class_init (GstBaseTimeOverlayClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstBaseTransformClass *trans_class = GST_BASE_TRANSFORM_CLASS (klass);
  GstBaseTextLayoutOverlayClass *overlay_class =
      GST_BASE_TEXT_LAYOUT_OVERLAY_CLASS (klass);

  object_class->finalize = gst_base_time_overlay_finalize;
  object_class->set_property = gst_base_time_overlay_set_property;
  object_class->get_property = gst_base_time_overlay_get_property;

  g_object_class_install_property (object_class, PROP_TIME_LINE,
      g_param_spec_enum ("time-mode", "Time Mode", "What time to show",
          GST_TYPE_BASE_TIME_OVERLAY_TIME_LINE, DEFAULT_TIME_LINE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_DATETIME_EPOCH,
      g_param_spec_boxed ("datetime-epoch", "Datetime Epoch",
          "When showing times as dates, the initial date from which time "
          "is counted, if not specified prime epoch is used (1900-01-01)",
          G_TYPE_DATE_TIME, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_DATETIME_FORMAT,
      g_param_spec_string ("datetime-format", "Datetime Format",
          "When showing times as dates, the format to render date and time in",
          DEFAULT_DATETIME_FORMAT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_SHOW_TIMES_AS_DATES,
      g_param_spec_boolean ("show-times-as-dates", "Show times as dates",
          "Whether to display times, counted from datetime-epoch, as dates",
          DEFAULT_SHOW_TIMES_AS_DATES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_REFERENCE_TIMESTAMP_CAPS,
      g_param_spec_boxed ("reference-timestamp-caps",
          "Reference Timestamp Caps",
          "Caps to use for the reference timestamp time mode",
          GST_TYPE_CAPS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  trans_class->start = GST_DEBUG_FUNCPTR (gst_base_time_overlay_start);
  trans_class->sink_event =
      GST_DEBUG_FUNCPTR (gst_base_time_overlay_sink_event);

  overlay_class->process_input =
      GST_DEBUG_FUNCPTR (gst_base_time_overlay_process_input);
  overlay_class->generate_layout =
      GST_DEBUG_FUNCPTR (gst_base_time_overlay_generate_layout);

  GST_DEBUG_CATEGORY_INIT (base_time_overlay_debug, "baseclockoverlay", 0,
      "baseclockoverlay");
}

static void
gst_base_time_overlay_init (GstBaseTimeOverlay * self)
{
  GstBaseTimeOverlayPrivate *priv;

  g_object_set (self, "text-alignment", GST_TEXT_ALIGNMENT_LEFT,
      "paragraph-alignment", GST_PARAGRAPH_ALIGNMENT_TOP,
      "font-size", (gdouble) 18, NULL);

  self->priv = priv = gst_base_time_overlay_get_instance_private (self);
  g_mutex_init (&priv->lock);

  priv->time_line = DEFAULT_TIME_LINE;
  priv->show_times_as_dates = DEFAULT_SHOW_TIMES_AS_DATES;
  priv->datetime_epoch = g_date_time_new_utc (1900, 1, 1, 0, 0, 0);
  priv->datetime_format = g_strdup (DEFAULT_DATETIME_FORMAT);
  priv->reference_timestamp_caps =
      gst_static_caps_get (&ntp_reference_timestamp_caps);
}

static void
gst_base_time_overlay_finalize (GObject * object)
{
  GstBaseTimeOverlay *self = GST_BASE_TIME_OVERLAY (object);
  GstBaseTimeOverlayPrivate *priv = self->priv;

  gst_clear_caps (&priv->reference_timestamp_caps);
  g_clear_pointer (&priv->datetime_epoch, g_date_time_unref);
  g_free (priv->datetime_format);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_base_time_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstBaseTimeOverlay *self = GST_BASE_TIME_OVERLAY (object);
  GstBaseTimeOverlayPrivate *priv = self->priv;

  g_mutex_lock (&priv->lock);
  switch (prop_id) {
    case PROP_TIME_LINE:
      priv->time_line = g_value_get_enum (value);
      break;
    case PROP_SHOW_TIMES_AS_DATES:
      priv->show_times_as_dates = g_value_get_boolean (value);
      break;
    case PROP_DATETIME_EPOCH:
      g_date_time_unref (priv->datetime_epoch);
      priv->datetime_epoch = g_value_dup_boxed (value);
      if (!priv->datetime_epoch)
        priv->datetime_epoch = g_date_time_new_utc (1900, 1, 1, 0, 0, 0);
      break;
    case PROP_DATETIME_FORMAT:
      g_free (priv->datetime_format);
      priv->datetime_format = g_value_dup_string (value);
      if (!priv->datetime_format)
        priv->datetime_format = g_strdup (DEFAULT_DATETIME_FORMAT);
      break;
    case PROP_REFERENCE_TIMESTAMP_CAPS:
      gst_clear_caps (&priv->reference_timestamp_caps);
      priv->reference_timestamp_caps = g_value_dup_boxed (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  g_mutex_unlock (&priv->lock);
}

static void
gst_base_time_overlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstBaseTimeOverlay *self = GST_BASE_TIME_OVERLAY (object);
  GstBaseTimeOverlayPrivate *priv = self->priv;

  g_mutex_lock (&priv->lock);
  switch (prop_id) {
    case PROP_TIME_LINE:
      g_value_set_enum (value, priv->time_line);
      break;
    case PROP_SHOW_TIMES_AS_DATES:
      g_value_set_boolean (value, priv->show_times_as_dates);
      break;
    case PROP_DATETIME_EPOCH:
      g_value_set_boxed (value, priv->datetime_epoch);
      break;
    case PROP_DATETIME_FORMAT:
      g_value_set_string (value, priv->datetime_format);
      break;
    case PROP_REFERENCE_TIMESTAMP_CAPS:
      g_value_set_boxed (value, priv->reference_timestamp_caps);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  g_mutex_unlock (&priv->lock);
}

static gboolean
gst_base_time_overlay_start (GstBaseTransform * trans)
{
  GstBaseTimeOverlay *self = GST_BASE_TIME_OVERLAY (trans);
  GstBaseTimeOverlayPrivate *priv = self->priv;

  priv->first_running_time = GST_CLOCK_TIME_NONE;
  priv->buffer_count = 0;

  return GST_BASE_TRANSFORM_CLASS (parent_class)->start (trans);
}

static gboolean
gst_base_time_overlay_sink_event (GstBaseTransform * trans, GstEvent * event)
{
  GstBaseTimeOverlay *self = GST_BASE_TIME_OVERLAY (trans);
  GstBaseTimeOverlayPrivate *priv = self->priv;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEGMENT:
    case GST_EVENT_FLUSH_STOP:
      priv->first_running_time = GST_CLOCK_TIME_NONE;
      break;
    default:
      break;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->sink_event (trans, event);
}

static GstFlowReturn
gst_base_time_overlay_process_input (GstBaseTextLayoutOverlay * overlay,
    GstBuffer * buffer)
{
  GstBaseTimeOverlay *self = GST_BASE_TIME_OVERLAY (overlay);
  GstBaseTimeOverlayPrivate *priv = self->priv;

  priv->buffer_count++;

  return GST_FLOW_OK;
}

static gchar *
gst_base_time_overlay_render_time (GstBaseTimeOverlay * self, GstClockTime time)
{
  guint hours, mins, secs, msecs;

  if (!GST_CLOCK_TIME_IS_VALID (time))
    return g_strdup ("");

  hours = (guint) (time / (GST_SECOND * 60 * 60));
  mins = (guint) ((time / (GST_SECOND * 60)) % 60);
  secs = (guint) ((time / GST_SECOND) % 60);
  msecs = (guint) ((time % GST_SECOND) / (1000 * 1000));

  return g_strdup_printf ("%u:%02u:%02u.%03u", hours, mins, secs, msecs);
}

static GstFlowReturn
gst_base_time_overlay_generate_layout (GstBaseTextLayoutOverlay * overlay,
    const gchar * text, GstBuffer * buffer, GstTextLayout ** layout)
{
  GstBaseTimeOverlay *self = GST_BASE_TIME_OVERLAY (overlay);
  GstBaseTimeOverlayPrivate *priv = self->priv;
  gchar *time_str = NULL;
  gboolean show_buffer_count = FALSE;
  guint64 buffer_count = 0;
  GstTextLayout *ret = NULL;

  g_mutex_lock (&priv->lock);
  if (priv->time_line == GST_BASE_TIME_OVERLAY_TIME_LINE_TIME_CODE) {
    GstVideoTimeCodeMeta *tc_meta =
        gst_buffer_get_video_time_code_meta (buffer);
    if (!tc_meta) {
      GST_DEBUG_OBJECT (self, "buffer without valid timecode");
      time_str = g_strdup ("00:00:00:00");
    } else {
      time_str = gst_video_time_code_to_string (&tc_meta->tc);
      GST_DEBUG_OBJECT (self, "buffer with timecode %s", time_str);
    }
  } else {
    GstBaseTransform *trans = GST_BASE_TRANSFORM (overlay);
    GstClockTime ts, ts_buffer;
    GstSegment *seg = &trans->segment;

    ts = ts_buffer = GST_BUFFER_PTS (buffer);
    if (GST_CLOCK_TIME_IS_VALID (ts)) {
      switch (priv->time_line) {
        case GST_BASE_TIME_OVERLAY_TIME_LINE_STREAM_TIME:
          ts = gst_segment_to_stream_time (seg, GST_FORMAT_TIME, ts_buffer);
          break;
        case GST_BASE_TIME_OVERLAY_TIME_LINE_RUNNING_TIME:
          ts = gst_segment_to_running_time (seg, GST_FORMAT_TIME, ts_buffer);
          break;
        case GST_BASE_TIME_OVERLAY_TIME_LINE_ELAPSED_RUNNING_TIME:
          ts = gst_segment_to_running_time (seg, GST_FORMAT_TIME, ts_buffer);
          if (!GST_CLOCK_TIME_IS_VALID (priv->first_running_time))
            priv->first_running_time = ts;
          ts -= priv->first_running_time;
          break;
        case GST_BASE_TIME_OVERLAY_TIME_LINE_REFERENCE_TIMESTAMP:
        {
          GstReferenceTimestampMeta *meta;
          if (priv->reference_timestamp_caps) {
            meta = gst_buffer_get_reference_timestamp_meta (buffer,
                priv->reference_timestamp_caps);
            if (meta)
              ts = meta->timestamp;
            else
              ts = 0;
          } else {
            ts = 0;
          }
          break;
        }
        case GST_BASE_TIME_OVERLAY_TIME_LINE_BUFFER_COUNT:
          show_buffer_count = TRUE;
          buffer_count = priv->buffer_count;
          break;
        case GST_BASE_TIME_OVERLAY_TIME_LINE_BUFFER_OFFSET:
          show_buffer_count = TRUE;
          ts = gst_segment_to_running_time (seg, GST_FORMAT_TIME, ts_buffer);
          buffer_count = gst_util_uint64_scale (ts, overlay->in_info.fps_n,
              overlay->in_info.fps_d * GST_SECOND);
          break;
        case GST_BASE_TIME_OVERLAY_TIME_LINE_BUFFER_TIME:
        default:
          ts = ts_buffer;
          break;
      }

      if (show_buffer_count) {
        time_str = g_strdup_printf ("%" G_GUINT64_FORMAT, buffer_count);
      } else if (priv->show_times_as_dates) {
        GDateTime *datetime;

        datetime =
            g_date_time_add_seconds (priv->datetime_epoch,
            ((gdouble) ts) / GST_SECOND);

        time_str = g_date_time_format (datetime, priv->datetime_format);
        g_date_time_unref (datetime);
      } else {
        time_str = gst_base_time_overlay_render_time (self, ts);
      }
    }
  }

  if (!time_str) {
    if (text && text[0] != '\0')
      ret = gst_text_layout_new (text);
  } else if (text && text[0] != '\0') {
    gchar *tmp = g_strdup_printf ("%s %s", text, time_str);
    ret = gst_text_layout_new (tmp);
    g_free (tmp);
  } else {
    ret = gst_text_layout_new (time_str);
  }

  g_free (time_str);
  g_mutex_unlock (&priv->lock);

  *layout = ret;

  return GST_FLOW_OK;
}
