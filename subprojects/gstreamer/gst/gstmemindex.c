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

#include "gst_private.h"
#include "glib-compat-private.h"

#include <gst/gstindex.h>
#include <gstmemindex.h>

/**
 * SECTION:gstmemindex
 * @title: GstMemIndex
 * @short_description: In-memory #GstIndex implementation
 *
 * This index can be used to store entries in memory. It has a
 * logarithmic complexity for insertion and look up.
 *
 * In addition, it exposes serializing / deserializing methods.
 *
 * Since: 1.22
 */

struct _GstMemIndex
{
  GObject object;

  GstMemIndexPrivate *priv;

  gpointer _gst_reserved[GST_PADDING];
};

struct _GstMemIndexClass
{
  GObjectClass parent_class;

  gpointer _gst_reserved[GST_PADDING];
};

struct _GstMemIndexPrivate
{
  GObject parent;

  GMutex lock;
  GHashTable *stream_indices;
};

/* Constraint might be lifted in the future, keep private */
typedef enum
{
  INDEX_TYPE_UNKNOWN,
  INDEX_TYPE_TIME,
  INDEX_TYPE_OFFSET,
  INDEX_TYPE_BOTH,
} IndexType;

typedef struct
{
  GSequence *all_by_time;
  GSequence *all_by_offset;
  GSequence *sync_points_by_time;
  GSequence *sync_points_by_offset;
  GSequence *scanned_ranges_by_time;
  GSequence *scanned_ranges_by_offset;
  GList *entries;
  IndexType type;
} StreamIndex;

typedef struct
{
  GstClockTime stream_time;
  guint64 offset;
  GstIndexUnitType type;
  GstStructure *extra;
} IndexEntry;

typedef struct
{
  /* Inclusive */
  guint64 start;
  /* Inclusive */
  guint64 end;
} ScannedRange;

static IndexEntry *
_new_index_entry (GstClockTime stream_time, guint64 offset,
    GstIndexUnitType type, GstStructure * extra)
{
  IndexEntry *ret = g_new0 (IndexEntry, 1);

  ret->stream_time = stream_time;
  ret->offset = offset;
  ret->type = type;
  ret->extra = extra;

  return ret;
}

static void
_free_index_entry (IndexEntry * entry)
{
  if (entry->extra)
    gst_structure_free (entry->extra);

  g_free (entry);
}

static ScannedRange *
_new_scanned_range (guint64 start, guint64 end)
{
  ScannedRange *ret = g_new0 (ScannedRange, 1);

  ret->start = start;
  ret->end = end;

  return ret;
}

static void
_free_scanned_range (ScannedRange * range)
{
  g_free (range);
}

static gint
_cmp_scanned_range (gconstpointer a, gconstpointer b, gpointer user_data)
{
  ScannedRange *range_a = (ScannedRange *) a;
  ScannedRange *range_b = (ScannedRange *) b;

  g_assert (GST_INDEX_OFFSET_IS_VALID (range_a->start));
  g_assert (GST_INDEX_OFFSET_IS_VALID (range_b->start));

  if (range_a->start < range_b->start)
    return -1;
  else if (range_a->start == range_b->start)
    return 0;
  else
    return 1;
}

static StreamIndex *
_new_stream_index (void)
{
  StreamIndex *ret = g_new0 (StreamIndex, 1);

  ret->all_by_time = g_sequence_new (NULL);
  ret->all_by_offset = g_sequence_new (NULL);
  ret->sync_points_by_time = g_sequence_new (NULL);
  ret->sync_points_by_offset = g_sequence_new (NULL);
  ret->scanned_ranges_by_time =
      g_sequence_new ((GDestroyNotify) _free_scanned_range);
  ret->scanned_ranges_by_offset =
      g_sequence_new ((GDestroyNotify) _free_scanned_range);
  ret->type = INDEX_TYPE_UNKNOWN;

  g_sequence_insert_sorted (ret->scanned_ranges_by_time, _new_scanned_range (0,
          0), _cmp_scanned_range, NULL);
  g_sequence_insert_sorted (ret->scanned_ranges_by_offset,
      _new_scanned_range (0, 0), _cmp_scanned_range, NULL);


  ret->entries = NULL;

  return ret;
}

