/* GStreamer
 * Copyright (C) 2023 Michael Grzeschik <m.grzeschik@pengutronix.de>
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

#pragma once

#include <gst/codecs/codecs-prelude.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

typedef enum
{
  GST_RC_CONSTANT_QP,
  GST_RC_CONSTANT_BITRATE,
} GstRateControlMode;

typedef enum
{
  GST_RC_KEY_FRAME,
  GST_RC_INTER_FRAME,
} GstRcFrameType;

GType gst_rate_control_mode_get_type (void);

#define GST_RATE_CONTROLLER_TYPE (gst_rc_get_type())
G_DECLARE_FINAL_TYPE(GstRateController, gst_rc,
        GST, RC, GObject);

GstRateController*  gst_rc_new (void);

void                gst_rc_record (GstRateController * self,
                                   GstRcFrameType frame_type,
                                   gsize coded_size,
                                   GstClockTime duration);

gint                gst_rc_get_max_qp (GstRateController *self);
gint                gst_rc_get_min_qp (GstRateController *self);
gint                gst_rc_get_qp_step (GstRateController *self);
gint                gst_rc_get_init_qp (GstRateController *self);
GstRateControlMode  gst_rc_get_mode (GstRateController *self);
gint                gst_rc_get_bitrate (GstRateController *self);

void                gst_rc_set_format (GstRateController *self,
                                       const GstVideoInfo * vinfo);
void                gst_rc_set_max_qp (GstRateController *self, gint max_qp);
void                gst_rc_set_min_qp (GstRateController *self, gint min_qp);
void                gst_rc_set_qp_step (GstRateController *self, gint qp_step);
void                gst_rc_set_init_qp (GstRateController *self, gint init_qp);
void                gst_rc_set_mode (GstRateController *self, GstRateControlMode mode);
void                gst_rc_set_bitrate (GstRateController *self, gint bitrate);

gint                gst_rc_get_qp(GstRateController *self);

G_END_DECLS
