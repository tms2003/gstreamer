/*
 * GStreamer AVTP Plugin
 * Copyright (C) 2019 Intel Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later
 * version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA
 */

#include "gstavtputil.h"

/* Helper function to convert AVTP timestamp to AVTP presentation time. Since
 * AVTP timestamp represents the lower 32-bit part from AVTP presentation time,
 * the helper requires a reference time ('ref' argument) to convert it properly.
 * The reference time must be in gstreamer clock-time coordinate.
 */
GstClockTime
gst_avtp_tstamp_to_ptime (GstElement * element, guint32 tstamp,
    GstClockTime ref)
{
  GstClockTime ptime;

  ptime = (ref & 0xFFFFFFFF00000000ULL) | tstamp;

  /* If 'ptime' is less than our reference time, it means the higher part
   * from 'ptime' needs to be incremented by 1 in order reflect the correct
   * presentation time.
   */
  if (ptime < ref)
    ptime += (1ULL << 32);

  GST_LOG_OBJECT (element, "AVTP presentation time %" GST_TIME_FORMAT,
      GST_TIME_ARGS (ptime));
  return ptime;
}
