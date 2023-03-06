/* GStreamer
 * Copyright 2021-2022 NXP
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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstdashmatroska.h"
#include "stdio.h"

GST_DEBUG_CATEGORY_STATIC (gst_dash_matroska_debug);
#define GST_CAT_DEFAULT gst_dash_matroska_debug

static gboolean initialized = FALSE;

#define INITIALIZE_DEBUG_CATEGORY \
  if (!initialized) { \
  GST_DEBUG_CATEGORY_INIT (gst_dash_matroska_debug, "dashmatroska", 0, \
      "Dash Matroska Format parsing library"); \
    initialized = TRUE; \
  }

/* log definition */
#define GST_MATROSKA_PARSER_DEBUG                  GST_INFO

/* L0: EBML header */
#define GST_EBML_ID_HEADER                         0x1A45DFA3
/* L0: toplevel Segment */
#define GST_MATROSKA_ID_SEGMENT                    0x18538067

/* L1: matroska top-level master IDs, childs of Segment */
#define GST_MATROSKA_ID_SEEKHEAD                   0x114D9B74
#define GST_MATROSKA_ID_SEGMENTINFO                0x1549A966
#define GST_MATROSKA_ID_TRACKS                     0x1654AE6B
#define GST_MATROSKA_ID_CUES                       0x1C53BB6B
#define GST_MATROSKA_ID_CLUSTER                    0x1F43B675
#define GST_MATROSKA_ID_TAGS                       0x1254C367
#define GST_MATROSKA_ID_ATTACHMENTS                0x1941A469
#define GST_MATROSKA_ID_CHAPTERS                   0x1043A770

/* L2: time scale, child of segment information  */
#define GST_MATROSKA_ID_TIMECODESCALE              0x2AD7B1
#define GST_MATROSKA_ID_DURATION                   0x4489
/* L2: cue point, child of cues */
#define GST_MATROSKA_ID_POINTENTRY                 0xBB
/* L3: cue time, child of cue point */
#define GST_MATROSKA_ID_CUETIME                    0xB3
/* L3: cue track position , child of cue point */
#define GST_MATROSKA_ID_CUETRACKPOSITION           0xB7
/* L4: cue track, child of track position */
#define GST_MATROSKA_ID_CUETRACK                   0xF7
/* L4: cue cluster position, child of track position */
#define GST_MATROSKA_ID_CUECLUSTERPOSITION         0xF1
#define GST_MATROSKA_ID_CUEBLOCKNUMBER             0x5378

void
gst_dash_matroska_parser_init (GstDashMatroskaParser * parser)
{
  memset (parser, 0, sizeof (GstDashMatroskaParser));
  parser->status = GST_MATROSKA_PARSER_STATUS_INIT;
}

void
gst_dash_matroska_parser_clear (GstDashMatroskaParser * parser)
{
  if (parser->array) {
    g_array_free (parser->array, TRUE);
  }
  gst_dash_matroska_parser_init (parser);
}

static GstDashMatroskaParserResult
gst_dash_matroska_parser_read_element_length (guint8 * p_buf, guint len,
    guint64 * element_length)
{
  guint8 b = 0;
  guint8 *p8 = p_buf;
  guint64 total = 0;
  guint num_ffs = 0;
  guint mask = 0;
  guint n = 0;

  b = GST_READ_UINT8 (p8);
  p8++;
  mask = (1 << (8 - len)) - 1;
  b &= mask;
  if (G_UNLIKELY (b == mask))
    num_ffs++;

  total = (guint64) b;
  n = len;
  while (--n) {
    b = GST_READ_UINT8 (p8);
    p8++;
    if (G_UNLIKELY (b == 0xff))
      num_ffs++;

    total = (total << 8) | b;
  }

  /* check the unknown length */
  if (G_UNLIKELY (len == num_ffs)) {
    *element_length = G_MAXUINT64;
    return GST_MATROSKA_PARSER_ERROR;
  }

  *element_length = total;
  return GST_MATROSKA_PARSER_OK;
}