static void
_free_stream_index (StreamIndex * index)
{
  g_sequence_free (index->all_by_time);
  g_sequence_free (index->all_by_offset);
  g_sequence_free (index->sync_points_by_time);
  g_sequence_free (index->sync_points_by_offset);
  g_sequence_free (index->scanned_ranges_by_time);
  g_sequence_free (index->scanned_ranges_by_offset);
  g_list_free_full (index->entries, (GDestroyNotify) _free_index_entry);
  g_free (index);
}

static StreamIndex *
_ensure_index_unlocked (GstMemIndex * index, const gchar * stream_id)
{
  StreamIndex *ret =
      g_hash_table_lookup (index->priv->stream_indices, stream_id);

  if (!ret) {
    ret = _new_stream_index ();
    g_hash_table_insert (index->priv->stream_indices, g_strdup (stream_id),
        ret);
  }

  return ret;
}

static gint
_cmp_entry_time (gconstpointer a, gconstpointer b, gpointer user_data)
{
  IndexEntry *entry_a = (IndexEntry *) a;
  IndexEntry *entry_b = (IndexEntry *) b;

  g_assert (GST_CLOCK_TIME_IS_VALID (entry_a->stream_time));
  g_assert (GST_CLOCK_TIME_IS_VALID (entry_b->stream_time));

  if (entry_a->stream_time < entry_b->stream_time)
    return -1;
  else if (entry_a->stream_time == entry_b->stream_time)
    return 0;
  else
    return 1;
}

static gint
_cmp_entry_offset (gconstpointer a, gconstpointer b, gpointer user_data)
{
  IndexEntry *entry_a = (IndexEntry *) a;
  IndexEntry *entry_b = (IndexEntry *) b;

  g_assert (GST_INDEX_OFFSET_IS_VALID (entry_a->offset));
  g_assert (GST_INDEX_OFFSET_IS_VALID (entry_b->offset));

  if (entry_a->offset < entry_b->offset)
    return -1;
  else if (entry_a->offset == entry_b->offset)
    return 0;
  else
    return 1;
}



static gboolean
_stream_index_check_type (StreamIndex * stream_index, IndexEntry * entry)
{
  gboolean ret = FALSE;
  IndexType entry_type;

  if (GST_CLOCK_TIME_IS_VALID (entry->stream_time)
      && GST_INDEX_OFFSET_IS_VALID (entry->offset))
    entry_type = INDEX_TYPE_BOTH;
  else if (GST_CLOCK_TIME_IS_VALID (entry->stream_time))
    entry_type = INDEX_TYPE_TIME;
  else if (GST_INDEX_OFFSET_IS_VALID (entry->offset))
    entry_type = INDEX_TYPE_OFFSET;
  else {
    GST_WARNING_OBJECT (entry,
        "Index entries must have either a valid offset or a valid stream time, or both");
    goto done;
  }

  if (stream_index->type == INDEX_TYPE_UNKNOWN) {
    stream_index->type = entry_type;
  } else if (entry_type != stream_index->type) {
    GST_WARNING_OBJECT (entry,
        "New entry type does not match type for existing stream index");
    goto done;
  }

  ret = TRUE;

done:
  return ret;
}

/* Takes ownership of entry */
static gboolean
_stream_index_insert_entry_sorted (StreamIndex * stream_index,
    IndexEntry * entry)
{
  gboolean ret = FALSE;

  if (!_stream_index_check_type (stream_index, entry)) {
    _free_index_entry (entry);
    goto done;
  }

  if (GST_CLOCK_TIME_IS_VALID (entry->stream_time)) {
    g_sequence_insert_sorted (stream_index->all_by_time, entry, _cmp_entry_time,
        NULL);

    switch (entry->type) {
      case GST_INDEX_UNIT_TYPE_SYNC_POINT:
        g_sequence_insert_sorted (stream_index->sync_points_by_time, entry,
            _cmp_entry_time, NULL);
        break;
      default:
        break;
    }
  }

  if (GST_CLOCK_TIME_IS_VALID (entry->offset)) {
    g_sequence_insert_sorted (stream_index->all_by_offset, entry,
        _cmp_entry_offset, NULL);

    switch (entry->type) {
      case GST_INDEX_UNIT_TYPE_SYNC_POINT:
        g_sequence_insert_sorted (stream_index->sync_points_by_offset, entry,
            _cmp_entry_offset, NULL);
        break;
      default:
        break;
    }
  }

  stream_index->entries = g_list_prepend (stream_index->entries, entry);

  ret = TRUE;

done:
  return ret;
}

