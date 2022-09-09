#include <gst/check/gstcheck.h>
#include <gst/check/gstharness.h>
#include <gst/rtp/gstrtpbuffer.h>

/* OBU_TEMPORAL_DELIMITER */
static guint8 av1_obu_temp_delim[] = {
  0x12, 0x00
};

/* OBU_SEQUENCER_HEADER */
static guint8 av1_obu_seq_hdr[] = {
  0x0a, 0x0b, 0x00, 0x00, 0x00, 0x03, 0x37, 0xff, 0xfc, 0x0f, 0xff, 0x98, 0x04
};

/* OBU_FRAME */
static guint8 av1_obu_frame[] = {
  0x32, 0x1e, 0x10, 0x00, 0x00, 0x03, 0x00, 0x00, 0xe4, 0xef, 0xbc, 0xf2, 0x51,
  0xd8, 0x9f, 0x75, 0x6a,
  0xd0, 0xde, 0x30, 0xae, 0x3e, 0x50, 0xc9, 0xf0, 0xce, 0x0a, 0xd2, 0x04, 0x66,
  0x81, 0xf0
};

/* RTP encoded OBU_TEMPORAL_DELIMITER */
static guint8 rtp_av1_obu_temp_delim[] = {
  0x80, 0x60, 0x36, 0x42, 0x52, 0xdb, 0xb0, 0xa2, 0xba, 0x6b, 0x23, 0x0c, 0x18,
  0x12, 0x00
};

/* RTP encoded OBU_SEQUENCER_HEADER */
static guint8 rtp_av1_obu_seq_hdr[] = {
  0x80, 0x60, 0x5c, 0x20, 0xc6, 0x79, 0xec, 0xe6, 0x3b, 0xa8, 0x21, 0x89, 0x18,
  0x0a, 0x0b, 0x00,
  0x00, 0x00, 0x03, 0x37, 0xff, 0xfc, 0x0f, 0xff, 0x98, 0x04
};

/* RTP encoded OBU_FRAME */
static guint8 rtp_av1_obu_frame[] = {
  0x80, 0xe0, 0x5c, 0x21, 0xc6, 0x79, 0xec, 0xe6, 0x3b, 0xa8, 0x21, 0x89, 0x10,
  0x32, 0x1e, 0x10,
  0x00, 0x00, 0x03, 0x00, 0x00, 0xe4, 0xef, 0xbc, 0xf2, 0x51, 0xd8, 0x9f, 0x75,
  0x6a, 0xd0, 0xde,
  0x30, 0xae, 0x3e, 0x50, 0xc9, 0xf0, 0xce, 0x0a, 0xd2, 0x04, 0x66, 0x81, 0xf0,
};

/* RTP encoded aggregated TU */
static guint8 rtp_av1_agg_tu[] = {
  0x80, 0xe0, 0x71, 0xc7, 0x3e, 0x1a, 0x1c, 0x31, 0xfe, 0x66, 0x46, 0xe6, 0x28,
  0x0d, 0x0a, 0x0b,
  0x00, 0x00, 0x00, 0x03, 0x37, 0xff, 0xfc, 0x0f, 0xff, 0x98, 0x04, 0x32, 0x1e,
  0x10, 0x00, 0x00,
  0x03, 0x00, 0x00, 0xe4, 0xef, 0xbc, 0xf2, 0x51, 0xd8, 0x9f, 0x75, 0x6a, 0xd0,
  0xde, 0x30, 0xae,
  0x3e, 0x50, 0xc9, 0xf0, 0xce, 0x0a, 0xd2, 0x04, 0x66, 0x81, 0xf0
};

/* RTP encoded OBU_SEQUENCER_HEADER with a fragment of a OBU_FRAME */
static guint8 rtp_av1_frag1_tu[] = {
  0x80, 0x60, 0x32, 0x88, 0x55, 0x6c, 0x38, 0x9f, 0x62, 0x5c, 0xc5, 0x04, 0x68,
  0x0d, 0x0a, 0x0b, 0x00, 0x00, 0x00, 0x03, 0x37, 0xff, 0xfc, 0x0f, 0xff,
  0x98, 0x04, 0x32, 0x1e, 0x10, 0x00,
};

/* RTP continued fragment of a OBU_FRAME */
static guint8 rtp_av1_frag2_tu[] = {
  0x80, 0x60, 0x32, 0x89, 0x55, 0x6c, 0x38, 0x9f, 0x62, 0x5c, 0xc5, 0x04, 0xd0,
  0x00, 0x03, 0x00, 0x00, 0xe4, 0xef, 0xbc, 0xf2, 0x51, 0xd8, 0x9f, 0x75,
  0x6a, 0xd0, 0xde, 0x30, 0xae, 0x3e,
};

