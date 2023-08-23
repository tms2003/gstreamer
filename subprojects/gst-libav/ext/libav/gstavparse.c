/* GStreamer
 * Copyright (C) <2023> Devin Anderson <danderson@microsoft.com>
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

#include <string.h>

#include <glib.h>

#include <gst/gstbuffer.h>
#include <gst/gstcaps.h>
#include <gst/gstcompat.h>
#include <gst/gstelement.h>
#include <gst/gstpadtemplate.h>
#include <gst/base/gstbaseparse.h>

#include <libavcodec/avcodec.h>

#include "gstav.h"
#include "gstavcodecmap.h"
#include "gstavutils.h"

///////////////////////////////////////////////////////////////////////////////
// GstFFMpegParserCapsSnapshot
///////////////////////////////////////////////////////////////////////////////

#define UPDATE_SCALAR(a, b, updated) \
  do {                               \
    if ((a) != (b)) {                \
      (a) = (b);                     \
      (updated) = TRUE;              \
    }                                \
  } while (FALSE)

#define UPDATE_RATIONAL(a, b, updated)          \
  do {                                          \
    UPDATE_SCALAR((a).den, (b).den, (updated)); \
    UPDATE_SCALAR((a).num, (b).num, (updated)); \
  } while (FALSE)

// TODO: When `gst-libav` starts to depend on `ffmpeg` >= 5.1, we'll want to
// take the new channel layout mechanism into account.  Right now, channel
// counts and layout are handled separately.

typedef struct GstFFMpegParserCapsSnapshot
{

  // Common data
  unsigned int codec_tag;
  enum AVMediaType media_type;

  // Media specific data
  union
  {

    struct
    {
      int64_t bit_rate;
      int bits_per_coded_sample;
      int block_align;
      int channel_count;
      uint64_t channel_layout;
      enum AVSampleFormat sample_format;
      int sample_rate;
    } audio;

    struct
    {
      int64_t bit_rate;

      // Timing
      AVRational frame_rate;
      int ticks_per_frame;
      AVRational time_base;

      // Picture
      int bits_per_coded_sample;
      enum AVFieldOrder field_order;
      int height;
      enum AVPixelFormat pixel_format;
      AVRational sample_aspect_ratio;
      int width;
    } video;

  } data;

} GstFFMpegParserCapsSnapshot;

static void
gst_ffmpeg_parser_caps_snapshot_init_audio (GstFFMpegParserCapsSnapshot *
    snapshot, AVCodecContext * context)
{
  snapshot->data.audio.bit_rate = context->bit_rate;
  snapshot->data.audio.bits_per_coded_sample = context->bits_per_coded_sample;
  snapshot->data.audio.block_align = context->block_align;
  snapshot->data.audio.channel_count = context->channels;
  snapshot->data.audio.channel_layout = context->channel_layout;
  snapshot->data.audio.sample_format = context->sample_fmt;
  snapshot->data.audio.sample_rate = context->sample_rate;
}

static void
gst_ffmpeg_parser_caps_snapshot_init_video (GstFFMpegParserCapsSnapshot *
    snapshot, AVCodecContext * context)
{
  snapshot->data.video.bit_rate = context->bit_rate;

  snapshot->data.video.frame_rate = context->framerate;
  snapshot->data.video.ticks_per_frame = context->ticks_per_frame;
  snapshot->data.video.time_base = context->time_base;

  snapshot->data.video.bits_per_coded_sample = context->bits_per_coded_sample;
  snapshot->data.video.field_order = context->field_order;
  snapshot->data.video.height = context->height;
  snapshot->data.video.pixel_format = context->pix_fmt;
  snapshot->data.video.sample_aspect_ratio = context->sample_aspect_ratio;
  snapshot->data.video.width = context->width;
}

static void
gst_ffmpeg_parser_caps_snapshot_init (GstFFMpegParserCapsSnapshot * snapshot,
    AVCodecContext * context)
{
  switch (context->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
      gst_ffmpeg_parser_caps_snapshot_init_audio (snapshot, context);
      break;
    case AVMEDIA_TYPE_VIDEO:
      gst_ffmpeg_parser_caps_snapshot_init_video (snapshot, context);
      break;
    default:
      ;
  }

  snapshot->codec_tag = context->codec_tag;
  snapshot->media_type = context->codec_type;
}

static gboolean
gst_ffmpeg_parser_caps_snapshot_update (GstFFMpegParserCapsSnapshot * snapshot,
    AVCodecContext * context)
{
  gboolean result = FALSE;

  if (snapshot->media_type != context->codec_type) {

    switch (context->codec_type) {
      case AVMEDIA_TYPE_AUDIO:
        gst_ffmpeg_parser_caps_snapshot_init_audio (snapshot, context);
        break;
      case AVMEDIA_TYPE_VIDEO:
        gst_ffmpeg_parser_caps_snapshot_init_video (snapshot, context);
        break;
      default:
        ;
    }

    snapshot->media_type = context->codec_type;
    result = TRUE;

  } else {

    switch (snapshot->media_type) {
      case AVMEDIA_TYPE_AUDIO:
        UPDATE_SCALAR (snapshot->data.audio.bit_rate, context->bit_rate,
            result);
        UPDATE_SCALAR (snapshot->data.audio.bits_per_coded_sample,
            context->bits_per_coded_sample, result);
        UPDATE_SCALAR (snapshot->data.audio.block_align, context->block_align,
            result);
        UPDATE_SCALAR (snapshot->data.audio.channel_count, context->channels,
            result);
        UPDATE_SCALAR (snapshot->data.audio.channel_layout,
            context->channel_layout, result);
        UPDATE_SCALAR (snapshot->data.audio.sample_format, context->sample_fmt,
            result);
        UPDATE_SCALAR (snapshot->data.audio.sample_rate, context->sample_rate,
            result);
        break;

      case AVMEDIA_TYPE_VIDEO:
        UPDATE_SCALAR (snapshot->data.video.bit_rate, context->bit_rate,
            result);
        UPDATE_RATIONAL (snapshot->data.video.frame_rate, context->framerate,
            result);
        UPDATE_SCALAR (snapshot->data.video.ticks_per_frame,
            context->ticks_per_frame, result);
        UPDATE_RATIONAL (snapshot->data.video.time_base, context->time_base,
            result);
        UPDATE_SCALAR (snapshot->data.video.bits_per_coded_sample,
            context->bits_per_coded_sample, result);
        UPDATE_SCALAR (snapshot->data.video.field_order, context->field_order,
            result);
        UPDATE_SCALAR (snapshot->data.video.height, context->height, result);
        UPDATE_SCALAR (snapshot->data.video.pixel_format, context->pix_fmt,
            result);
        UPDATE_RATIONAL (snapshot->data.video.sample_aspect_ratio,
            context->sample_aspect_ratio, result);
        UPDATE_SCALAR (snapshot->data.video.width, context->width, result);
        break;

      default:
        ;
    }

  }

  UPDATE_SCALAR (snapshot->codec_tag, context->codec_tag, result);

  return result;
}

///////////////////////////////////////////////////////////////////////////////
// GstFFMpegParser: base definitions
///////////////////////////////////////////////////////////////////////////////

typedef gboolean (*GstFFMpegParserSinkEventHandler) (GstBaseParse *,
    GstEvent *);

typedef struct _GstFFMpegParserClass
{

  GstBaseParseClass parent;

  const AVCodec *codec;
  GstFFMpegParserSinkEventHandler default_sink_event_handler;
  gchar *mime_type;

} GstFFMpegParserClass;

typedef struct _GstFFMpegParser
{

  GstBaseParse parent;

  AVCodecContext *codec_context;
  AVCodecParserContext *parser_context;

  GstFFMpegParserCapsSnapshot caps_snapshot;
  gboolean data_parsed;
  uint8_t *frame_buffer;
  int frame_buffer_size;
  GstCaps *src_caps;

} GstFFMpegParser;

#define DEBUG_PARSER(parser)                                                 \
  GST_DEBUG_OBJECT (                                                         \
      parser,                                                                \
      "codec_context: %p, parser_context: %p, data_parsed: %s, "             \
      "frame_buffer: %p, frame_buffer_size: %d, src_caps: %" GST_PTR_FORMAT, \
      (parser)->codec_context, (parser)->parser_context,                     \
      (parser)->data_parsed ? "true" : "false", (parser)->frame_buffer,	     \
      (parser)->frame_buffer_size, (parser)->src_caps)

///////////////////////////////////////////////////////////////////////////////
// GstFFMpegParser: GObject overrides
///////////////////////////////////////////////////////////////////////////////

static void
gst_ffmpeg_parser_finalize (GObject * obj)
{
  GstFFMpegParser *parser = (GstFFMpegParser *) obj;

  if (parser->frame_buffer != NULL) {
    g_free (parser->frame_buffer);
    parser->frame_buffer = NULL;
    parser->frame_buffer_size = 0;
  }

  GstFFMpegParserClass *parser_cls =
      (GstFFMpegParserClass *) G_OBJECT_GET_CLASS (parser);
  G_OBJECT_CLASS (g_type_class_peek_parent (parser_cls))->finalize (obj);
}

///////////////////////////////////////////////////////////////////////////////
// GstFFMpegParser: GstBaseParse overrides
///////////////////////////////////////////////////////////////////////////////

static gboolean gst_ffmpeg_parser_drain (GstFFMpegParser * parser);

static gboolean gst_ffmpeg_parser_restart (GstFFMpegParser * parser);

static gboolean gst_ffmpeg_parser_update_src_caps (GstFFMpegParser * parser);

static GstFlowReturn
gst_ffmpeg_parser_handle_frame (GstBaseParse * base_parser,
    GstBaseParseFrame * frame, gint * skip_size)
{
  GstFFMpegParser *parser = (GstFFMpegParser *) base_parser;

  GstBuffer *buffer = frame->buffer;
  gsize buffer_size = gst_buffer_get_size (buffer);

  // `ffmpeg` requires additional bytes at the end of the buffer to accommodate
  // "some optimized bitstream readers [that] read 32 or 64 bit[s] at once and
  // could read over the end."
  int required_size = ((int) buffer_size) + AV_INPUT_BUFFER_PADDING_SIZE;
  if (required_size > parser->frame_buffer_size) {
    parser->frame_buffer =
        g_realloc (parser->frame_buffer, (gsize) required_size);
    parser->frame_buffer_size = required_size;
  }
  gst_buffer_extract (buffer, 0, parser->frame_buffer, buffer_size);
  memset (parser->frame_buffer + buffer_size, 0, AV_INPUT_BUFFER_PADDING_SIZE);

  AVCodecContext *codec_context = parser->codec_context;
  uint8_t *result_buffer;
  int result_size;
  AVRational *time_base = &(codec_context->time_base);
  *skip_size =
      av_parser_parse2 (parser->parser_context, codec_context, &result_buffer,
      &result_size, parser->frame_buffer, (int) buffer_size,
      gst_ffmpeg_time_gst_to_ff (GST_BUFFER_PTS (buffer), *time_base),
      gst_ffmpeg_time_gst_to_ff (GST_BUFFER_DTS (buffer), *time_base), 0);
  parser->data_parsed = TRUE;

  if (result_size == 0) {
    return GST_FLOW_OK;
  }

  if (!gst_ffmpeg_parser_update_src_caps (parser)) {
    return GST_FLOW_ERROR;
  }

  frame->out_buffer = gst_buffer_new_memdup (
      (gconstpointer) result_buffer, (gsize) result_size);
  return gst_base_parse_finish_frame (base_parser, frame, 0);
}

static gboolean
gst_ffmpeg_parser_process_sink_event (GstBaseParse * base_parser,
    GstEvent * event)
{
  GstFFMpegParser *parser = (GstFFMpegParser *) base_parser;
  if ((GST_EVENT_TYPE (event) == GST_EVENT_EOS) &&
      (!gst_ffmpeg_parser_drain (parser))) {
    gst_event_unref (event);
    return FALSE;
  }
  return ((GstFFMpegParserClass *)
      G_OBJECT_GET_CLASS (parser))->default_sink_event_handler (base_parser,
      event);
}

static gboolean
gst_ffmpeg_parser_set_sink_caps (GstBaseParse * base_parser, GstCaps * caps)
{
  GstFFMpegParser *parser = (GstFFMpegParser *) base_parser;

  // Initially, we use any CAPS data sent over the sink pad as *base* data,
  // which *may* be overridden by the codec parser as the parser receives data.
  // Not all `ffmpeg` codec parsers write CAPS data to the codec context, so
  // we'll have to be careful to pick and choose parsers that provide the
  // information we need in the parser and/or are associated with fixed CAPS.
  GstCaps *sink_caps =
      gst_pad_get_current_caps (GST_BASE_PARSE_SINK_PAD (base_parser));
  if (sink_caps == NULL) {
    if ((!parser->data_parsed) || gst_ffmpeg_parser_restart (parser)) {
      gst_ffmpeg_caps_with_codecid (parser->codec_context->codec_id,
          parser->codec_context->codec_type, caps, parser->codec_context);
      return TRUE;
    }
    return FALSE;
  }

  gboolean result = TRUE;
  if (!gst_caps_is_equal (caps, sink_caps)) {
    result = gst_ffmpeg_parser_restart (parser);
    if (result) {
      gst_ffmpeg_caps_with_codecid (parser->codec_context->codec_id,
          parser->codec_context->codec_type, caps, parser->codec_context);
    }
  }

  gst_caps_unref (sink_caps);

  return result;
}

static gboolean
gst_ffmpeg_parser_start (GstBaseParse * base_parser)
{
  GstFFMpegParser *parser = (GstFFMpegParser *) base_parser;
  GstFFMpegParserClass *cls =
      (GstFFMpegParserClass *) G_OBJECT_GET_CLASS (parser);

  parser->codec_context = avcodec_alloc_context3 (cls->codec);
  if (parser->codec_context == NULL) {
    GST_ELEMENT_ERROR (parser, CORE, FAILED,
        ("avcodec_alloc_context3(): failed to initialize parser context for "
            "codec %s", cls->codec->name), NULL);
    return FALSE;
  }
  parser->codec_context->err_recognition = 1;
  parser->codec_context->workaround_bugs |= FF_BUG_AUTODETECT;

  parser->parser_context = av_parser_init (cls->codec->id);
  if (parser->parser_context == NULL) {
    GST_ELEMENT_ERROR (parser, CORE, FAILED,
        ("av_parser_init(): failed to initialize parser context for codec %s",
            cls->codec->name), NULL);
    avcodec_free_context (&(parser->codec_context));
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_ffmpeg_parser_stop (GstBaseParse * base_parser)
{
  GstFFMpegParser *parser = (GstFFMpegParser *) base_parser;

  if (parser->src_caps != NULL) {
    gst_caps_unref (parser->src_caps);
    parser->src_caps = NULL;
  }

  parser->data_parsed = FALSE;

  av_parser_close (parser->parser_context);
  parser->parser_context = NULL;

  avcodec_free_context (&(parser->codec_context));

  return TRUE;
}

///////////////////////////////////////////////////////////////////////////////
// GstFFMpegParser: helper functions
///////////////////////////////////////////////////////////////////////////////

#define EMIT_AV_ERROR(parser, code, context, ...)                           \
  GST_ELEMENT_ERROR (                                                       \
    (parser), LIBRARY, FAILED,                                              \
    ((context ": %s"), __VA_ARGS__ __VA_OPT__(,) av_err2str (code)), NULL);

static gboolean
gst_ffmpeg_parser_drain (GstFFMpegParser * parser)
{
  uint8_t *result_buffer;
  int result_size;
  av_parser_parse2 (parser->parser_context, parser->codec_context,
      &result_buffer, &result_size, NULL, 0, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);

  if (result_size == 0) {
    return TRUE;
  }
  // TODO: The `g723_1` parser, as it's currently coded, will never exercise
  // the code below this comment.  Other parsers in `ffmpeg` may exercise this
  // code, but they'll need to be enabled.  When those parsers are enabled,
  // this code should be checked for correctness.

  if (!gst_ffmpeg_parser_update_src_caps (parser)) {
    return FALSE;
  }

  GstBaseParseFrame frame;
  gst_base_parse_frame_init (&frame);
  frame.buffer = gst_buffer_new ();
  frame.out_buffer = gst_buffer_new_memdup (
      (gconstpointer) result_buffer, (gsize) result_size);
  GstFlowReturn result = gst_base_parse_finish_frame (
      (GstBaseParse *) parser, &frame, 0);
  gst_base_parse_frame_free (&frame);

  if (result != GST_FLOW_OK) {
    GST_ELEMENT_ERROR (parser, STREAM, FAILED,
        ("gst_base_parse_finish_frame(): failed to send frame during "
            "draining: %s", gst_flow_get_name (result)), NULL);
    return FALSE;
  }
  return TRUE;
}

static gboolean
gst_ffmpeg_parser_restart (GstFFMpegParser * parser)
{
  GstBaseParse *base_parser = (GstBaseParse *) parser;
  gst_base_parse_drain (base_parser);
  if (!gst_ffmpeg_parser_drain (parser)) {
    return FALSE;
  }
  gst_ffmpeg_parser_stop (base_parser);
  return gst_ffmpeg_parser_start (base_parser);
}

static GstCaps *
gst_ffmpeg_parser_send_updated_src_caps (GstFFMpegParser * parser)
{
  GstFFMpegParserClass *cls =
      (GstFFMpegParserClass *) G_OBJECT_GET_CLASS (parser);

  GstCaps *src_caps =
      gst_ffmpeg_make_parser_src_caps (parser->codec_context, cls->mime_type);
  if (src_caps == NULL) {
    GST_ELEMENT_ERROR (parser, STREAM, WRONG_TYPE,
        ("gst_ffmpeg_make_parser_src_caps(): failed to deduce CAPS from codec "
            "context and MIME type"), NULL);
    return NULL;
  }

  if (!gst_pad_set_caps (GST_BASE_PARSE_SRC_PAD (parser), src_caps)) {
    GST_ELEMENT_ERROR (parser, STREAM, WRONG_TYPE,
        ("gst_pad_set_caps(): failed to set 'src' pad CAPS to '%"
            GST_PTR_FORMAT "'", src_caps), NULL);
    gst_caps_unref (src_caps);
    return NULL;
  }

  return src_caps;
}

static gboolean
gst_ffmpeg_parser_update_src_caps (GstFFMpegParser * parser)
{
  if (G_UNLIKELY (parser->src_caps == NULL)) {

    // If we're sending out the first frame, then we need to ensure we send
    // CAPS over the source pad before sending the frame.  At this point, the
    // codec context will reflect any *base* CAPS data we may have received
    // over the sink pad, and any *overridden* CAPS data that may have been set
    // by the codec parser as it received data to parse.
    gst_ffmpeg_parser_caps_snapshot_init (&(parser->caps_snapshot),
        parser->codec_context);

    GstCaps *src_caps = gst_ffmpeg_parser_send_updated_src_caps (parser);
    if (src_caps == NULL) {
      return FALSE;
    }
    parser->src_caps = src_caps;
    return TRUE;
  }
  // It's possible that the codec parser has updated the context in between the
  // time we sent the last frame and now.  If the data we use to infer source
  // pad CAPS has been updated, then we need to send new CAPS over the source
  // pad before we send out the current frame.
  if (!gst_ffmpeg_parser_caps_snapshot_update (&(parser->caps_snapshot),
          parser->codec_context)) {
    return TRUE;
  }

  GstCaps *src_caps = gst_ffmpeg_parser_send_updated_src_caps (parser);
  if (src_caps == NULL) {
    return FALSE;
  }
  gst_caps_unref (parser->src_caps);
  parser->src_caps = src_caps;

  return TRUE;
}

///////////////////////////////////////////////////////////////////////////////
// GstFFMpegParser: class construction/destruction
///////////////////////////////////////////////////////////////////////////////

#define GST_FFPARSER_PARAMS_QDATA \
  (g_quark_from_static_string ("avparse-params"))

static void
gst_ffmpeg_parser_base_finalize (GstFFMpegParserClass * cls)
{
  g_free (cls->mime_type);
}

static void
gst_ffmpeg_parser_base_init (GstFFMpegParserClass * cls)
{
  const AVCodec *codec =
      (const AVCodec *) g_type_get_qdata (G_OBJECT_CLASS_TYPE (cls),
      GST_FFPARSER_PARAMS_QDATA);
  g_assert (codec != NULL);

  GstElementClass *element_cls = (GstElementClass *) cls;

  gchar *long_name =
      g_strdup_printf ("libav %s codec parser", codec->long_name);
  gchar *description = g_strdup_printf ("libav %s codec parser", codec->name);
  gst_element_class_set_metadata (element_cls, long_name, "Codec/Parser",
      description, "Devin Anderson <danderson@microsoft.com>");
  g_free (description);
  g_free (long_name);

  GstCaps *sink_caps = gst_ffmpeg_codecid_to_caps (codec->id, NULL, FALSE);
  if (sink_caps == NULL) {
    GST_WARNING ("couldn't get sink caps for parser '%s'", codec->name);
    sink_caps = gst_caps_from_string ("unknown/unknown");
    g_assert (sink_caps != NULL);
  }
  GstPadTemplate *sink_template =
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, sink_caps);
  g_assert (sink_template != NULL);
  gst_element_class_add_pad_template (element_cls, sink_template);

  GstCaps *src_caps = gst_caps_copy (sink_caps);
  gst_caps_unref (sink_caps);
  gst_ffmpeg_caps_set_framed (src_caps);
  GstPadTemplate *src_template =
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, src_caps);
  g_assert (src_template != NULL);
  gst_element_class_add_pad_template (element_cls, src_template);

  cls->codec = codec;
  cls->mime_type =
      g_strdup (gst_structure_get_name (gst_caps_get_structure (src_caps, 0)));

  gst_caps_unref (src_caps);
}

static void
gst_ffmpeg_parser_class_init (GstFFMpegParserClass * cls)
{
  GObjectClass *object_cls = (GObjectClass *) cls;
  object_cls->finalize = gst_ffmpeg_parser_finalize;

  GstBaseParseClass *parse_cls = (GstBaseParseClass *) cls;
  parse_cls->handle_frame = gst_ffmpeg_parser_handle_frame;
  parse_cls->set_sink_caps = gst_ffmpeg_parser_set_sink_caps;
  parse_cls->start = gst_ffmpeg_parser_start;
  parse_cls->stop = gst_ffmpeg_parser_stop;

  cls->default_sink_event_handler = parse_cls->sink_event;
  parse_cls->sink_event = gst_ffmpeg_parser_process_sink_event;
}

///////////////////////////////////////////////////////////////////////////////
// GstFFMpegParser: instance initialization
///////////////////////////////////////////////////////////////////////////////

static void
gst_ffmpeg_parser_init (GstFFMpegParser * parser)
{
  GstBaseParse *base_parser = (GstBaseParse *) parser;
  gst_base_parse_set_infer_ts (base_parser, FALSE);
  gst_base_parse_set_syncable (base_parser, FALSE);

  parser->codec_context = NULL;
  parser->parser_context = NULL;

  parser->data_parsed = FALSE;
  parser->frame_buffer = NULL;
  parser->frame_buffer_size = 0;
  parser->src_caps = NULL;
}

///////////////////////////////////////////////////////////////////////////////
// GstFFMpegParser: element registration
///////////////////////////////////////////////////////////////////////////////

/**
 * element-avparse_g723_1
 *
 * Since: 1.23
 */

