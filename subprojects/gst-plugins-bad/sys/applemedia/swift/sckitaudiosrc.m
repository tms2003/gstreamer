/*
 * Copyright (C) 2024 Piotr Brzeziński <piotr@centricular.com>
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

#include "gst/gstinfo.h"
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "sckitaudiosrc.h"

GST_DEBUG_CATEGORY (gst_sckit_audio_src_debug);
#define GST_CAT_DEFAULT gst_sckit_audio_src_debug

enum {
  PROP_0,
  PROP_EXCLUDE_CURRENT_PROCESS,
};

/* Supported sample rates found here: 
 * https://developer.apple.com/documentation/screencapturekit/scstreamconfiguration/3931903-samplerate */
static GstStaticPadTemplate gst_sckitsrc_audio_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw, layout=non-interleaved, format = (string) { F32LE }, "
        "rate = (int) { 8000, 16000, 24000, 48000 }, channels = (int) { 1, 2 }"));

GST_ELEMENT_REGISTER_DEFINE (sckitaudiosrc, "sckitaudiosrc", GST_RANK_PRIMARY, GST_TYPE_SCKIT_AUDIO_SRC); // TODO: rank?

#define gst_sckit_audio_src_parent_class parent_class
G_DEFINE_TYPE (GstSCKitAudioSrc, gst_sckit_audio_src, GST_TYPE_BASE_SRC);

static void
gst_sckit_audio_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstSCKitAudioSrc *self = GST_SCKIT_AUDIO_SRC (object);
  SCKitAudioSrc *impl = self->impl;

  switch (prop_id) {
    case PROP_EXCLUDE_CURRENT_PROCESS:
      g_value_set_boolean (value, impl.excludeCurrentProcess);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_sckit_audio_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSCKitAudioSrc *self = GST_SCKIT_AUDIO_SRC (object);
  SCKitAudioSrc *impl = self->impl;

  switch (prop_id) {
    case PROP_EXCLUDE_CURRENT_PROCESS:
      impl.excludeCurrentProcess = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstFlowReturn
gst_sckit_audio_src_create (GstBaseSrc * src, guint64 offset, guint size, GstBuffer ** buf)
{
  GstSCKitAudioSrc *self = GST_SCKIT_AUDIO_SRC (src);
  return [self->impl gstCreateWithOffset:offset size:size bufPtr:buf];
}

static gboolean
gst_sckit_audio_src_start (GstBaseSrc * src)
{
  GstSCKitAudioSrc *self = GST_SCKIT_AUDIO_SRC (src);
  return [self->impl gstStart];
}

static gboolean
gst_sckit_audio_src_stop (GstBaseSrc * src)
{
  GstSCKitAudioSrc *self = GST_SCKIT_AUDIO_SRC (src);
  return [self->impl gstStop];
}

static gboolean
gst_sckit_audio_src_set_caps (GstBaseSrc * src, GstCaps * caps)
{
  GstSCKitAudioSrc *self = GST_SCKIT_AUDIO_SRC (src);
  return [self->impl gstSetCapsWithCaps:caps];
}

static void
gst_sckit_audio_src_init (GstSCKitAudioSrc * self)
{
  GstBaseSrc *base_src = GST_BASE_SRC (self);
  self->impl = [[SCKitAudioSrc alloc] initWithSrc:base_src debugCat:GST_CAT_DEFAULT];
}

static void
gst_sckit_audio_src_class_init (GstSCKitAudioSrcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseSrcClass *basesrc_class = GST_BASE_SRC_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class, &gst_sckitsrc_audio_template);

  gst_element_class_set_static_metadata (element_class,
      "ScreenCaptureKit Audio Source", "Source/Audio",
      "Captures system audio on macOS", "Piotr Brzeziński <piotr@centricular.com>");

  GST_DEBUG_CATEGORY_INIT (gst_sckit_audio_src_debug, "sckitaudiosrc", 0, "ScreenCaptureKit audio source");

  gobject_class->get_property = gst_sckit_audio_src_get_property;
  gobject_class->set_property = gst_sckit_audio_src_set_property;

  basesrc_class->create = gst_sckit_audio_src_create;
  basesrc_class->start = gst_sckit_audio_src_start;
  basesrc_class->stop = gst_sckit_audio_src_stop;
  basesrc_class->set_caps = gst_sckit_audio_src_set_caps;

  g_object_class_install_property (gobject_class, PROP_EXCLUDE_CURRENT_PROCESS,
    g_param_spec_boolean ("exclude-current-process",
        "Exclude current process",
        "Whether to exclude audio output by the current process from the capture",
        TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}