/* RTP completed fragment of a OBU_FRAME */
static guint8 rtp_av1_frag3_tu[] = {
  0x80, 0xe0, 0x32, 0x8a, 0x55, 0x6c, 0x38, 0x9f, 0x62, 0x5c, 0xc5, 0x04, 0x90,
  0x50, 0xc9, 0xf0, 0xce, 0x0a, 0xd2, 0x04, 0x66, 0x81, 0xf0
};


GST_DEBUG_CATEGORY_STATIC (rtpav1test_debug);
#define GST_CAT_DEFAULT (rtpav1test_debug)

guint32
gst_rtp_av1_read_leb128 (const guint8 * leb128, guint8 * read_bytes,
    guint max_len)
{
  guint64 value = 0;
  guint8 read = 0;

  for (guint8 i = 0; i < 8 && i < max_len; i++, leb128++) {
    value |= ((*leb128 & 0x7f) << (i * 7));
    read++;

    if (~*leb128 & 0x80)
      break;
  }

  *read_bytes = read;

  g_assert (value <= (1ull << 32) - 1);
  return value;
}

guint64
gst_rtp_av1_write_leb128 (guint64 value, guint8 * bytes_written)
{
  guint64 leb128 = 0;
  guint8 written = 0;

  for (guint8 i = 0; i < 8; i++) {
    guint8 byte = value & 0x7F;
    value >>= 7;
    byte |= (value != 0) << 7;
    leb128 |= byte << (i * 8);
    written++;

    if (value == 0)
      break;
  }

  *bytes_written = written;
  return leb128;
}

typedef struct _Av1AggregateHeader Av1AggregateHeader;

struct _Av1AggregateHeader
{
  guint8 pad:3;
  guint8 n:1;
  guint8 w:2;
  guint8 y:1;
  guint8 z:1;
};

static gsize
validate_rtp_payload (GstBuffer * buffer, Av1AggregateHeader expected_header,
    guint8 ** expected_content, gsize no_obu_elements, gboolean marker)
{
  GstRTPBuffer rtp_buf = GST_RTP_BUFFER_INIT;
  GstBuffer *payload_buf;
  guint8 *payload;
  gsize payload_len;
  gsize parsed_elements;
  gsize parsed_bytes;
  gsize combined_obu_size;
  Av1AggregateHeader *actual_hdr;

  fail_unless (GST_IS_BUFFER (buffer));
  fail_unless (gst_rtp_buffer_map (buffer, GST_MAP_READWRITE, &rtp_buf));
  payload = gst_rtp_buffer_get_payload (&rtp_buf);
  payload_buf = gst_rtp_buffer_get_payload_buffer (&rtp_buf);
  payload_len = gst_rtp_buffer_get_payload_len (&rtp_buf);
  actual_hdr = (Av1AggregateHeader *) payload;
  fail_unless (gst_rtp_buffer_get_marker (&rtp_buf) == marker);
  fail_unless_equals_int (expected_header.z, actual_hdr->z);
  fail_unless_equals_int (expected_header.y, actual_hdr->y);
  fail_unless_equals_int (expected_header.n, actual_hdr->n);

  payload++;
  parsed_elements = 0;
  parsed_bytes = 1;
  combined_obu_size = 0;
  while (parsed_bytes < payload_len) {
    guint8 *content;
    gsize obu_element_size;
    fail_unless (parsed_elements < no_obu_elements);
    content = expected_content[parsed_elements];

    if (actual_hdr->w == 0 || (parsed_elements != actual_hdr->w - 1)) {
      guint8 leb128_len = 0;
      obu_element_size =
          gst_rtp_av1_read_leb128 (payload, &leb128_len,
          payload_len - parsed_bytes);

      payload += leb128_len;
      parsed_bytes += leb128_len;
    } else {
      obu_element_size = payload_len - parsed_bytes;
    }

    fail_unless_equals_int (gst_buffer_memcmp (payload_buf, parsed_bytes,
            content, obu_element_size), 0);

    parsed_elements++;
    parsed_bytes += obu_element_size;
    payload += obu_element_size;
    combined_obu_size += obu_element_size;
  }

  gst_rtp_buffer_unmap (&rtp_buf);
  gst_buffer_unref (buffer);

  return combined_obu_size;
}