/* Takes ownership of entry */
static gboolean
_stream_index_append (StreamIndex * stream_index, IndexEntry * entry)
{
  gboolean ret = FALSE;

  if (!_stream_index_check_type (stream_index, entry)) {
    _free_index_entry (entry);
    goto done;
  }

  if (GST_CLOCK_TIME_IS_VALID (entry->stream_time)) {
    g_sequence_append (stream_index->all_by_time, entry);

    switch (entry->type) {
      case GST_INDEX_UNIT_TYPE_SYNC_POINT:
        g_sequence_append (stream_index->sync_points_by_time, entry);
        break;
      default:
        break;
    }
  }

  if (GST_CLOCK_TIME_IS_VALID (entry->offset)) {
    g_sequence_append (stream_index->all_by_offset, entry);

    switch (entry->type) {
      case GST_INDEX_UNIT_TYPE_SYNC_POINT:
        g_sequence_append (stream_index->sync_points_by_offset, entry);
        break;
      default:
        break;
    }
  }

  stream_index->entries = g_list_prepend (stream_index->entries, entry);

  ret = TRUE;

done:
  return ret;
}

# if 0
static void
_print_scanned_range (ScannedRange * range, gpointer user_data)
{
  g_print ("%lu -> %lu\n", range->start, range->end);
}

static void
_print_scanned_ranges (GSequence * scanned_ranges)
{
  g_sequence_foreach (scanned_ranges, (GFunc) _print_scanned_range, NULL);
}
# endif

static gboolean
_scanned_ranges_check_same_range (GSequence * scanned_ranges, guint64 a,
    guint64 b)
{
  gboolean ret = FALSE;
  GSequenceIter *iter;
  ScannedRange *tmp;
  ScannedRange dummy = {
    a,
    GST_INDEX_OFFSET_NONE,
  };

  iter = g_sequence_search (scanned_ranges, &dummy, _cmp_scanned_range, NULL);

  g_assert_false (g_sequence_iter_is_begin (iter));

  tmp = g_sequence_get (g_sequence_iter_prev (iter));

  if (tmp->start <= a && a <= tmp->end) {
    ret = tmp->start <= b && b <= tmp->end;
  }

  return ret;
}

static gboolean
_scanned_ranges_check_inside_range (GSequence * scanned_ranges,
    guint64 value, gboolean strictly)
{
  gboolean ret = FALSE;
  GSequenceIter *iter;
  ScannedRange *tmp;
  ScannedRange dummy = {
    value,
    GST_INDEX_OFFSET_NONE,
  };

  iter = g_sequence_search (scanned_ranges, &dummy, _cmp_scanned_range, NULL);

  g_assert_false (g_sequence_iter_is_begin (iter));

  tmp = g_sequence_get (g_sequence_iter_prev (iter));

  if (strictly)
    ret = tmp->start < value && value <= tmp->end;
  else
    ret = tmp->start <= value && value <= tmp->end;

  return ret;
}

/* Contiguous cases:
 *
 * - Outside of any scanned range: extend previous range (there always is one)
 * - Strictly inside scanned range: do nothing
 * - Exactly equal to scanned range start: extend previous range, merge with next range
 *
 * Non-contiguous cases:
 *
 * - Outside of any scanned range: start new range
 * - Inside scanned range: do nothing, shouldn't happen though
 */