static GstDashMatroskaParserResult
gst_dash_matroska_parser_read_ebml_info (GstDashMatroskaParser * parser,
    guint8 * buf, guint64 size, GstDashMatroskaEbmlInfo * ebml_info,
    guint64 * consume)
{
  guint i = 0;
  guint len = 0;
  guint64 bytes = 0;
  guint64 rem_size = 0;
  guint8 *p8 = NULL;
  guint64 data = 0;
  GstDashMatroskaParserResult res = GST_MATROSKA_PARSER_OK;

  if (!size) {
    return GST_MATROSKA_PARSER_INSUFFICIENT_DATA;
  }

  /* initialize the data structure */
  memset (ebml_info, 0, sizeof (GstDashMatroskaEbmlInfo));
  *consume = 0;
  rem_size = size;
  p8 = buf;

  /* read EBML ID */
  for (i = 0; i < 8; i++) {
    if ((GST_READ_UINT8 (p8) & (0x80 >> i)) != 0)
      break;
  }
  len = i + 1;
  if (G_UNLIKELY (len > 4)) {
    GST_ERROR ("Invalid EBML ID size %d at position %ld", len, parser->consume);
    return GST_MATROSKA_PARSER_ERROR;
  }

  if (rem_size <= len) {
    return GST_MATROSKA_PARSER_INSUFFICIENT_DATA;
  } else {
    i = 0;
    while (i < len) {
      data <<= 8;
      data |= GST_READ_UINT8 (p8 + i);
      i++;
    }
    ebml_info->id = (guint32) data;

    p8 += len;
    bytes += len;
    rem_size -= len;
  }

  /* read EBML length */
  for (i = 0; i < 8; i++) {
    if ((GST_READ_UINT8 (p8) & (0x80 >> i)) != 0)
      break;
  }
  len = i + 1;
  if (G_UNLIKELY (len > 8)) {
    GST_ERROR ("Invalid EBML length size %d at position %ld",
        len, parser->consume);
    return GST_MATROSKA_PARSER_ERROR;
  }

  if (rem_size <= len) {
    return GST_MATROSKA_PARSER_INSUFFICIENT_DATA;
  } else {
    res =
        gst_dash_matroska_parser_read_element_length (p8, len,
        &ebml_info->size);
    if (res != GST_MATROSKA_PARSER_OK) {
      GST_ERROR ("read ebml, unknown length");
      return res;
    }
    p8 += len;
    bytes += len;
    rem_size -= len;

    /* store EBML data field information */
    ebml_info->data_offset = bytes;
    bytes += ebml_info->size;
    *consume = bytes;

    /* check EBML data field size */
    if (rem_size < ebml_info->size) {
      return GST_MATROSKA_PARSER_INSUFFICIENT_DATA;
    } else {
      /* store the first address of EBML data field */
      ebml_info->data_buf = p8;
    }
  }

  return res;
}

static GstDashMatroskaParserResult
gst_dash_matroska_parser_read_uint (GstDashMatroskaParser * parser,
    GstDashMatroskaEbmlInfo * ebml_info, guint64 * data)
{
  guint64 value = 0;
  guint8 *p8 = ebml_info->data_buf;
  guint len = ebml_info->size;

  if (len > 8) {
    GST_ERROR ("Invalid integer element size %d at position %ld",
        len, parser->consume);
    return GST_MATROSKA_PARSER_ERROR;
  }

  while (len--) {
    value <<= 8;
    value |= GST_READ_UINT8 (p8);
    p8++;
  }

  *data = value;
  return GST_MATROSKA_PARSER_OK;
}

