/* gstrtpvp8parse.c
 * Copyright (C) 2024 Benjamin Gaignard <benjamin.gaignard@collabora.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <gst/base/gstbitreader.h>
#include "dboolhuff.h"
#include "gstrtpvp8parse.h"

gboolean
gst_rtp_vp8_parse_header (GstBuffer * buffer,
    gsize buffer_size, GstRtpVP8Parse * parsed_header)
{
  GstMapInfo map = GST_MAP_INFO_INIT;
  GstBitReader reader;
  guint8 *data;
  gsize size;
  int i;
  gboolean keyframe;
  guint32 partition0_size;
  guint8 version;
  guint8 tmp8 = 0;
  guint8 partitions;
  guint offset;
  BOOL_DECODER bc;
  guint8 *pdata;

  if (G_UNLIKELY (buffer_size < 3))
    goto error;

  if (!gst_buffer_map (buffer, &map, GST_MAP_READ) || !map.data)
    goto error;

  data = map.data;
  size = map.size;

  gst_bit_reader_init (&reader, data, size);

  parsed_header->is_keyframe = keyframe = ((data[0] & 0x1) == 0);
  version = (data[0] >> 1) & 0x7;
  parsed_header->refresh_golden_frame = FALSE;
  parsed_header->refresh_alternate_frame = FALSE;

  if (G_UNLIKELY (version > 3)) {
    GST_ERROR ("Unknown VP8 version %u", version);
    goto error;
  }

  /* keyframe, version and show_frame use 5 bits */
  partition0_size = data[2] << 11 | data[1] << 3 | (data[0] >> 5);

  /* Include the uncompressed data blob in the first partition */
  offset = keyframe ? 10 : 3;
  partition0_size += offset;

  if (!gst_bit_reader_skip (&reader, 24))
    goto error;

  if (keyframe) {
    /* check start tag: 0x9d 0x01 0x2a */
    if (!gst_bit_reader_get_bits_uint8 (&reader, &tmp8, 8) || tmp8 != 0x9d)
      goto error;

    if (!gst_bit_reader_get_bits_uint8 (&reader, &tmp8, 8) || tmp8 != 0x01)
      goto error;

    if (!gst_bit_reader_get_bits_uint8 (&reader, &tmp8, 8) || tmp8 != 0x2a)
      goto error;

    /* Skip horizontal size code (16 bits) vertical size code (16 bits) */
    if (!gst_bit_reader_skip (&reader, 32))
      goto error;
  }

  offset = keyframe ? 10 : 3;
  vp8dx_start_decode (&bc, data + offset, size - offset);

  if (keyframe) {
    /* color space (1 bit) and clamping type (1 bit) */
    vp8dx_decode_bool (&bc, 0x80);
    vp8dx_decode_bool (&bc, 0x80);
  }

  /* segmentation_enabled */
  if (vp8dx_decode_bool (&bc, 0x80)) {
    guint8 update_mb_segmentation_map = vp8dx_decode_bool (&bc, 0x80);
    guint8 update_segment_feature_data = vp8dx_decode_bool (&bc, 0x80);

    if (update_segment_feature_data) {
      /* skip segment feature mode */
      vp8dx_decode_bool (&bc, 0x80);

      /* quantizer update */
      for (i = 0; i < 4; i++) {
        /* skip flagged quantizer value (7 bits) and sign (1 bit) */
        if (vp8dx_decode_bool (&bc, 0x80))
          vp8_decode_value (&bc, 8);
      }

      /* loop filter update */
      for (i = 0; i < 4; i++) {
        /* skip flagged lf update value (6 bits) and sign (1 bit) */
        if (vp8dx_decode_bool (&bc, 0x80))
          vp8_decode_value (&bc, 7);
      }
    }

    if (update_mb_segmentation_map) {
      /* segment prob update */
      for (i = 0; i < 3; i++) {
        /* skip flagged segment prob */
        if (vp8dx_decode_bool (&bc, 0x80))
          vp8_decode_value (&bc, 8);
      }
    }
  }

  /* skip filter type (1 bit), loop filter level (6 bits) and
   * sharpness level (3 bits) */
  vp8_decode_value (&bc, 1);
  vp8_decode_value (&bc, 6);
  vp8_decode_value (&bc, 3);

  /* loop_filter_adj_enabled */
  if (vp8dx_decode_bool (&bc, 0x80)) {

    /* delta update */
    if (vp8dx_decode_bool (&bc, 0x80)) {

      for (i = 0; i < 8; i++) {
        /* 8 updates, 1 bit indicate whether there is one and if follow by a
         * 7 bit update */
        if (vp8dx_decode_bool (&bc, 0x80))
          vp8_decode_value (&bc, 7);
      }
    }
  }

  if (vp8dx_bool_error (&bc))
    goto error;

  tmp8 = vp8_decode_value (&bc, 2);

  partitions = 1 << tmp8;

  /* Check if things are still sensible */
  if (partition0_size + (partitions - 1) * 3 >= size)
    goto error;

  /* partition data is right after the mode partition */
  pdata = data + partition0_size;

  /* Set up mapping */
  parsed_header->n_partitions = partitions + 1;
  parsed_header->partition_offset[0] = 0;
  parsed_header->partition_size[0] = partition0_size + (partitions - 1) * 3;

  parsed_header->partition_offset[1] = parsed_header->partition_size[0];
  for (i = 1; i < partitions; i++) {
    guint psize = (pdata[2] << 16 | pdata[1] << 8 | pdata[0]);

    pdata += 3;
    parsed_header->partition_size[i] = psize;
    parsed_header->partition_offset[i + 1] =
        parsed_header->partition_offset[i] + psize;
  }

  /* Check that our partition offsets and sizes don't go outsize the buffer
   * size. */
  if (parsed_header->partition_offset[i] >= size)
    goto error;

  parsed_header->partition_size[i] = size - parsed_header->partition_offset[i];

  parsed_header->partition_offset[i + 1] = size;

  /* Dequantization Indices */
  /* Skip Y ac index */
  vp8_decode_value (&bc, 7);
  /* Skip Y dc delta */
  if (vp8dx_decode_bool (&bc, 0x80))
    vp8_decode_value (&bc, 4);
  /* Skip Y2 dc delta */
  if (vp8dx_decode_bool (&bc, 0x80))
    vp8_decode_value (&bc, 4);
  /* Skip Y2 ac delta */
  if (vp8dx_decode_bool (&bc, 0x80))
    vp8_decode_value (&bc, 4);
  /* Skip uv dc delta */
  if (vp8dx_decode_bool (&bc, 0x80))
    vp8_decode_value (&bc, 4);
  /* Skip uv ac delta */
  if (vp8dx_decode_bool (&bc, 0x80))
    vp8_decode_value (&bc, 4);

  if (!keyframe) {
    parsed_header->refresh_golden_frame = vp8dx_decode_bool (&bc, 0x80);
    parsed_header->refresh_alternate_frame = vp8dx_decode_bool (&bc, 0x80);
  }

  gst_buffer_unmap (buffer, &map);

  return TRUE;

error:
  GST_DEBUG ("Failed to parse frame");
  if (map.memory != NULL) {
    gst_buffer_unmap (buffer, &map);
  }
  return FALSE;
}
