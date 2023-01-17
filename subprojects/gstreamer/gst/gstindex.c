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

/**
 * SECTION:gstindex
 * @title: GstIndex
 * @short_description: Interface for indexing objects
 *
 * An index has two intended user classes:
 *
 * - Elements wishing to store position information regarding units of data
 *   in the format they process and retrieve it later, for instance in order
 *   to perform a seek.
 *
 * - Applications wishing to access that information, for instance in order
 *   to persist it.
 *
 * Units can refer to any type of data, but some generic types are defined,
 * see #GstIndexUnitType.
 *
 * Units always relate to a given stream id, which means persisted indices
 * are not guaranteed to stay valid across GStreamer versions.
 *
 * Index entries are immutable: no API is exposed to modify or remove them.
 *
 * A default, in-memory version exists: #GstMemIndex.
 *
 * Example applications are available in gst-examples/indexing .
 *
 * Since: 1.22
 */

#include "gst_private.h"

#include "gstindex.h"

G_DEFINE_INTERFACE (GstIndex, gst_index, G_TYPE_OBJECT);

static void
gst_index_default_init (GstIndexInterface * klass)
{
  /* nothing to do here, it's a dummy interface */
}

/**
 * gst_index_add_unit:
 * @index: The index
 * @stream_id: Identifier for the indexed stream
 * @type: What the unit represents, when well-defined
 * @stream_time: The timestamp associated with @offset
 * @offset: The byte offset associated with @stream_time
 * @contiguous: Whether the unit extends the previously scanned range
 * @extra: (nullable) (transfer full): Extra metadata associated with the unit
 *
 * Add a unit to the index.
 *
 * Returns: %TRUE if the unit was indeed added, %FALSE otherwise
 */
gboolean
gst_index_add_unit (GstIndex * index,
    const gchar * stream_id,
    GstIndexUnitType type,
    GstClockTime stream_time, guint64 offset, gboolean contiguous,
    GstStructure * extra)
{
  GstIndexInterface *iface;

  g_return_val_if_fail (GST_IS_INDEX (index), FALSE);
  g_return_val_if_fail (stream_id != NULL, FALSE);
  g_return_val_if_fail (GST_CLOCK_TIME_IS_VALID (stream_time)
      || GST_INDEX_OFFSET_IS_VALID (offset), FALSE);

  iface = GST_INDEX_GET_IFACE (index);

  if (iface->add_unit) {
    return iface->add_unit (index, stream_id, type, stream_time, offset,
        contiguous, extra);
  } else {
    return FALSE;
  }
}

/**
 * gst_index_lookup_unit_time:
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
gboolean
gst_index_lookup_unit_time (GstIndex * index,
    const gchar * stream_id,
    GstIndexLookupMethod method,
    GstIndexUnitType type,
    GstIndexLookupFlags flags,
    GstClockTime target_stream_time,
    GstClockTime * stream_time, guint64 * offset, GstStructure ** extra)
{
  GstIndexInterface *iface;

  g_return_val_if_fail (GST_IS_INDEX (index), FALSE);
  g_return_val_if_fail (stream_id != NULL, FALSE);

  iface = GST_INDEX_GET_IFACE (index);

  if (iface->lookup_unit_time) {
    return iface->lookup_unit_time (index, stream_id, method, type, flags,
        target_stream_time, stream_time, offset, extra);
  } else {
    return FALSE;
  }
}

/**
 * gst_index_lookup_unit_offset:
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
gboolean
gst_index_lookup_unit_offset (GstIndex * index,
    const gchar * stream_id,
    GstIndexLookupMethod method,
    GstIndexUnitType type,
    GstIndexLookupFlags flags,
    guint64 target_offset,
    GstClockTime * stream_time, guint64 * offset, GstStructure ** extra)
{
  GstIndexInterface *iface;

  g_return_val_if_fail (GST_IS_INDEX (index), FALSE);
  g_return_val_if_fail (stream_id != NULL, FALSE);

  iface = GST_INDEX_GET_IFACE (index);

  if (iface->lookup_unit_offset) {
    return iface->lookup_unit_offset (index, stream_id, method, type,
        flags, target_offset, stream_time, offset, extra);
  } else {
    return FALSE;
  }
}