static GstDashMatroskaParserResult
gst_dash_matroska_parser_check_ebml_id (GstDashMatroskaParser * parser,
    guint32 id, guint32 len, GstBuffer * buffer)
{
  GstDashMatroskaParserResult res = GST_MATROSKA_PARSER_OK;
  guint32 i = 0;
  guint32 data = 0;
  GstMapInfo map;

  g_return_val_if_fail ((len > 0 && len <= 4), GST_MATROSKA_PARSER_ERROR_PARAM);

  /* check buffer length */
  gst_buffer_map (buffer, &map, GST_MAP_READ);

  if (map.size < len) {
    GST_MATROSKA_PARSER_DEBUG ("insufficient data, len %ld at position %ld",
        map.size, parser->consume);
    return GST_MATROSKA_PARSER_INSUFFICIENT_DATA;
  }

  /* check EBML ID */
  i = 0;
  while (i < len) {
    data <<= 8;
    data |= GST_READ_UINT8 (map.data + i);
    i++;
  }
  gst_buffer_unmap (buffer, &map);

  if (data != id) {
    res = GST_MATROSKA_PARSER_NOT_SUPPORTED;
  }
  return res;
}

#define SWITCH_TO_PARSE_SUB_EBML(ebml_total_size, size_of_ebml_id_length) \
  do { \
    ebml_total_size = size_of_ebml_id_length; \
  } while(0)

