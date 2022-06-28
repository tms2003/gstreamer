/* GStreamer
 * Copyright (C) 2022 GStreamer developers
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

#ifndef __GST_INDEX_H__
#define __GST_INDEX_H__

#include <glib-object.h>
#include <gst/gstconfig.h>
#include <gst/gstclock.h>
#include <gst/gststructure.h>
#include <gst/gstformat.h>

G_BEGIN_DECLS

#define GST_TYPE_INDEX               (gst_index_get_type())
#define GST_INDEX(obj)               (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_INDEX, GstIndex))
#define GST_IS_INDEX(obj)            (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_INDEX))
#define GST_INDEX_GET_IFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), GST_TYPE_INDEX, GstIndexInterface))

/**
 * GstIndex:
 *
 * Opaque #GstIndex data structure.
 *
 * Since: 1.22
 */
typedef struct _GstIndex GstIndex; /* dummy object */
typedef struct _GstIndexInterface GstIndexInterface;

/**
 * GstIndexLookupMethod:
 * @GST_INDEX_LOOKUP_METHOD_EXACT: There has to be an exact indexentry with the given format/value
 * @GST_INDEX_LOOKUP_METHOD_BEFORE: The exact entry or the one before it
 * @GST_INDEX_LOOKUP_METHOD_AFTER: The exact entry or the one after it
 *
 * Specify the method to find an index entry in the index.
 *
 * Since: 1.22
 */
typedef enum {
  GST_INDEX_LOOKUP_METHOD_EXACT,
  GST_INDEX_LOOKUP_METHOD_BEFORE,
  GST_INDEX_LOOKUP_METHOD_AFTER
} GstIndexLookupMethod;

/**
 * GstIndexUnitType:
 * @GST_INDEX_UNIT_TYPE_NONE: no specific type
 * @GST_INDEX_UNIT_TYPE_SYNC_POINT: Marks a sync point, a sync point is one
 *   where one can randomly seek to.
 *
 * Potential unit types.
 *
 * Since: 1.22
 */
typedef enum {
  GST_INDEX_UNIT_TYPE_NONE,
  GST_INDEX_UNIT_TYPE_SYNC_POINT,
} GstIndexUnitType;

/**
 * GstIndexLookupFlags:
 * @GST_INDEX_LOOKUP_FLAG_NONE: no extra flags
 * @GST_INDEX_LOOKUP_FLAG_CONTIGUOUS: only search for a unit within contiguous
 *  scanned regions. When the requested position is not within a previously scanned
 *  region, lookup will not return anything if that flag is set, as well as when the
 *  candidate unit is in a separate region.
 *
 * Flags for a unit lookup.
 *
 * Since: 1.22
 */
typedef enum {
  GST_INDEX_LOOKUP_FLAG_NONE       = 0,
  GST_INDEX_LOOKUP_FLAG_CONTIGUOUS   = (1 << 0),

  /* new flags should start here */
  GST_INDEX_LOOKUP_FLAG_LAST     = (1 << 8)
} GstIndexLookupFlags;

/**
 * GST_INDEX_OFFSET_NONE:
 *
 * Represents an invalid / unknown offset.
 *
 * Since: 1.22
 */
#define GST_INDEX_OFFSET_NONE  ((guint64)-1)

/**
 * GST_INDEX_OFFSET_IS_VALID:
 *
 * Check whether an index offset is valid.
 *
 * Since: 1.22
 */
#define GST_INDEX_OFFSET_IS_VALID(off)      (off != GST_BUFFER_OFFSET_NONE)

/**
 * GstIndexInterface:
 * @parent: parent interface type.
 *
 * #GstIndex interface.
 *
 * Since: 1.22
 */
struct _GstIndexInterface
{
  GTypeInterface parent;

