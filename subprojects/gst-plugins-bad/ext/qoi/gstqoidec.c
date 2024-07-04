/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *
 * Filter:
 * Copyright (C) 2000 Donald A. Graft
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
 *
 */
 /**
 * SECTION:element-qoidec
 * @title: qoidec
 *
 * Decodes qoi files.
 *
 * ## Example launch line
 * |[
 * gst-launch-1.0 filesrc location=encoded.qoi ! qoidec ! filesink location=decoded.rgb
 * ]|
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/base/gstbytereader.h>
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>

#include "gstqoidec.h"

#define QOI_IMPLEMENTATION

#include <qoi.h>

struct _GstQoiDec
{
  GstVideoDecoder parent;
  GstVideoCodecState *input_state;
  GstVideoCodecState *output_state;
  gsize read_data;
  qoi_desc *qoi_description;
};

GST_DEBUG_CATEGORY_STATIC (qoidec_debug);
#define GST_CAT_DEFAULT qoidec_debug

/* Src caps */
#define QOI_ENCODER_STATIC_CAPS_SRC     \
  "video/x-raw, "                       \
  "format = (string) { RGBA, RGB }, "   \
  "width = (int) [ 1, 20000 ], "        \
  "height = (int) [ 1, 20000 ], "       \
  "framerate = " GST_VIDEO_FPS_RANGE

/* Sink caps */
#define QOI_ENCODER_STATIC_CAPS_SINK    \
  "image/qoi"                           \

static GstStaticPadTemplate gst_qoidec_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (QOI_ENCODER_STATIC_CAPS_SRC)
    );

static GstStaticPadTemplate gst_qoidec_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (QOI_ENCODER_STATIC_CAPS_SINK)
    );

#define parent_class gst_qoidec_parent_class
G_DEFINE_TYPE (GstQoiDec, gst_qoidec, GST_TYPE_VIDEO_DECODER);
GST_ELEMENT_REGISTER_DEFINE (qoidec, "qoidec", GST_RANK_PRIMARY,
    GST_TYPE_QOIDEC);

static GstFlowReturn
gst_qoidec_caps_create_and_set (GstQoiDec * qoidec, guint32 width,
    guint32 height, guint8 channels);

static gboolean gst_qoidec_start (GstVideoDecoder * decoder);
static gboolean gst_qoidec_stop (GstVideoDecoder * decoder);
static gboolean gst_qoidec_flush (GstVideoDecoder * decoder);
static gboolean gst_qoidec_set_format (GstVideoDecoder * Decoder,
    GstVideoCodecState * state);
static GstFlowReturn gst_qoidec_parse (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame, GstAdapter * adapter, gboolean at_eos);
static GstFlowReturn gst_qoidec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame);
static gboolean gst_qoidec_decide_allocation (GstVideoDecoder * decoder,
    GstQuery * query);
static gboolean gst_qoidec_sink_event (GstVideoDecoder * bdec,
    GstEvent * event);

static void
gst_qoidec_class_init (GstQoiDecClass * klass)
{
  GstElementClass *element_class;
  GstVideoDecoderClass *vdec_class;

  element_class = (GstElementClass *) klass;
  vdec_class = (GstVideoDecoderClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gst_element_class_add_static_pad_template (element_class,
      &gst_qoidec_src_pad_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_qoidec_sink_pad_template);

  gst_element_class_set_static_metadata (element_class,
      "Quiet-OK Image decoder", "Codec/Decoder/Image",
      "Decode a qoi video frame to a raw image",
      "Abhay Chirania <a-chirania@ti.com>");

  vdec_class->start = gst_qoidec_start;
  vdec_class->stop = gst_qoidec_stop;
  vdec_class->flush = gst_qoidec_flush;
  vdec_class->set_format = gst_qoidec_set_format;
  vdec_class->parse = gst_qoidec_parse;
  vdec_class->handle_frame = gst_qoidec_handle_frame;
  vdec_class->decide_allocation = gst_qoidec_decide_allocation;
  vdec_class->sink_event = gst_qoidec_sink_event;

  (void) qoi_encode;

  GST_DEBUG_CATEGORY_INIT (qoidec_debug, "qoidec", 0, "Quiet-OK image decoder");
}

static void
gst_qoidec_init (GstQoiDec * qoidec)
{

  qoidec->read_data = 0;

  gst_video_decoder_set_use_default_pad_acceptcaps (GST_VIDEO_DECODER_CAST
      (qoidec), TRUE);

  GST_PAD_SET_ACCEPT_TEMPLATE (GST_VIDEO_DECODER_SINK_PAD (qoidec));

  qoidec->qoi_description = (qoi_desc *) g_malloc (sizeof (qoi_desc));
}

static gboolean
gst_qoidec_set_format (GstVideoDecoder * decoder, GstVideoCodecState * state)
{
  GstQoiDec *qoidec = (GstQoiDec *) decoder;

  if (qoidec->input_state)
    gst_video_codec_state_unref (qoidec->input_state);
  qoidec->input_state = gst_video_codec_state_ref (state);

  return TRUE;
}

static GstFlowReturn
gst_qoidec_caps_create_and_set (GstQoiDec * qoidec, guint32 width,
    guint32 height, guint8 channels)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstVideoFormat format = GST_VIDEO_FORMAT_UNKNOWN;
  GstVideoInfo *info;

  g_return_val_if_fail (GST_IS_QOIDEC (qoidec), GST_FLOW_ERROR);

  if (channels == 4) {
    format = GST_VIDEO_FORMAT_RGBA;
  } else if (channels == 3) {
    format = GST_VIDEO_FORMAT_RGB;
  }

  if (format == GST_VIDEO_FORMAT_UNKNOWN) {
    GST_ELEMENT_ERROR (qoidec, STREAM, NOT_IMPLEMENTED, (NULL),
        ("Invalid channels in qoi frame"));
    ret = GST_FLOW_NOT_SUPPORTED;
    goto done;
  }

  /* Check if output state changed */
  if (qoidec->output_state) {
    info = &qoidec->output_state->info;
    if (width == GST_VIDEO_INFO_WIDTH (info) &&
        height == GST_VIDEO_INFO_HEIGHT (info) &&
        format == GST_VIDEO_INFO_FORMAT (info)) {
      goto done;
    }
    gst_video_codec_state_unref (qoidec->output_state);
  }

  qoidec->output_state =
      gst_video_decoder_set_output_state (GST_VIDEO_DECODER (qoidec), format,
      width, height, qoidec->input_state);

  gst_video_decoder_negotiate (GST_VIDEO_DECODER (qoidec));

  GST_DEBUG ("Final width=%d height=%d",
      GST_VIDEO_INFO_WIDTH (&qoidec->output_state->info),
      GST_VIDEO_INFO_HEIGHT (&qoidec->output_state->info));

done:
  return ret;
}

