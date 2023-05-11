/* GStreamer
 * Copyright (C) <2023> Vivienne Watermeier <vwatermeier@igalia.com>
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

/*
 * aggregate-return warns for calling a function that returns a value,
 * so this pragma will prevent users of this API from getting warnings on every
 * use of rational_time functions. Unfortunately, this will also silence all
 * other aggregate-return warnings in any files including this header.
 */
#pragma GCC diagnostic ignored "-Waggregate-return"

#include <glib.h>
#include <gst/gstconfig.h>
#include <gst/gstmeta.h>

G_BEGIN_DECLS

/**
 * GST_RATIONAL_TIME:
 * @n: the numerator
 * @d: the denominator
 *
 * Creates a #GstRationalTime.
 *
 * Since: 1.24
 */
#define GST_RATIONAL_TIME(n, d)       (gst_rational_time_reduce ((GstRationalTime) { (n), (d) }))

/**
 * GST_RATIONAL_TIME_NONE:
 *
 * Constant to define an undefined rational timestamp.
 *
 * Since: 1.24
 */
#define GST_RATIONAL_TIME_NONE        ((GstRationalTime) { 0, 0 })

/**
 * GST_RATIONAL_TIME_ZERO:
 *
 * Constant to define a rational timestamp equal to 0.
 *
 * Since: 1.24
 */
#define GST_RATIONAL_TIME_ZERO        ((GstRationalTime) { 0, 1 })

/**
 * GST_RATIONAL_TIME_IS_VALID:
 * @t: a rational timestamp to validate
 *
 * Tests if a given #GstRationalTime represents a valid defined time.
 *
 * Since: 1.24
 */
#define GST_RATIONAL_TIME_IS_VALID(t) ((t).denom != 0)

/**
 * GST_CLOCK_TIME_AS_RATIONAL_TIME:
 * @t: the time
 *
 * Converts a #GstClockTime to #GstRationalTime, reduced to smallest terms.
 *
 * Since: 1.24
 */
#define GST_CLOCK_TIME_AS_RATIONAL_TIME(t) \
    (GST_CLOCK_TIME_IS_VALID ((t)) ? \
    (gst_rational_time_reduce (GST_RATIONAL_TIME ((gint64) (t), GST_SECOND))) : GST_RATIONAL_TIME_NONE)

/**
 * GST_CLOCK_STIME_AS_RATIONAL_TIME:
 * @t: the time
 *
 * Converts a #GstClockTimeDiff to #GstRationalTime, reduced to smallest terms.
 *
 * Since: 1.24
 */
#define GST_CLOCK_STIME_AS_RATIONAL_TIME(t) \
    (GST_CLOCK_STIME_IS_VALID ((t)) ? \
    (gst_rational_time_reduce (GST_RATIONAL_TIME ((gint64) (t), GST_SECOND))) : GST_RATIONAL_TIME_NONE)

/**
 * GST_RATIONAL_TIME_AS_CLOCK_TIME:
 * @t: a rational timestamp
 *
 * Converts a #GstRationalTime to #GstClockTime.
 *
 * Since: 1.24
 */
#define GST_RATIONAL_TIME_AS_CLOCK_TIME(t) \
    ((GstClockTime) GST_RATIONAL_TIME_IS_VALID ((t)) && (t).num >= 0 ? \
    (gst_util_uint64_scale ((t).num, GST_SECOND, (t).denom)) : GST_CLOCK_TIME_NONE)

/**
 * GST_RATIONAL_TIME_AS_CLOCK_STIME:
 * @t: a rational timestamp
 *
 * Converts a #GstRationalTime to #GstClockTimeDiff.
 *
 * Since: 1.24
 */
#define GST_RATIONAL_TIME_AS_CLOCK_STIME(t) \
    ((GstClockTimeDiff) GST_RATIONAL_TIME_IS_VALID ((t)) ? \
    (((t).num < 0 ? -1 : 1) * gst_util_uint64_scale (ABS ((t).num), GST_SECOND, (t).denom)) : GST_CLOCK_STIME_NONE)

/**
 * GST_RATIONAL_TIME_AS_SECONDS:
 * @t: a rational timestamp
 *
 * Converts a #GstRationalTime to seconds. @t must be valid and non-negative.
 *
 * Since: 1.24
 */
#define GST_RATIONAL_TIME_AS_SECONDS(t)  (gst_util_uint64_scale ((t).num, 1, (t).denom))

/**
 * GST_RATIONAL_TIME_AS_MSECONDS:
 * @t: a rational timestamp
 *
 * Converts a #GstRationalTime to milliseconds (1/1000 of a second).
 * @t must be valid and non-negative.
 *
 * Since: 1.24
 */
#define GST_RATIONAL_TIME_AS_MSECONDS(t) (gst_util_uint64_scale ((t).num, 1000, (t).denom))