static void
_update_scanned_ranges (GSequence * scanned_ranges, guint64 start,
    gboolean contiguous)
{
  ScannedRange *cur;
  ScannedRange *next = NULL;
  ScannedRange *prev = NULL;
  GSequenceIter *iter;
  ScannedRange dummy = {
    start,
    GST_INDEX_OFFSET_NONE,
  };

  iter = g_sequence_search (scanned_ranges, &dummy, _cmp_scanned_range, NULL);
  g_assert_false (g_sequence_iter_is_begin (iter));

  while (!prev) {
    if (!g_sequence_iter_is_end (iter)) {
      cur = g_sequence_get (iter);

      if (cur->start < start) {
        prev = cur;
        break;
      }
    }

    if (g_sequence_iter_is_begin (iter)) {
      break;
    }

    iter = g_sequence_iter_prev (iter);
  }

  while (!next) {
    if (g_sequence_iter_is_end (iter)) {
      break;
    }

    cur = g_sequence_get (iter);

    if (cur->start >= start) {
      next = cur;
      break;
    }

    iter = g_sequence_iter_next (iter);
  }

  if (prev) {
    if (prev->end < start) {
      if (contiguous) {
        prev->end = start;
      } else {
        prev = _new_scanned_range (start, start);

        g_sequence_insert_sorted (scanned_ranges, prev,
            _cmp_scanned_range, NULL);
      }
    }

    if (next) {
      /* Merge */
      if (prev->end == next->start) {
        prev->end = next->end;
        g_sequence_remove (iter);
      }
    }
  }
}

static gboolean
gst_mem_index_add_unit (GstIndex * index, const gchar * stream_id,
    GstIndexUnitType type,
    GstClockTime stream_time, guint64 offset, gboolean contiguous,
    GstStructure * extra)
{
  GstMemIndex *mindex = GST_MEM_INDEX (index);
  StreamIndex *stream_index;
  IndexEntry *entry;
  gboolean ret = FALSE;

  g_return_val_if_fail (GST_CLOCK_TIME_IS_VALID (stream_time)
      || GST_INDEX_OFFSET_IS_VALID (offset), FALSE);

  g_mutex_lock (&mindex->priv->lock);

  stream_index = _ensure_index_unlocked (mindex, stream_id);

  /* First check scanned ranges without updating anything */
  if (GST_INDEX_OFFSET_IS_VALID (offset)) {
    if (_scanned_ranges_check_inside_range
        (stream_index->scanned_ranges_by_offset, offset, contiguous)) {
      goto done;
    }
  }

  if (GST_CLOCK_TIME_IS_VALID (stream_time)) {
    if (_scanned_ranges_check_inside_range
        (stream_index->scanned_ranges_by_time, stream_time, contiguous)) {
      goto done;
    }
  }

  entry = _new_index_entry (stream_time, offset, type, extra);

  ret = _stream_index_insert_entry_sorted (stream_index, entry);

  /* Finally update scanned ranges if all went well */
  if (ret) {
    if (GST_INDEX_OFFSET_IS_VALID (offset)) {
      _update_scanned_ranges (stream_index->scanned_ranges_by_offset, offset,
          contiguous);
    }

    if (GST_CLOCK_TIME_IS_VALID (stream_time)) {
      _update_scanned_ranges (stream_index->scanned_ranges_by_time, stream_time,
          contiguous);
    }
  }

done:
  g_mutex_unlock (&mindex->priv->lock);
  return ret;
}

