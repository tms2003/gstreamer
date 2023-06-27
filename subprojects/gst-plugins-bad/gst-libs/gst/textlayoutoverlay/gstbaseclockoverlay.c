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

#include "gstbaseclockoverlay.h"
#include <time.h>

GST_DEBUG_CATEGORY_STATIC (base_clock_overlay_debug);
#define GST_CAT_DEFAULT base_clock_overlay_debug

enum
{
  PROP_0,
  PROP_TIME_FORMAT,
};

#define DEFAULT_TIME_FORMAT "%H:%M:%S"

struct _GstBaseClockOverlayPrivate
{
  GMutex lock;
  gchar *format;
  gunichar2 *wformat;
  GstTextLayout *layout;
};

static void gst_base_clock_overlay_finalize (GObject * object);
static void gst_base_clock_overlay_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_base_clock_overlay_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static gboolean gst_base_clock_overlay_start (GstBaseTransform * trans);
static gboolean gst_base_clock_overlay_stop (GstBaseTransform * trans);
static GstFlowReturn
gst_base_clock_overlay_generate_layout (GstBaseTextLayoutOverlay * overlay,
    const gchar * text, GstBuffer * buffer, GstTextLayout ** layout);

#define gst_base_clock_overlay_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (GstBaseClockOverlay,
    gst_base_clock_overlay, GST_TYPE_BASE_TEXT_LAYOUT_OVERLAY);