static GstFlowReturn
gst_qoidec_handle_frame (GstVideoDecoder * decoder, GstVideoCodecFrame * frame)
{
  GstQoiDec *qoidec = (GstQoiDec *) decoder;
  GstFlowReturn ret = GST_FLOW_OK;
  GstMapInfo frame_map;
  GstVideoFrame out_frame;
  guint input_buffer_size;
  guint32 width, height;
  guint8 channels;

  input_buffer_size = (guint) gst_buffer_get_size (frame->input_buffer);

  GST_LOG_OBJECT (qoidec, "Got buffer, size=%u", input_buffer_size);

  if (input_buffer_size < 12) {
    GST_WARNING_OBJECT (qoidec, "Input buffer size too small");
    ret = GST_FLOW_ERROR;
    goto done;
  }

  if (!gst_buffer_map (frame->input_buffer, &frame_map, GST_MAP_READ)) {
    GST_WARNING_OBJECT (qoidec, "Failed to map input buffer");
    ret = GST_FLOW_ERROR;
    goto done;
  }

  width = (guint32) (frame_map.data[4] << 24 | frame_map.data[5] << 16 |
      frame_map.data[6] << 8 | frame_map.data[7] << 0);

  height = (guint32) (frame_map.data[8] << 24 | frame_map.data[9] << 16 |
      frame_map.data[10] << 8 | frame_map.data[11] << 0);

  channels = (guint8) (frame_map.data[12]);

  ret = gst_qoidec_caps_create_and_set (qoidec, width, height, channels);
  if (ret != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (qoidec, "failed to output state");
    gst_buffer_unmap (frame->input_buffer, &frame_map);
    goto done;
  }

  /* Allocate output buffer */
  ret = gst_video_decoder_allocate_output_frame (decoder, frame);
  if (G_UNLIKELY (ret != GST_FLOW_OK)) {
    GST_DEBUG_OBJECT (qoidec, "failed to acquire output buffer");
    gst_buffer_unmap (frame->input_buffer, &frame_map);
    goto done;
  }

  /* Map output frame */
  if (!gst_video_frame_map (&out_frame, &qoidec->output_state->info,
          frame->output_buffer, GST_MAP_WRITE)) {
    GST_DEBUG_OBJECT (qoidec, "failed to map output buffer");
    gst_buffer_unmap (frame->input_buffer, &frame_map);
    goto done;
  }

  qoi_decode (frame_map.data, frame_map.size, qoidec->qoi_description, channels,
      GST_VIDEO_FRAME_COMP_DATA (&out_frame, 0));

  gst_video_frame_unmap (&out_frame);

  gst_buffer_unmap (frame->input_buffer, &frame_map);

  ret = gst_video_decoder_finish_frame (GST_VIDEO_DECODER (qoidec), frame);

  qoidec->read_data = 0;

done:
  GST_DEBUG_OBJECT (qoidec, "END, ret:%d", ret);

  return ret;
}