gboolean
gst_ffmpeg_parser_register (GstPlugin * plugin)
{
  GTypeInfo type_info = {
    sizeof (GstFFMpegParserClass),
    (GBaseInitFunc) gst_ffmpeg_parser_base_init,
    (GBaseFinalizeFunc) gst_ffmpeg_parser_base_finalize,
    (GClassInitFunc) gst_ffmpeg_parser_class_init,
    NULL,
    NULL,
    sizeof (GstFFMpegParser),
    0,
    (GInstanceInitFunc) gst_ffmpeg_parser_init
  };

  GST_INFO ("registering parser types");

  void *iteration_state = NULL;
  for (;;) {

    const AVCodecParser *parser = av_parser_iterate (&iteration_state);
    if (parser == NULL) {
      break;
    }

    size_t max_codec_ids = sizeof (parser->codec_ids) / sizeof (int);
    for (size_t i = 0; i < max_codec_ids; ++i) {

      enum AVCodecID codec_id = parser->codec_ids[i];
      guint rank;
      switch (codec_id) {
        case AV_CODEC_ID_NONE:
          goto next_parser;

        case AV_CODEC_ID_G723_1:
          rank = GST_RANK_MARGINAL;
          break;

        default:
          continue;
      }

      const AVCodec *codec = avcodec_find_decoder (codec_id);
      if (codec == NULL) {
        g_warning
            ("avcodec_find_decoder(): failed to get decoder for codec id %d",
            codec_id);
        continue;
      }

      gchar *type_name = g_strdup_printf ("avparse_%s", codec->name);
      g_strdelimit (type_name, ".,|-<> ", '_');

      GType type = g_type_from_name (type_name);
      if (!type) {
        type = g_type_register_static (GST_TYPE_BASE_PARSE, type_name,
            &type_info, 0);
        g_type_set_qdata (type, GST_FFPARSER_PARAMS_QDATA, (gpointer) codec);
      }

      if (!gst_element_register (plugin, type_name, rank, type)) {
        g_warning ("failed to register type %s", type_name);
        g_free (type_name);
        return FALSE;
      }

      GST_INFO ("successfully registered parser type %s", type_name);

      g_free (type_name);
    }

  next_parser:
    ;
  }

  GST_INFO ("successfully registered parser types");
  return TRUE;
}