static void
gst_base_clock_overlay_class_init (GstBaseClockOverlayClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstBaseTransformClass *trans_class = GST_BASE_TRANSFORM_CLASS (klass);
  GstBaseTextLayoutOverlayClass *overlay_class =
      GST_BASE_TEXT_LAYOUT_OVERLAY_CLASS (klass);

  object_class->finalize = gst_base_clock_overlay_finalize;
  object_class->set_property = gst_base_clock_overlay_set_property;
  object_class->get_property = gst_base_clock_overlay_get_property;

  g_object_class_install_property (object_class, PROP_TIME_FORMAT,
      g_param_spec_string ("time-format", "Date/Time Format",
          "Format to use for time and date value, as in strftime.",
          DEFAULT_TIME_FORMAT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  trans_class->start = GST_DEBUG_FUNCPTR (gst_base_clock_overlay_start);
  trans_class->stop = GST_DEBUG_FUNCPTR (gst_base_clock_overlay_stop);

  overlay_class->generate_layout =
      GST_DEBUG_FUNCPTR (gst_base_clock_overlay_generate_layout);

  GST_DEBUG_CATEGORY_INIT (base_clock_overlay_debug, "baseclockoverlay", 0,
      "baseclockoverlay");
}

static void
gst_base_clock_overlay_init (GstBaseClockOverlay * self)
{
  GstBaseClockOverlayPrivate *priv;

  g_object_set (self, "text-alignment", GST_TEXT_ALIGNMENT_LEFT,
      "paragraph-alignment", GST_PARAGRAPH_ALIGNMENT_TOP,
      "font-size", (gdouble) 18, NULL);

  self->priv = priv = gst_base_clock_overlay_get_instance_private (self);
  g_mutex_init (&priv->lock);
  priv->format = g_strdup (DEFAULT_TIME_FORMAT);
#ifdef G_OS_WIN32
  priv->wformat = g_utf8_to_utf16 (DEFAULT_TIME_FORMAT, -1, NULL, NULL, NULL);
#endif
}

static void
gst_base_clock_overlay_finalize (GObject * object)
{
  GstBaseClockOverlay *self = GST_BASE_CLOCK_OVERLAY (object);
  GstBaseClockOverlayPrivate *priv = self->priv;

  g_free (priv->format);
  g_free (priv->wformat);
  g_mutex_clear (&priv->lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_base_clock_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstBaseClockOverlay *self = GST_BASE_CLOCK_OVERLAY (object);
  GstBaseClockOverlayPrivate *priv = self->priv;

  g_mutex_lock (&priv->lock);
  switch (prop_id) {
    case PROP_TIME_FORMAT:
      g_free (priv->format);
      priv->format = g_value_dup_string (value);
      if (!priv->format)
        priv->format = g_strdup (DEFAULT_TIME_FORMAT);
#ifdef G_OS_WIN32
      g_free (priv->wformat);
      priv->wformat = g_utf8_to_utf16 (priv->format, -1, NULL, NULL, NULL);
#endif
      gst_clear_text_layout (&priv->layout);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  g_mutex_unlock (&priv->lock);
}

static void
gst_base_clock_overlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstBaseClockOverlay *self = GST_BASE_CLOCK_OVERLAY (object);
  GstBaseClockOverlayPrivate *priv = self->priv;

  g_mutex_lock (&priv->lock);
  switch (prop_id) {
    case PROP_TIME_FORMAT:
      g_value_set_string (value, priv->format);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  g_mutex_unlock (&priv->lock);
}

static void
gst_base_clock_overlay_reset (GstBaseClockOverlay * self)
{
  GstBaseClockOverlayPrivate *priv = self->priv;

  gst_clear_text_layout (&priv->layout);
}

static gboolean
gst_base_clock_overlay_start (GstBaseTransform * trans)
{
  GstBaseClockOverlay *self = GST_BASE_CLOCK_OVERLAY (trans);

  gst_base_clock_overlay_reset (self);

  return GST_BASE_TRANSFORM_CLASS (parent_class)->start (trans);
}

static gboolean
gst_base_clock_overlay_stop (GstBaseTransform * trans)
{
  GstBaseClockOverlay *self = GST_BASE_CLOCK_OVERLAY (trans);

  gst_base_clock_overlay_reset (self);

  return GST_BASE_TRANSFORM_CLASS (parent_class)->stop (trans);
}

static gchar *
gst_base_clock_overlay_render_time (GstBaseClockOverlay * self)
{
  GstBaseClockOverlayPrivate *priv = self->priv;
#ifdef HAVE_LOCALTIME_R
  struct tm dummy;
#endif
  struct tm *t;
  time_t now;
#ifdef G_OS_WIN32
  gunichar2 buf[256];
#else
  gchar buf[256];
#endif

  now = time (NULL);

#ifdef HAVE_LOCALTIME_R
  /* Need to call tzset explicitly when calling localtime_r for changes
   * to the timezone between calls to be visible.  */
  tzset ();
  t = localtime_r (&now, &dummy);
#else
  /* on win32 this apparently returns a per-thread struct which would be fine */
  t = localtime (&now);
#endif

  if (t == NULL)
    return g_strdup ("--:--:--");

#ifdef G_OS_WIN32
  if (wcsftime (buf, sizeof (buf), priv->wformat, t) == 0)
    return g_strdup ("");

  return g_utf16_to_utf8 (buf, -1, NULL, NULL, NULL);
#else
  if (strftime (buf, sizeof (buf), priv->format, t) == 0)
    return g_strdup ("");

  return g_strdup (buf);
#endif
}

static GstFlowReturn
gst_base_clock_overlay_generate_layout (GstBaseTextLayoutOverlay * overlay,
    const gchar * text, GstBuffer * buffer, GstTextLayout ** layout)
{
  GstBaseClockOverlay *self = GST_BASE_CLOCK_OVERLAY (overlay);
  GstBaseClockOverlayPrivate *priv = self->priv;
  gchar *clock_text;

  g_mutex_lock (&priv->lock);
  clock_text = gst_base_clock_overlay_render_time (self);

  if (text && text[0] != '\0') {
    gchar *tmp = g_strdup_printf ("%s %s", text, clock_text);
    g_free (clock_text);
    clock_text = tmp;
  }

  if (priv->layout &&
      g_strcmp0 (gst_text_layout_get_text (priv->layout), clock_text) != 0) {
    gst_clear_text_layout (&priv->layout);
  }

  if (!priv->layout)
    priv->layout = gst_text_layout_new (clock_text);
  g_free (clock_text);
  g_mutex_unlock (&priv->lock);

  *layout = gst_text_layout_ref (priv->layout);

  return GST_FLOW_OK;
}