static GstFlowReturn
gst_qoidec_parse (GstVideoDecoder * decoder, GstVideoCodecFrame * frame,
    GstAdapter * adapter, gboolean at_eos)
{
  gsize toadd = 0;
  GstByteReader reader;
  gconstpointer data;
  guint32 signature;
  guint64 end;
  gsize size;
  GstQoiDec *qoidec = (GstQoiDec *) decoder;

  GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (frame);

  size = gst_adapter_available (adapter);
  GST_DEBUG ("Parsing QOI image data (%" G_GSIZE_FORMAT " bytes)", size);

  if (size < 8)
    goto need_more_data;

  data = gst_adapter_map (adapter, size);
  gst_byte_reader_init (&reader, data, size);

  if (qoidec->read_data == 0) {
    if (!gst_byte_reader_peek_uint32_be (&reader, &signature))
      goto need_more_data;
    if (signature != 0x716F6966) {
      for (;;) {
        guint offset;

        offset = gst_byte_reader_masked_scan_uint32 (&reader, 0xffffffff,
            0x716F6966, 0, gst_byte_reader_get_remaining (&reader));

        if (offset == -1) {
          gst_adapter_flush (adapter,
              gst_byte_reader_get_remaining (&reader) - 4);
          goto need_more_data;
        }

        if (!gst_byte_reader_skip (&reader, offset))
          goto need_more_data;

        if (!gst_byte_reader_peek_uint32_be (&reader, &signature))
          goto need_more_data;

        if (signature == 0x716F6966) {
          gst_adapter_flush (adapter, gst_byte_reader_get_pos (&reader));
          goto need_more_data;
        }
        if (!gst_byte_reader_skip (&reader, 4))
          goto need_more_data;
      }
    }
    qoidec->read_data = 4;
  }

  if (!gst_byte_reader_skip (&reader, qoidec->read_data))
    goto need_more_data;

  for (;;) {

    guint offset;

    offset = gst_byte_reader_masked_scan_uint32 (&reader, 0xffffffff,
        0x00000001, 0, gst_byte_reader_get_remaining (&reader));

    if (offset == -1)
      goto need_more_data;

    if (!gst_byte_reader_skip (&reader, offset - 4))
      goto need_more_data;

    if (!gst_byte_reader_get_uint64_be (&reader, &end))
      goto need_more_data;

    if (end == 0x0000000000000001) {
      /* Have complete frame */
      GST_DEBUG ("End Code: %lx", end);
      toadd = gst_byte_reader_get_pos (&reader);
      GST_DEBUG_OBJECT (decoder, "Have complete frame of size %" G_GSIZE_FORMAT,
          toadd);
      qoidec->read_data = 0;
      goto have_full_frame;
    }
  }

  g_assert_not_reached ();
  return GST_FLOW_ERROR;

need_more_data:
  return GST_VIDEO_DECODER_FLOW_NEED_DATA;

have_full_frame:
  if (toadd)
    gst_video_decoder_add_to_frame (decoder, toadd);
  return gst_video_decoder_have_frame (decoder);
}

static gboolean
gst_qoidec_decide_allocation (GstVideoDecoder * bdec, GstQuery * query)
{
  GstBufferPool *pool = NULL;
  GstStructure *config;

  if (!GST_VIDEO_DECODER_CLASS (parent_class)->decide_allocation (bdec, query))
    return FALSE;

  if (gst_query_get_n_allocation_pools (query) > 0)
    gst_query_parse_nth_allocation_pool (query, 0, &pool, NULL, NULL, NULL);

  if (pool == NULL)
    return FALSE;

  config = gst_buffer_pool_get_config (pool);
  if (gst_query_find_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL)) {
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);
  }
  gst_buffer_pool_set_config (pool, config);
  gst_object_unref (pool);

  return TRUE;
}

static gboolean
gst_qoidec_sink_event (GstVideoDecoder * bdec, GstEvent * event)
{
  const GstSegment *segment;

  if (GST_EVENT_TYPE (event) != GST_EVENT_SEGMENT)
    goto done;

  gst_event_parse_segment (event, &segment);

  if (segment->format == GST_FORMAT_TIME)
    gst_video_decoder_set_packetized (bdec, TRUE);
  else
    gst_video_decoder_set_packetized (bdec, FALSE);

done:
  return GST_VIDEO_DECODER_CLASS (parent_class)->sink_event (bdec, event);
}

static gboolean
gst_qoidec_start (GstVideoDecoder * decoder)
{
  GstQoiDec *qoidec = (GstQoiDec *) decoder;

  gst_video_decoder_set_packetized (GST_VIDEO_DECODER (qoidec), FALSE);

  return TRUE;
}

static gboolean
gst_qoidec_stop (GstVideoDecoder * decoder)
{
  GstQoiDec *qoidec = (GstQoiDec *) decoder;

  if (qoidec->input_state) {
    gst_video_codec_state_unref (qoidec->input_state);
    qoidec->input_state = NULL;
  }

  if (qoidec->output_state) {
    gst_video_codec_state_unref (qoidec->output_state);
    qoidec->output_state = NULL;
  }

  if (qoidec->qoi_description) {
    g_free (qoidec->qoi_description);
  }

  return TRUE;
}

static gboolean
gst_qoidec_flush (GstVideoDecoder * decoder)
{
  return TRUE;
}
