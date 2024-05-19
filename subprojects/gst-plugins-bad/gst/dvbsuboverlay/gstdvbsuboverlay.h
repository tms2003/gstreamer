/* GStreamer DVB subtitles overlay
 * Copyright (c) 2010 Mart Raudsepp <mart.raudsepp@collabora.co.uk>
 * Copyright (c) 2010 ONELAN Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __GST_DVBSUB_OVERLAY_H__
#define __GST_DVBSUB_OVERLAY_H__

#define GST_USE_UNSTABLE_API

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/video-overlay-composition.h>
#include <gst/video/gstsuboverlay.h>

#include "dvb-sub.h"

G_BEGIN_DECLS

#define GST_TYPE_DVBSUB_OVERLAY (gst_dvbsub_overlay_get_type())
G_DECLARE_FINAL_TYPE (GstDVBSubOverlay, gst_dvbsub_overlay, GST,
    DVBSUB_OVERLAY, GstSubOverlay)

struct _GstDVBSubOverlay
{
  GstSubOverlay suboverlay;

  /* properties */
  gboolean enable;
  gint max_page_timeout;
  gboolean force_end;

  /* <private> */
  DVBSubtitles *current_subtitle; /* The currently active set of subtitle regions, if any */
  GQueue *pending_subtitles; /* A queue of raw subtitle region sets with
			      * metadata that are waiting their running time */

  DvbSub *dvb_sub;

  /* subtitle data submitted to dvb_sub but no sub received yet */
  gboolean pending_sub;
  /* last text pts */
  GstClockTime last_text_pts;
};

GST_ELEMENT_REGISTER_DECLARE (dvbsuboverlay);

G_END_DECLS

#endif