static GstDashMatroskaParserResult
gst_dash_matroska_parser_extract_data (GstDashMatroskaParser * parser,
    GstBuffer * buffer)
{
  GstMapInfo map;
  guint8 *p8 = 0;
  guint64 len = 0;
  GstDashMatroskaEbmlInfo ebml_info;
  guint64 consume = 0;
  GstDashMatroskaParserResult res = GST_MATROSKA_PARSER_OK;

  INITIALIZE_DEBUG_CATEGORY;
  /* check buffer length */
  gst_buffer_map (buffer, &map, GST_MAP_READ);
  p8 = map.data;
  len = map.size;

  while (len) {
    /* 1. read one ebml */
    res =
        gst_dash_matroska_parser_read_ebml_info (parser, p8 + parser->offset,
        len, &ebml_info, &consume);
    if (res == GST_MATROSKA_PARSER_ERROR) {
      break;
    } else if (res == GST_MATROSKA_PARSER_INSUFFICIENT_DATA) {
      /* adjust to parse sub element information if get 
       * EBML ID and length field information. For the blow 
       * EBML type, don't wait for all the data to be received. */
      if (consume) {
        if ((ebml_info.id == GST_MATROSKA_ID_SEGMENT) ||
            (ebml_info.id == GST_MATROSKA_ID_CUES) ||
            (ebml_info.id == GST_MATROSKA_ID_CLUSTER)) {
          /* Attention: For GST_MATROSKA_ID_SEGMENT type ebml,
           * Not all data is in header fragment, some data is 
           * in other fragment. So need parse sub element as 
           * soon as possible, otherwise the data will be cleared
           * in dashdemux2. */
          if (consume > ebml_info.size) {
            /* go to parse sub ebml */
            consume -= ebml_info.size;
            res = GST_MATROSKA_PARSER_OK;
          }
        }
      }

      /* check integrity and return if not */
      if (res == GST_MATROSKA_PARSER_INSUFFICIENT_DATA) {
        GST_MATROSKA_PARSER_DEBUG
            ("Incomplete EBML in adapter, adapter offset %ld and no paresd length %ld, stream position %ld",
            parser->offset, len, parser->consume);
        break;
      }
    }

    /*2. handle ebml information */
    switch (ebml_info.id) {
      case GST_EBML_ID_HEADER:
      {
        /* In some cases, we may receive header data repeatly
         * and sholud reset it and start parsing again */
        gst_dash_matroska_parser_clear (parser);
        parser->status = GST_MATROSKA_PARSER_STATUS_DATA;
        GST_MATROSKA_PARSER_DEBUG ("EBML ID header at position %ld, size %ld",
            parser->consume, ebml_info.size);
        break;
      }
      case GST_MATROSKA_ID_SEGMENT:
      {
        parser->segment_offset = parser->consume;
        /* need to parse childs ID */
        SWITCH_TO_PARSE_SUB_EBML (consume, ebml_info.data_offset);
        GST_MATROSKA_PARSER_DEBUG ("EBML ID segment at position %ld, size %ld",
            parser->consume, ebml_info.size);
        break;
      }
      case GST_MATROSKA_ID_SEEKHEAD:
      {
        parser->segment_head_offset = parser->consume;
        GST_MATROSKA_PARSER_DEBUG
            ("EBML ID segment seek head at position %ld, offset %ld, size %ld",
            parser->consume, parser->segment_head_offset, ebml_info.size);
        break;
      }
      case GST_MATROSKA_ID_SEGMENTINFO:
      {
        /* need to parse childs ID */
        SWITCH_TO_PARSE_SUB_EBML (consume, ebml_info.data_offset);
        GST_MATROSKA_PARSER_DEBUG
            ("EBML ID segment information at position %ld, size %ld",
            parser->consume, ebml_info.size);
        break;
      }
      case GST_MATROSKA_ID_TIMECODESCALE:
      {
        res =
            gst_dash_matroska_parser_read_uint (parser, &ebml_info,
            &parser->time_scale);
        if (res != GST_MATROSKA_PARSER_OK) {
          goto done;
        }
        break;
      }
      case GST_MATROSKA_ID_DURATION:
      {
        res =
            gst_dash_matroska_parser_read_uint (parser, &ebml_info,
            &parser->duration);
        if (res != GST_MATROSKA_PARSER_OK) {
          goto done;
        }
        break;
      }
      case GST_MATROSKA_ID_CUES:
      {
        /* need to parse childs ID */
        SWITCH_TO_PARSE_SUB_EBML (consume, ebml_info.data_offset);
        parser->need_parse_length = parser->consume + consume + ebml_info.size;
        GST_MATROSKA_PARSER_DEBUG
            ("EBML ID cues at position %ld, need parse length %ld",
            parser->consume, parser->need_parse_length);
        break;
      }
      case GST_MATROSKA_ID_POINTENTRY:
      {
        if (!parser->array) {
          parser->array =
              g_array_new (FALSE, TRUE, sizeof (GstDashMatroskaPointData));
        }
        GstDashMatroskaPointData point_data;
        memset (&point_data, 0, sizeof (GstDashMatroskaPointData));
        g_array_append_val (parser->array, point_data);
        parser->cue_point_num++;
        /* need to parse childs ID */
        SWITCH_TO_PARSE_SUB_EBML (consume, ebml_info.data_offset);
        GST_MATROSKA_PARSER_DEBUG
            ("EBML ID cue point at position %ld, num %ld, size %ld",
            parser->consume, parser->cue_point_num, ebml_info.size);
        break;
      }
      case GST_MATROSKA_ID_CUETIME:
      {
        /* get current cue information to fill array list */
        GstDashMatroskaPointData *entry = NULL;
        if (parser->array && parser->cue_point_num) {
          entry = &g_array_index (parser->array,
              GstDashMatroskaPointData, (parser->cue_point_num - 1));
          if (entry) {
            res =
                gst_dash_matroska_parser_read_uint (parser, &ebml_info,
                &entry->cue_time);
            if (res != GST_MATROSKA_PARSER_OK) {
              goto done;
            }
          }
        }

        break;
      }
      case GST_MATROSKA_ID_CUETRACKPOSITION:
      {
        /* need to parse childs ID */
        SWITCH_TO_PARSE_SUB_EBML (consume, ebml_info.data_offset);
        GST_MATROSKA_PARSER_DEBUG
            ("EBML ID track position at position %ld, num %ld, size %ld",
            parser->consume, parser->cue_point_num, ebml_info.size);
        break;
      }
      case GST_MATROSKA_ID_CUECLUSTERPOSITION:
      {
        /* get current cue information to fill array list */
        GstDashMatroskaPointData *entry = NULL;
        if (parser->array && parser->cue_point_num) {
          entry = &g_array_index (parser->array,
              GstDashMatroskaPointData, (parser->cue_point_num - 1));
          if (entry) {
            res =
                gst_dash_matroska_parser_read_uint (parser, &ebml_info,
                &entry->track_pos.cluster_pos);
            if (res != GST_MATROSKA_PARSER_OK) {
              goto done;
            }
          }
        }

        break;
      }
      case GST_MATROSKA_ID_CUETRACK:
      {
        /* get current cue information to fill array list */
        GstDashMatroskaPointData *entry = NULL;
        if (parser->array && parser->cue_point_num) {
          entry = &g_array_index (parser->array,
              GstDashMatroskaPointData, (parser->cue_point_num - 1));
          if (entry) {
            res =
                gst_dash_matroska_parser_read_uint (parser, &ebml_info,
                &entry->track_pos.track);
            if (res != GST_MATROSKA_PARSER_OK) {
              goto done;
            }
          }
        }
        break;
      }
      case GST_MATROSKA_ID_CLUSTER:
      {
        /* If cues information is incomplete, return here */
        GST_MATROSKA_PARSER_DEBUG ("EBML ID cluster at position %ld",
            parser->consume);
        res = GST_MATROSKA_PARSER_DONE;
        goto done;
      }
      default:
      {
        GST_MATROSKA_PARSER_DEBUG
            ("unhandled EBML ID 0x%x at position %ld, data field size %ld ",
            ebml_info.id, parser->consume, ebml_info.size);
        break;
      }
    }

    /* 3. update parsing parameters */
    /* update the parsed length of current buffer */
    parser->offset += consume;
    /* update the total consume length of the stream */
    parser->consume += consume;
    /* update unparsed length of current buffer */
    len -= consume;

    /* 4. check if parser can exit. 
     * It can stop parsing if get all cues data */
    if (parser->need_parse_length &&
        parser->consume >= parser->need_parse_length) {
      GST_MATROSKA_PARSER_DEBUG
          ("get all cues data, current buffer offset %ld, total parsed length %ld",
          parser->offset, parser->consume);
      res = GST_MATROSKA_PARSER_DONE;
      break;
    }
  }

done:
  gst_buffer_unmap (buffer, &map);
  return res;
}