/**
 * GST_RATIONAL_TIME_AS_USECONDS:
 * @t: a rational timestamp
 *
 * Converts a #GstRationalTime to microseconds (1/1000000 of a second).
 * @t must be valid and non-negative.
 *
 * Since: 1.24
 */
#define GST_RATIONAL_TIME_AS_USECONDS(t) (gst_util_uint64_scale ((t).num, 1000000, (t).denom))

/**
 * GST_RATIONAL_TIME_AS_NSECONDS:
 * @t: a rational timestamp
 *
 * Converts a #GstRationalTime to nanoseconds (1/1000000000 of a second).
 * @t must be valid and non-negative.
 *
 * Since: 1.24
 */
#define GST_RATIONAL_TIME_AS_NSECONDS(t) (gst_util_uint64_scale ((t).num, 1000000000, (t).denom))

/**
 * GST_RATIONAL_TIME_ABS:
 * @t: the time
 *
 * Evaluates to the absolute value of @t. @t must be valid..
 *
 * Since: 1.24
 */
#define GST_RATIONAL_TIME_ABS(t)         ((GstRationalTime) {(t).num < 0 ? -(t).num : (t).num, (t).denom})

/**
 * GST_RATIONAL_TIME_MIN:
 * @a: a #GstRationalTime
 * @b: a #GstRationalTime
 *
 * Evaluates to the minimum of @a and @b. @a and @b must be valid.
 *
 * Since: 1.24
 */
#define GST_RATIONAL_TIME_MIN(a, b)      (gst_rational_time_cmp ((a), (b)) < 0 ? (a) : (b))

/**
 * GST_RATIONAL_TIME_MAX:
 * @a: a #GstRationalTime
 * @b: a #GstRationalTime
 *
 * Evaluates to the maximum of @a and @b. @a and @b must be valid.
 *
 * Since: 1.24
 */
#define GST_RATIONAL_TIME_MAX(a, b)      (gst_rational_time_cmp ((a), (b)) > 0 ? (a) : (b))

/**
 * GST_RATIONAL_TIME_CLAMP:
 * @t: a #GstRationalTime value to be clamped
 * @low: a lower bound #GstRationalTime
 * @high: an upper bound #GstRationalTime
 *
 * Evaluates to @t clamped to the range defined by @low and @high.
 * @t. @low and @high must be valid.
 *
 * Since: 1.24
 */
#define GST_RATIONAL_TIME_CLAMP(t, low, high) \
    (gst_rational_time_cmp ((t), (high)) > 0 ? \
    (high) : (gst_rational_time_cmp ((t), (low)) < 0 ? (low) : (t)))

/**
 * GST_RATIONAL_TIME_FORMAT:
 *
 * A string that can be used in printf-like format strings to display a
 * #GstRationalTime value. Use GST_RATIONAL_TIME_ARGS() to construct the
 * matching arguments. Prints the time as a fraction, and additionally prints
 * the equivalent #GstClockTime.
 *
 * Example:
 *
 * ``` C
 * printf("%" GST_RATIONAL_TIME_FORMAT "\n", GST_RATIONAL_TIME_ARGS(time));
 * ```
 *
 * Since: 1.24
 */
#define GST_RATIONAL_TIME_FORMAT            "ld/%u (%s%u:%02u:%02u.%09u)"

/**
 * GST_RATIONAL_TIME_FORMAT_SHORT:
 *
 * A strint that can be used in printf-like format strings ti display a
 * #GstRationalTime value. Use GST_RATIONAL_TIME_ARGS_SHORT() to construct the
 * matching arguments. Prints the time as a fraction.
 *
 * Example:
 *
 * ```C
 * printf("%" GST_RATIONAL_TIME_FORMAT_SHORT "\n", GST_RATIONAL_TIME_ARGS_SHORT(time))
 * ```
 *
 * Since: 1.24
 */
#define GST_RATIONAL_TIME_FORMAT_SHORT      "ld/%u"

/**
 * GST_RATIONAL_TIME_ARGS:
 * @t: a rational timestamp
 *
 * Formats @t for the #GST_RATIONAL_TIME_FORMAT format string. Note: @t will be
 * evaluated more than once.
 *
 * Since: 1.24
 */
#define GST_RATIONAL_TIME_ARGS(t) \
        (gint64) ((t).num), \
        (guint) ((t).denom), \
        GST_RATIONAL_TIME_IS_VALID (t) && (t).num < 0 ? "-" : "", \
        GST_RATIONAL_TIME_IS_VALID (t) ? \
        (guint) (GST_RATIONAL_TIME_AS_SECONDS (GST_RATIONAL_TIME_ABS (t)) / (60 * 60)) : 99, \
        GST_RATIONAL_TIME_IS_VALID (t) ? \
        (guint) ((GST_RATIONAL_TIME_AS_SECONDS (GST_RATIONAL_TIME_ABS (t)) / 60) % 60) : 99, \
        GST_RATIONAL_TIME_IS_VALID (t) ? \
        (guint) (GST_RATIONAL_TIME_AS_SECONDS (GST_RATIONAL_TIME_ABS (t)) % 60) : 99, \
        GST_RATIONAL_TIME_IS_VALID (t) ? \
        (guint) (GST_RATIONAL_TIME_AS_NSECONDS (GST_RATIONAL_TIME_ABS (t)) % GST_SECOND) : 999999999