GST_START_TEST (test_rtpav1pay_agg_none)
{
  GstHarness *h =
      gst_harness_new_parse ("rtpav1pay mtu=1500 aggregate-mode=none");
  GstBuffer *in_buf;
  GstBuffer *out_buf;
  GstFlowReturn ret;
  gst_harness_set_src_caps_str (h, "video/x-av1, alignment=tu");

  in_buf =
      gst_buffer_new_and_alloc (sizeof (av1_obu_seq_hdr) +
      sizeof (av1_obu_frame));
  fail_unless_equals_int (gst_buffer_fill (in_buf, 0, av1_obu_seq_hdr,
          sizeof (av1_obu_seq_hdr)), sizeof (av1_obu_seq_hdr));
  fail_unless_equals_int (gst_buffer_fill (in_buf, sizeof (av1_obu_seq_hdr),
          av1_obu_frame, sizeof (av1_obu_frame)), sizeof (av1_obu_frame));

  ret = gst_harness_push (h, in_buf);
  fail_unless_equals_int (ret, GST_FLOW_OK);
  fail_unless_equals_int (gst_harness_buffers_in_queue (h), 2);

  out_buf = gst_harness_pull (h);
  Av1AggregateHeader expected_header = { 0, 1, 1, 0, 0 };
  guint8 *expected_content[1] = { av1_obu_seq_hdr };

  validate_rtp_payload (out_buf, expected_header, expected_content, 1, FALSE);

  out_buf = gst_harness_pull (h);
  Av1AggregateHeader expected_header2 = { 0, 0, 1, 0, 0 };
  guint8 *expected_content2[1] = { av1_obu_frame };

  validate_rtp_payload (out_buf, expected_header2, expected_content2, 1, TRUE);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_rtpav1pay_agg_tu)
{
  GstHarness *h =
      gst_harness_new_parse ("rtpav1pay mtu=1500 aggregate-mode=tu");
  GstBuffer *in_buf;
  GstBuffer *out_buf;
  GstFlowReturn ret;
  gst_harness_set_src_caps_str (h, "video/x-av1, alignment=tu");

  in_buf =
      gst_buffer_new_and_alloc (sizeof (av1_obu_seq_hdr) +
      sizeof (av1_obu_frame));
  fail_unless_equals_int (gst_buffer_fill (in_buf, 0, av1_obu_seq_hdr,
          sizeof (av1_obu_seq_hdr)), sizeof (av1_obu_seq_hdr));
  fail_unless_equals_int (gst_buffer_fill (in_buf, sizeof (av1_obu_seq_hdr),
          av1_obu_frame, sizeof (av1_obu_frame)), sizeof (av1_obu_frame));

  ret = gst_harness_push (h, in_buf);
  fail_unless_equals_int (ret, GST_FLOW_OK);
  fail_unless_equals_int (gst_harness_buffers_in_queue (h), 1);

  Av1AggregateHeader expected_header = { 0, 1, 0, 0, 0 };
  guint8 *expected_content[2] = { av1_obu_seq_hdr, av1_obu_frame };
  out_buf = gst_harness_pull (h);
  validate_rtp_payload (out_buf, expected_header, expected_content, 2, TRUE);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_rtpav1pay_agg_none_frag)
{
  GstHarness *h =
      gst_harness_new_parse ("rtpav1pay mtu=30 aggregate-mode=none");
  GstBuffer *in_buf;
  GstBuffer *out_buf;
  GstFlowReturn ret;
  gst_harness_set_src_caps_str (h, "video/x-av1, alignment=tu");

  in_buf = gst_buffer_new_and_alloc (sizeof (av1_obu_frame));
  fail_unless_equals_int (gst_buffer_fill (in_buf, 0, av1_obu_frame,
          sizeof (av1_obu_frame)), sizeof (av1_obu_frame));

  ret = gst_harness_push (h, in_buf);
  fail_unless_equals_int (ret, GST_FLOW_OK);
  fail_unless_equals_int (gst_harness_buffers_in_queue (h), 2);

  Av1AggregateHeader expected_header = { 0, 1, 0, 1, 0 };
  guint8 *expected_content[2] = { av1_obu_frame };
  out_buf = gst_harness_pull (h);
  gsize parsed =
      validate_rtp_payload (out_buf, expected_header, expected_content, 1,
      FALSE);

  Av1AggregateHeader expected_header2 = { 0, 0, 0, 0, 1 };
  guint8 *expected_content2[1] = { av1_obu_frame + parsed };
  out_buf = gst_harness_pull (h);
  validate_rtp_payload (out_buf, expected_header2, expected_content2, 1, TRUE);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_rtpav1pay_ignored)
{
  GstHarness *h =
      gst_harness_new_parse ("rtpav1pay mtu=30 aggregate-mode=none");
  GstBuffer *in_buf;
  GstFlowReturn ret;
  gst_harness_set_src_caps_str (h, "video/x-av1, alignment=tu");

  in_buf = gst_buffer_new_and_alloc (sizeof (av1_obu_temp_delim));
  fail_unless_equals_int (gst_buffer_fill (in_buf, 0, av1_obu_temp_delim,
          sizeof (av1_obu_temp_delim)), sizeof (av1_obu_temp_delim));

  ret = gst_harness_push (h, in_buf);
  fail_unless_equals_int (ret, GST_FLOW_OK);
  fail_unless_equals_int (gst_harness_buffers_in_queue (h), 0);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_rtpav1depay_agg_none)
{
  GstHarness *h = gst_harness_new ("rtpav1depay");
  GstBuffer *in_buf;
  GstBuffer *out_buf;
  GstFlowReturn ret;
  gsize out_buf_size;
  gst_harness_set_src_caps_str (h,
      "application/x-rtp,media=video,payload=(int)96,clock-rate=90000,encoding-name=AV1");

  in_buf = gst_buffer_new_and_alloc (sizeof (rtp_av1_obu_seq_hdr));
  fail_unless_equals_int (gst_buffer_fill (in_buf, 0, rtp_av1_obu_seq_hdr,
          sizeof (rtp_av1_obu_seq_hdr)), sizeof (rtp_av1_obu_seq_hdr));

  ret = gst_harness_push (h, in_buf);
  fail_unless_equals_int (ret, GST_FLOW_OK);
  fail_unless_equals_int (gst_harness_buffers_in_queue (h), 1);

  out_buf = gst_harness_pull (h);
  fail_unless (GST_IS_BUFFER (out_buf));
  out_buf_size = gst_buffer_get_size (out_buf);
  fail_unless_equals_int64 (out_buf_size, sizeof (av1_obu_seq_hdr));
  fail_unless_equals_int (gst_buffer_memcmp (out_buf, 0, av1_obu_seq_hdr,
          sizeof (av1_obu_seq_hdr)), 0);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_rtpav1depay_agg_tu)
{
  GstHarness *h = gst_harness_new ("rtpav1depay");
  GstBuffer *in_buf;
  GstBuffer *out_buf;
  GstFlowReturn ret;
  gsize out_buf_size;
  gst_harness_set_src_caps_str (h,
      "application/x-rtp,media=video,payload=(int)96,clock-rate=90000,encoding-name=AV1");

  in_buf = gst_buffer_new_and_alloc (sizeof (rtp_av1_agg_tu));
  fail_unless_equals_int (gst_buffer_fill (in_buf, 0, rtp_av1_agg_tu,
          sizeof (rtp_av1_agg_tu)), sizeof (rtp_av1_agg_tu));

  ret = gst_harness_push (h, in_buf);
  fail_unless_equals_int (ret, GST_FLOW_OK);
  fail_unless_equals_int (gst_harness_buffers_in_queue (h), 2);

  out_buf = gst_harness_pull (h);
  fail_unless (GST_IS_BUFFER (out_buf));
  out_buf_size = gst_buffer_get_size (out_buf);
  fail_unless_equals_int64 (out_buf_size, sizeof (av1_obu_seq_hdr));
  fail_unless_equals_int (gst_buffer_memcmp (out_buf, 0, av1_obu_seq_hdr,
          sizeof (av1_obu_seq_hdr)), 0);

  out_buf = gst_harness_pull (h);
  fail_unless (GST_IS_BUFFER (out_buf));
  out_buf_size = gst_buffer_get_size (out_buf);
  fail_unless_equals_int64 (out_buf_size, sizeof (av1_obu_frame));
  fail_unless_equals_int (gst_buffer_memcmp (out_buf, 0, av1_obu_frame,
          sizeof (av1_obu_frame)), 0);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_rtpav1depay_frag_tu)
{
  GstHarness *h = gst_harness_new ("rtpav1depay");
  GstBuffer *in_buf;
  GstBuffer *out_buf;
  GstFlowReturn ret;
  gsize out_buf_size;
  gst_harness_set_src_caps_str (h,
      "application/x-rtp,media=video,payload=(int)96,clock-rate=90000,encoding-name=AV1");

  in_buf = gst_buffer_new_and_alloc (sizeof (rtp_av1_frag1_tu));
  fail_unless_equals_int (gst_buffer_fill (in_buf, 0, rtp_av1_frag1_tu,
          sizeof (rtp_av1_frag1_tu)), sizeof (rtp_av1_frag1_tu));

  ret = gst_harness_push (h, in_buf);
  fail_unless_equals_int (ret, GST_FLOW_OK);
  fail_unless_equals_int (gst_harness_buffers_in_queue (h), 1);

  out_buf = gst_harness_pull (h);
  fail_unless (GST_IS_BUFFER (out_buf));
  out_buf_size = gst_buffer_get_size (out_buf);
  fail_unless_equals_int64 (out_buf_size, sizeof (av1_obu_seq_hdr));
  fail_unless_equals_int (gst_buffer_memcmp (out_buf, 0, av1_obu_seq_hdr,
          sizeof (av1_obu_seq_hdr)), 0);

  in_buf = gst_buffer_new_and_alloc (sizeof (rtp_av1_frag2_tu));
  fail_unless_equals_int (gst_buffer_fill (in_buf, 0, rtp_av1_frag2_tu,
          sizeof (rtp_av1_frag2_tu)), sizeof (rtp_av1_frag2_tu));
  ret = gst_harness_push (h, in_buf);
  fail_unless_equals_int (ret, GST_FLOW_OK);
  fail_unless_equals_int (gst_harness_buffers_in_queue (h), 0);

  in_buf = gst_buffer_new_and_alloc (sizeof (rtp_av1_frag3_tu));
  fail_unless_equals_int (gst_buffer_fill (in_buf, 0, rtp_av1_frag3_tu,
          sizeof (rtp_av1_frag3_tu)), sizeof (rtp_av1_frag3_tu));
  ret = gst_harness_push (h, in_buf);
  fail_unless_equals_int (ret, GST_FLOW_OK);
  fail_unless_equals_int (gst_harness_buffers_in_queue (h), 1);

  out_buf = gst_harness_pull (h);
  fail_unless (GST_IS_BUFFER (out_buf));
  out_buf_size = gst_buffer_get_size (out_buf);
  fail_unless_equals_int64 (out_buf_size, sizeof (av1_obu_frame));
  fail_unless_equals_int (gst_buffer_memcmp (out_buf, 0, av1_obu_frame,
          sizeof (av1_obu_frame)), 0);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_rtpav1depay_ignored)
{
  GstHarness *h = gst_harness_new ("rtpav1depay");
  GstBuffer *in_buf;
  GstFlowReturn ret;
  gst_harness_set_src_caps_str (h,
      "application/x-rtp,media=video,payload=(int)96,clock-rate=90000,encoding-name=AV1");

  in_buf = gst_buffer_new_and_alloc (sizeof (rtp_av1_obu_temp_delim));
  fail_unless_equals_int (gst_buffer_fill (in_buf, 0, rtp_av1_obu_temp_delim,
          sizeof (rtp_av1_obu_temp_delim)), sizeof (rtp_av1_obu_temp_delim));

  ret = gst_harness_push (h, in_buf);
  fail_unless_equals_int (ret, GST_FLOW_OK);
  fail_unless_equals_int (gst_harness_buffers_in_queue (h), 0);

  gst_harness_teardown (h);
}

GST_END_TEST;

static Suite *
rtpav1_suite (void)
{
  GST_DEBUG_CATEGORY_INIT (rtpav1test_debug, "rtpav1test", 0,
      "AV1 RTP Payloader");

  Suite *s = suite_create ("rtpav1_test");
  TCase *tc_chain;

  tc_chain = tcase_create ("rtpav1pay");
  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_rtpav1pay_agg_none);
  tcase_add_test (tc_chain, test_rtpav1pay_agg_tu);
  tcase_add_test (tc_chain, test_rtpav1pay_agg_none_frag);
  tcase_add_test (tc_chain, test_rtpav1pay_ignored);

  tc_chain = tcase_create ("rtpav1depay");
  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_rtpav1depay_agg_none);
  tcase_add_test (tc_chain, test_rtpav1depay_agg_tu);
  tcase_add_test (tc_chain, test_rtpav1depay_frag_tu);
  tcase_add_test (tc_chain, test_rtpav1depay_ignored);

  return s;
}

GST_CHECK_MAIN (rtpav1);