GstDashMatroskaParserResult
gst_dash_matroska_parser_parse (GstDashMatroskaParser * parser,
    GstBuffer * buffer)
{
  GstDashMatroskaParserResult res = GST_MATROSKA_PARSER_OK;

  INITIALIZE_DEBUG_CATEGORY;
  g_return_val_if_fail (parser != NULL, GST_MATROSKA_PARSER_ERROR_PARAM);
  g_return_val_if_fail (buffer != NULL, GST_MATROSKA_PARSER_ERROR_PARAM);

  switch (parser->status) {
    case GST_MATROSKA_PARSER_STATUS_INIT:
    {
      gst_dash_matroska_parser_init (parser);
      parser->status = GST_MATROSKA_PARSER_STATUS_HEADER;
    }
    case GST_MATROSKA_PARSER_STATUS_HEADER:
    {
      res =
          gst_dash_matroska_parser_check_ebml_id (parser, GST_EBML_ID_HEADER, 4,
          buffer);
      if (res != GST_MATROSKA_PARSER_OK) {
        return res;
      }
      parser->status = GST_MATROSKA_PARSER_STATUS_DATA;
    }
    case GST_MATROSKA_PARSER_STATUS_DATA:
    {
      res = gst_dash_matroska_parser_extract_data (parser, buffer);
      if (res != GST_MATROSKA_PARSER_DONE) {
        if (res == GST_MATROSKA_PARSER_ERROR) {
          GST_ERROR ("detect invalid information at position %ld, clear parser",
              parser->consume);
          gst_dash_matroska_parser_clear (parser);
          parser->status = GST_MATROSKA_PARSER_STATUS_FINISHED;
        }
        return res;
      }
      parser->status = GST_MATROSKA_PARSER_STATUS_FINISHED;
    }
    case GST_MATROSKA_PARSER_STATUS_FINISHED:
      break;
    default:
      break;
  }
  return res;
}