/**
 * GST_RATIONAL_TIME_ARGS_SHORT:
 * @t: a rational timestamp
 *
 * Formats @t for the #GST_RATIONAL_TIME_FORMAT format string. Note: @t will be
 * evaluated more than once.
 *
 * Since: 1.24
*/
#define GST_RATIONAL_TIME_ARGS_SHORT(t) \
        (gint64) ((t).num), \
        (guint) ((t).denom)


typedef struct _GstRationalTimeMeta GstRationalTimeMeta;
typedef struct _GstRationalTime GstRationalTime;

/**
 * GstRationalTimeMeta:
 * @meta: parent #GstMeta
 * @get_buffer_time: virtual function to get buffer timestamps and durations
 * @get_stream_time: virtual function to get stream-time timestamps and durations
 *
 * A meta that can be used to communicate in a lossless way the rational
 * timestamps and durations found in the media data of various formats.
 *
 * Since: 1.24
 */
struct _GstRationalTimeMeta {
  GstMeta meta;

  /**
   * GstRationalTimeMeta::get_buffer_time:
   * @meta: the #RationalTimeMeta
   * @dts: (out) (optional): buffer DTS
   * @dts_duration: (out) (optional): buffer decode duration
   * @pts: (out) (optional): buffer PTS
   * @pts_duration: (out) (optional): buffer presentation duration
   *
   * Get buffer timestamps and durations stored in a #GstRationalTimeMeta.
   *
   * Since: 1.24
   */
  void (*get_buffer_time) (GstRationalTimeMeta *meta,
                           GstRationalTime     *dts,
                           GstRationalTime     *dts_duration,
                           GstRationalTime     *pts,
                           GstRationalTime     *pts_duration);

  /**
   * GstRationalTimeMeta::get_stream_time:
   * @meta: the #RationalTimeMeta
   * @dts: (out) (optional): stream-time DTS
   * @dts_duration: (out) (optional): stream-time decode duration
   * @pts: (out) (optional): stream-time PTS
   * @pts_duration: (out) (optional): stream-time presentation duration
   *
   * Get stream-time timestamps and durations stored in a #GstRationalTimeMeta.
   *
   * Since: 1.24
   */
  void (*get_stream_time) (GstRationalTimeMeta *meta,
                           GstRationalTime     *dts,
                           GstRationalTime     *dts_duration,
                           GstRationalTime     *pts,
                           GstRationalTime     *pts_duration);
};

/**
 * GstRationalTime:
 * @num: the numerator
 * @denom: the denominator
 *
 * A structure representing rational timestamps with a numerator and a
 * denominator. A timestamp with a numerator of G_MININT64 or a denominator of 0
 * is considered undefined, similar to #GST_CLOCK_TIME_NONE.
 *
 * Since: 1.24
 */
struct _GstRationalTime {
  gint64 num;
  guint32 denom;
};

GST_API
GType gst_rational_time_meta_api_get_type (void);
/**
 * GST_RATIONAL_TIME_META_API_TYPE:
 *
 * Since: 1.24
 */
#define GST_RATIONAL_TIME_META_API_TYPE (gst_rational_time_meta_api_get_type())

/**
 * gst_buffer_get_rational_time_meta:
 * @b: a #GstBuffer
 *
 * Gets the #GstRationalTimeMeta that might be present on @b.
 *
 * Returns: (nullable): The first #GstRationalTimeMeta present on @b, or NULL
 * no #GstRationalTimeMeta are present.
 *
 * Since: 1.24
 */
#define gst_buffer_get_rational_time_meta(b) \
  ((GstRationalTimeMeta*)gst_buffer_get_meta((b),GST_RATIONAL_TIME_META_API_TYPE))

/* math functions for rational time */

GST_API
GstRationalTime gst_rational_time_add                     (GstRationalTime lhs,
                                                           GstRationalTime rhs);

GST_API
GstRationalTime gst_rational_time_subtract                (GstRationalTime lhs,
                                                           GstRationalTime rhs);

GST_API
gint64          gst_rational_time_cmp                     (GstRationalTime lhs,
                                                           GstRationalTime rhs);

GST_API
GstRationalTime gst_rational_time_reduce                  (GstRationalTime time);

GST_API
gboolean        gst_rational_time_to_lowest_common_denom  (GstRationalTime *lhs,
                                                           GstRationalTime *rhs);

G_END_DECLS