static gboolean
gst_mem_index_lookup_unlocked (GstMemIndex * index,
    GSequence * all,
    GSequence * keyframes,
    GstIndexLookupMethod method,
    GstIndexUnitType type,
    guint64 target_value,
    GCompareDataFunc cmp_func,
    GstClockTime * stream_time, guint64 * offset, GstStructure ** extra)
{
  gboolean ret = FALSE;
  IndexEntry dummy = {
    target_value,
    target_value,
    GST_INDEX_UNIT_TYPE_NONE,
    NULL
  };
  GSequence *seq;
  GSequenceIter *iter;
  IndexEntry *target = NULL;

  seq = type == GST_INDEX_UNIT_TYPE_SYNC_POINT ? keyframes : all;

  if (g_sequence_is_empty (seq)) {
    goto done;
  }

  iter = g_sequence_search (seq, &dummy, cmp_func, NULL);

  if (g_sequence_iter_is_begin (iter)) {
    /* Safe, the sequence is not empty */
    IndexEntry *entry = g_sequence_get (iter);

    switch (method) {
      case GST_INDEX_LOOKUP_METHOD_EXACT:
      case GST_INDEX_LOOKUP_METHOD_BEFORE:
        if (cmp_func (entry, &dummy, NULL) == 0)
          target = entry;
        break;
      case GST_INDEX_LOOKUP_METHOD_AFTER:
        target = entry;
        break;
    }
  } else if (g_sequence_iter_is_end (iter)) {
    /* Safe, the sequence is not empty */
    IndexEntry *entry = g_sequence_get (g_sequence_iter_prev (iter));

    switch (method) {
      case GST_INDEX_LOOKUP_METHOD_EXACT:
      case GST_INDEX_LOOKUP_METHOD_AFTER:
        if (cmp_func (entry, &dummy, NULL) == 0)
          target = entry;
        break;
      case GST_INDEX_LOOKUP_METHOD_BEFORE:
        target = entry;
        break;
    }
  } else {
    IndexEntry *prev = g_sequence_get (g_sequence_iter_prev (iter));
    IndexEntry *next = g_sequence_get (iter);

    switch (method) {
      case GST_INDEX_LOOKUP_METHOD_BEFORE:
        target = prev;
        break;
      case GST_INDEX_LOOKUP_METHOD_AFTER:
        target = next;
        break;
      case GST_INDEX_LOOKUP_METHOD_EXACT:
        if (cmp_func (prev, &dummy, NULL) == 0)
          target = prev;
        else if (cmp_func (next, &dummy, NULL) == 0)
          target = next;
        break;
    }
  }

  if (target) {
    *stream_time = target->stream_time;
    *offset = target->offset;

    if (extra)
      *extra = target->extra;

    ret = TRUE;
  }

done:
  return ret;
}

static gboolean
gst_mem_index_lookup_unit_time (GstIndex * index,
    const gchar * stream_id,
    GstIndexLookupMethod method,
    GstIndexUnitType type,
    GstIndexLookupFlags flags,
    GstClockTime target_stream_time,
    GstClockTime * stream_time, guint64 * offset, GstStructure ** extra)
{
  GstMemIndex *mindex = GST_MEM_INDEX (index);
  StreamIndex *stream_index;
  gboolean ret = FALSE;

  g_mutex_lock (&mindex->priv->lock);

  if (!(stream_index =
          g_hash_table_lookup (mindex->priv->stream_indices, stream_id))) {
    GST_LOG ("No such stream %s (%p)", stream_id, stream_index);
    goto done;
  }

  ret =
      gst_mem_index_lookup_unlocked (mindex, stream_index->all_by_time,
      stream_index->sync_points_by_time, method, type, target_stream_time,
      _cmp_entry_time, stream_time, offset, extra);

  if (ret && (flags & GST_INDEX_LOOKUP_FLAG_CONTIGUOUS)) {
    ret =
        _scanned_ranges_check_same_range (stream_index->scanned_ranges_by_time,
        target_stream_time, *stream_time);
  }

done:
  g_mutex_unlock (&mindex->priv->lock);

  return ret;
}

static gboolean
gst_mem_index_lookup_unit_offset (GstIndex * index,
    const gchar * stream_id,
    GstIndexLookupMethod method,
    GstIndexUnitType type,
    GstIndexLookupFlags flags,
    GstClockTime target_offset,
    GstClockTime * stream_time, guint64 * offset, GstStructure ** extra)
{
  GstMemIndex *mindex = GST_MEM_INDEX (index);
  StreamIndex *stream_index;
  gboolean ret = FALSE;

  g_mutex_lock (&mindex->priv->lock);

  if (!(stream_index =
          g_hash_table_lookup (mindex->priv->stream_indices, stream_id))) {
    GST_LOG ("No such stream %s (%p)", stream_id, stream_index);
    goto done;
  }

  ret =
      gst_mem_index_lookup_unlocked (mindex, stream_index->all_by_offset,
      stream_index->sync_points_by_offset, method, type, target_offset,
      _cmp_entry_offset, stream_time, offset, extra);

  if (ret && (flags & GST_INDEX_LOOKUP_FLAG_CONTIGUOUS)) {
    ret =
        _scanned_ranges_check_same_range
        (stream_index->scanned_ranges_by_offset, target_offset, *stream_time);
  }

done:
  g_mutex_unlock (&mindex->priv->lock);

  return ret;
}

