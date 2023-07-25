/* GStreamer
 * Copyright (C) 2008-2009 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

/* Implementation of SMPTE 381M - Mapping MPEG streams into the MXF
 * Generic Container
 */

#ifndef __MXF_MPEG_H__
#define __MXF_MPEG_H__

#include <gst/gst.h>
#include "mxfessence.h"

typedef enum
{
  MXF_MPEG_ESSENCE_TYPE_OTHER = 0,
  MXF_MPEG_ESSENCE_TYPE_VIDEO_MPEG2,
  MXF_MPEG_ESSENCE_TYPE_VIDEO_MPEG4,
  MXF_MPEG_ESSENCE_TYPE_VIDEO_AVC
} MXFMPEGEssenceType;

/**
 * MXFMPEGVideoMappingData:
 * @essence_type: Type of essence for the essence track.
 * @closed_group: Whether current group is closed.
 * @only_b_picts: Whether only B-pictures were encountered since last I-frame.
 * @pict_seq_nb: The sequence number of current pict. NONE is G_MAXUINT64;
 *
 * The MXF Mpeg Mapping Data structure stores context data which is used
 * while parsing mpeg packets of the essence track.
 */
struct _MXFMPEGVideoMappingData
{
  MXFMPEGEssenceType essence_type;
  gboolean closed_group;
  gboolean only_b_picts;
  gpointer parser_context;
};

typedef struct _MXFMPEGVideoMappingData MXFMPEGVideoMappingData;

void mxf_mpeg_init (void);

GstFlowReturn mxf_mpeg_parse_mpeg2_pict_props (GstBuffer * buffer,
    MXFMPEGVideoMappingData * mapping_data,
    MXFEssenceElementParsedProperties * props);

GstFlowReturn mxf_mpeg_parse_mpeg4_pict_props (GstBuffer * buffer,
    MXFMPEGVideoMappingData * mapping_data,
    MXFEssenceElementParsedProperties * props);

#endif /* __MXF_MPEG_H__ */
