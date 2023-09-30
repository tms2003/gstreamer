/* GStreamer
 *
 * Copyright (c) 2008,2009 Sebastian Dröge <sebastian.droege@collabora.co.uk>
 * Copyright (c) 2008-2017 Collabora Ltd
 *  @author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *  @author: Vincent Penquerc'h <vincent.penquerch@collabora.com>
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

#ifndef __GST_FLV_MUX_H__
#define __GST_FLV_MUX_H__

#include <gst/gst.h>
#include <gst/base/gstaggregator.h>
#include <gst/codecparsers/gstav1parser.h>

G_BEGIN_DECLS

#define GST_TYPE_FLV_MUX_PAD (gst_flv_mux_pad_get_type())
#define GST_FLV_MUX_PAD(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_FLV_MUX_PAD, GstFlvMuxPad))
#define GST_FLV_MUX_PAD_CAST(obj) ((GstFlvMuxPad *)(obj))
#define GST_FLV_MUX_PAD_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_FLV_MUX_PAD, GstFlvMuxPad))
#define GST_IS_FLV_MUX_PAD(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_FLV_MUX_PAD))
#define GST_IS_FLV_MUX_PAD_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_FLV_MUX_PAD))

typedef struct _GstFlvMuxPad GstFlvMuxPad;
typedef struct _GstFlvMuxPadClass GstFlvMuxPadClass;
typedef struct _GstFlvMux GstFlvMux;
typedef struct _GstFlvMuxClass GstFlvMuxClass;

#define GST_TYPE_FLV_MUX \
  (gst_flv_mux_get_type ())
#define GST_FLV_MUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_FLV_MUX, GstFlvMux))
#define GST_FLV_MUX_CAST(obj) ((GstFlvMux *)obj)
#define GST_FLV_MUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_FLV_MUX, GstFlvMuxClass))
#define GST_IS_FLV_MUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_FLV_MUX))
#define GST_IS_FLV_MUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_FLV_MUX))

typedef struct _AV1CodecConfigurationRecord {
  guint8 marker_version; /* (1) marker, (7) version */
  guint8 seq_profile_level; /* (3) seq_profile, (5) seq_level_idx_0  */
  guint8 seq_tier_bitdepth_twelve_monochrome_chroma; /* (1) seq_tier_0, (1) high_bitdepth, (1) twelve_bit, (1) monochrome, (1) chroma_subsampling_x, (1) chroma_subsampling_y, (2) chroma_sample_position */
  guint8 initial_presentation; /* (3) reserved (1) initial_presentation_delay_present (4) initial_presentation_delay_minus_one */
} AV1CodecConfigurationRecord;


struct _GstFlvMuxPad
{
  GstAggregatorPad aggregator_pad;

  guint codec;
  guint rate;
  guint width;
  guint channels;
  GstBuffer *codec_data;

  guint bitrate;

  GstClockTime last_timestamp;
  GstClockTime pts;
  GstClockTime dts;

  gboolean info_changed;
  gboolean drop_deltas;

  gboolean is_ex_header;
  guint32 fourcc;

  AV1CodecConfigurationRecord av1_codec_config;
  gboolean seq_header_sent;
};

struct _GstFlvMuxPadClass {
  GstAggregatorPadClass parent;
};

typedef enum
{
  GST_FLV_MUX_STATE_HEADER,
  GST_FLV_MUX_STATE_DATA
} GstFlvMuxState;

struct _GstFlvMux {
  GstAggregator   aggregator;

  GstPad         *srcpad;

  /* <private> */
  GstFlvMuxState state;
  GstFlvMuxPad *audio_pad;
  GstFlvMuxPad *video_pad;
  gboolean streamable;
  gchar *metadatacreator;
  gchar *encoder;
  gboolean skip_backwards_streams;
  gboolean enforce_increasing_timestamps;

  GstTagList *tags;
  gboolean new_metadata;
  GList *index;
  guint64 byte_count;
  GstClockTime duration;
  GstClockTime first_timestamp;
  guint64 last_dts;

  gboolean sent_header;

  GstAV1Parser *parser;
};

struct _GstFlvMuxClass {
  GstAggregatorClass parent;
};

GType    gst_flv_mux_pad_get_type(void);
GType    gst_flv_mux_get_type    (void);

G_END_DECLS

#endif /* __GST_FLV_MUX_H__ */
