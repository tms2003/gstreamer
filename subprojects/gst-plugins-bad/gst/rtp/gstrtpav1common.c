/* GStreamer
 * Copyright (c) 2022 Cisco Systems, Inc. and its affiliates 
 * All rights reserved.
 * Author: 2022 Alistair Hampton <alhampto@cisco.com>
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
#include "gstrtpav1common.h"

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