static void
_index_iface_init (GstIndexInterface * iface)
{
  iface->add_unit = gst_mem_index_add_unit;
  iface->lookup_unit_time = gst_mem_index_lookup_unit_time;
  iface->lookup_unit_offset = gst_mem_index_lookup_unit_offset;
}

G_DEFINE_TYPE_WITH_CODE (GstMemIndex, gst_mem_index,
    G_TYPE_OBJECT, G_ADD_PRIVATE (GstMemIndex)
    G_IMPLEMENT_INTERFACE (GST_TYPE_INDEX, _index_iface_init));

static void
gst_mem_index_finalize (GObject * object)
{
  GstMemIndex *index = GST_MEM_INDEX (object);

  g_hash_table_unref (index->priv->stream_indices);
  index->priv->stream_indices = NULL;
  g_mutex_clear (&index->priv->lock);

  G_OBJECT_CLASS (gst_mem_index_parent_class)->finalize (object);
}

static void
gst_mem_index_class_init (GstMemIndexClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = gst_mem_index_finalize;
}

static void
gst_mem_index_init (GstMemIndex * index)
{
  index->priv = gst_mem_index_get_instance_private (index);

  index->priv->stream_indices =
      g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      (GDestroyNotify) _free_stream_index);
}

GstMemIndex *
gst_mem_index_new (void)
{
  return g_object_new (GST_TYPE_MEM_INDEX, NULL);
}

static GVariant *
_stream_index_to_variant (StreamIndex * sindex)
{
  GSequenceIter *iter;
  GVariantBuilder builder;
  GList *tmp;

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("(a(ttus)a(tt)a(tt))"));

  g_variant_builder_open (&builder, G_VARIANT_TYPE ("a(ttus)"));

  /* Iterate in reverse to preserve insertion order */
  for (tmp = g_list_last (sindex->entries); tmp; tmp = tmp->prev) {
    gchar *extra_str;
    IndexEntry *entry = (IndexEntry *) tmp->data;

    if (entry->extra) {
      extra_str = gst_structure_to_string (entry->extra);
    } else {
      extra_str = g_strdup ("\0");
    }

    g_variant_builder_add (&builder, "(ttus)",
        entry->stream_time, entry->offset, entry->type, extra_str);

    g_free (extra_str);
  }

  g_variant_builder_close (&builder);

  g_variant_builder_open (&builder, G_VARIANT_TYPE ("a(tt)"));

  for (iter = g_sequence_get_begin_iter (sindex->scanned_ranges_by_time);
      !g_sequence_iter_is_end (iter); iter = g_sequence_iter_next (iter)) {
    ScannedRange *range = g_sequence_get (iter);

    g_variant_builder_add (&builder, "(tt)", range->start, range->end);
  }

  g_variant_builder_close (&builder);

  g_variant_builder_open (&builder, G_VARIANT_TYPE ("a(tt)"));

  for (iter = g_sequence_get_begin_iter (sindex->scanned_ranges_by_offset);
      !g_sequence_iter_is_end (iter); iter = g_sequence_iter_next (iter)) {
    ScannedRange *range = g_sequence_get (iter);

    g_variant_builder_add (&builder, "(tt)", range->start, range->end);
  }

  g_variant_builder_close (&builder);

  return g_variant_builder_end (&builder);
}