  /**
   * GstIndexInterface::add_unit:
   * @index: The index interface
   * @stream_id: Identifier for the indexed stream
   * @type: What the unit represents, when well-defined
   * @stream_time: The timestamp associated with @offset
   * @offset: The byte offset associated with @stream_time
   * @contiguous: Whether the unit extends the previously scanned range
   * @extra: (nullable) (transfer full): Extra metadata associated with the unit
   *
   * Adding indexed units.
   *
   * Returns: %TRUE if the unit was indeed added, %FALSE otherwise
   */
  gboolean (*add_unit) (GstIndex *index,
                               const gchar *stream_id,
                               GstIndexUnitType type,
                               GstClockTime stream_time,
                               guint64 offset,
                               gboolean contiguous,
                               GstStructure *extra);

  /**
   * GstIndexInterface::lookup_unit_time:
   * @index: the index interface
   * @stream_id: identifier for the indexed stream
   * @method: When no exact match is found, how further look up should be performed
   * @type: Type of the unit to look up
   * @flags: Extra lookup flags
   * @target_stream_time: The desired stream time
   * @stream_time: (out): The stream time of the looked up unit when successful
   * @offset: (out): The offset in bytes of the looked up unit when successful
   * @extra: (out) (transfer none) (nullable): The extra metadata structure associated to
   *   the looked up unit when successful. The structure remains valid for the duration of
   *   the index' lifecycle.
   *
   * Search for an existing unit in the index, according to a target stream time and unit type.
   * Non-exact matches can be requested with @method.
   *
   * Returns: %TRUE when a matching unit was looked up.
   */
  gboolean (* lookup_unit_time) (GstIndex *index,
                            const gchar *stream_id,
                            GstIndexLookupMethod method,
                            GstIndexUnitType type,
                            GstIndexLookupFlags flags,
                            GstClockTime target_stream_time,
                            GstClockTime *stream_time,
                            guint64 *offset,
                            GstStructure **extra);

  /**
   * GstIndexInterface::lookup_unit_offset:
   * @index: the index interface
   * @stream_id: identifier for the indexed stream
   * @method: When no exact match is found, how further look up should be performed
   * @type: Type of the unit to look up
   * @flags: Extra lookup flags
   * @target_offset: The desired stream time
   * @stream_time: (out): The stream time of the looked up unit when successful
   * @offset: (out): The offset in bytes of the looked up unit when successful
   * @extra: (out) (transfer none) (nullable): The extra metadata structure associated to
   *   the looked up unit when successful. The structure remains valid for the duration of
   *   the index' lifecycle.
   *
   * Search for an existing unit in the index, according to a target offset and unit type.
   * Non-exact matches can be requested with @method.
   *
   * Returns: %TRUE when a matching unit was looked up.
   */
  gboolean (* lookup_unit_offset) (GstIndex *index,
                              const gchar *stream_id,
                              GstIndexLookupMethod method,
                              GstIndexUnitType type,
                              GstIndexLookupFlags flags,
                              guint64 target_offset,
                              GstClockTime *stream_time,
                              guint64 *offset,
                              GstStructure **extra);

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING_LARGE];
};

GST_API
GType        gst_index_get_type (void);

GST_API
gboolean     gst_index_add_unit (GstIndex *index,
                                         const gchar *stream_id,
                                         GstIndexUnitType type,
                                         GstClockTime stream_time,
                                         guint64 offset,
                                         gboolean contiguous,
                                         GstStructure *extra);

GST_API
gboolean gst_index_lookup_unit_time (GstIndex *index,
                            const gchar *stream_id,
                            GstIndexLookupMethod method,
                            GstIndexUnitType type,
                            GstIndexLookupFlags flags,
                            GstClockTime target_stream_time,
                            GstClockTime *stream_time,
                            guint64 *offset,
                            GstStructure **extra);

GST_API
gboolean gst_index_lookup_unit_offset (GstIndex *index,
                            const gchar *stream_id,
                            GstIndexLookupMethod method,
                            GstIndexUnitType type,
                            GstIndexLookupFlags flags,
                            guint64 target_offset,
                            GstClockTime *stream_time,
                            guint64 *offset,
                            GstStructure **extra);

G_END_DECLS

#endif /* __GST_INDEX_H__ */