static StreamIndex *
_stream_index_from_variant (GVariant * variant)
{
  GVariantIter iter;
  guint64 stream_time, offset;
  guint32 type;
  gchar *extra_str;
  StreamIndex *ret = NULL;
  GVariantIter *heap_iter;
  guint64 range_start, range_end;

  if (!g_variant_is_of_type (variant, G_VARIANT_TYPE ("(a(ttus)a(tt)a(tt))")))
    goto err;

  ret = _new_stream_index ();

  g_variant_iter_init (&iter, variant);

  g_variant_iter_next (&iter, "a(ttus)", &heap_iter);
  while (g_variant_iter_next (heap_iter, "(ttus)", &stream_time, &offset, &type,
          &extra_str)) {
    IndexEntry *entry;
    GstStructure *extra = NULL;

    if (strlen (extra_str) > 0) {
      gchar *endptr = NULL;
      extra = gst_structure_from_string (extra_str, &endptr);

      if (!extra || endptr) {
        GST_ERROR ("Failed to parse extra structure");

        if (extra)
          gst_structure_free (extra);

        goto err;
      }
    }

    g_free (extra_str);

    entry = _new_index_entry (stream_time, offset, type, extra);

    if (!_stream_index_append (ret, entry)) {
      GST_ERROR ("Invalid entry found when deserializing index");
      goto err;
    }
  }
  g_variant_iter_free (heap_iter);

  g_variant_iter_next (&iter, "a(tt)", &heap_iter);
  while (g_variant_iter_next (heap_iter, "(tt)", &range_start, &range_end)) {
    ScannedRange *srange = _new_scanned_range (range_start, range_end);

    g_sequence_append (ret->scanned_ranges_by_time, srange);
  }
  g_variant_iter_free (heap_iter);

  g_variant_iter_next (&iter, "a(tt)", &heap_iter);
  while (g_variant_iter_next (heap_iter, "(tt)", &range_start, &range_end)) {
    ScannedRange *srange = _new_scanned_range (range_start, range_end);

    g_sequence_append (ret->scanned_ranges_by_offset, srange);
  }
  g_variant_iter_free (heap_iter);

  g_sequence_sort (ret->all_by_time, _cmp_entry_time, NULL);
  g_sequence_sort (ret->sync_points_by_time, _cmp_entry_time, NULL);
  g_sequence_sort (ret->all_by_offset, _cmp_entry_offset, NULL);
  g_sequence_sort (ret->sync_points_by_offset, _cmp_entry_offset, NULL);
  g_sequence_sort (ret->scanned_ranges_by_time, _cmp_scanned_range, NULL);
  g_sequence_sort (ret->scanned_ranges_by_offset, _cmp_scanned_range, NULL);

done:
  return ret;

err:
  if (ret) {
    _free_stream_index (ret);
    ret = NULL;
  }

  goto done;
}

/**
 * gst_mem_index_to_variant:
 * @index: The index to serialize
 *
 * Serialize an index to #GVariant. Use gst_mem_index_new_from_variant()
 * to deserialize it.
 *
 * Returns: (transfer floating): The index serialized as a #GVariant
 */
GVariant *
gst_mem_index_to_variant (GstMemIndex * index)
{
  GHashTableIter iter;
  gpointer key, value;
  GVariantBuilder *builder;
  GVariant *dict;

  builder = g_variant_builder_new (G_VARIANT_TYPE ("a{sv}"));

  g_hash_table_iter_init (&iter, index->priv->stream_indices);

  while (g_hash_table_iter_next (&iter, &key, &value)) {
    gchar *stream_id = (gchar *) key;
    StreamIndex *sindex = (StreamIndex *) value;

    g_variant_builder_add (builder, "{sv}", stream_id,
        _stream_index_to_variant (sindex));
  }

  dict = g_variant_builder_end (builder);

  return dict;
}

/**
 * gst_mem_index_new_from_variant:
 * @variant: The variant to deserialize the index from
 *
 * Constructs a new index from a GVariant as serialized by
 * gst_mem_index_to_variant().
 *
 * Returns: (transfer full) (nullable): The deserialized @GstMemIndex
 */
GstMemIndex *
gst_mem_index_new_from_variant (GVariant * variant)
{
  GVariantIter iter;
  gchar *key;
  GVariant *value;
  GstMemIndex *ret = NULL;

  if (!g_variant_is_of_type (variant, G_VARIANT_TYPE ("a{sv}"))) {
    GST_ERROR ("Invalid format for index variant");
    goto err;
  }

  ret = gst_mem_index_new ();

  g_variant_iter_init (&iter, variant);

  while (g_variant_iter_loop (&iter, "{sv}", &key, &value)) {
    StreamIndex *sindex;

    if (!(sindex = _stream_index_from_variant (value))) {
      goto err;
    }

    g_hash_table_insert (ret->priv->stream_indices, g_strdup (key), sindex);
  }

done:
  return ret;

err:
  if (ret) {
    g_object_unref (ret);
    ret = NULL;
  }

  goto done;
}
