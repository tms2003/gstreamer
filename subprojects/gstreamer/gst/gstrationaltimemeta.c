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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstrationaltimemeta.h"
#include "gstutils.h"

GType
gst_rational_time_meta_api_get_type (void)
{
  static GType type;
  static const gchar *tags[] = { NULL };

  if (g_once_init_enter (&type)) {
    GType _type = gst_meta_api_type_register ("GstRationalTimeMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }
  return type;
}

/* math functions */

/**
 * gst_rational_time_add:
 * @lhs: first addend
 * @rhs: second addend
 *
 * Adds two #GstRationalTime (@lhs + @rhs).
 *
 * Returns: the sum of @lhs and @rhs
 *
 * Since: 1.24
 */
GstRationalTime
gst_rational_time_add (GstRationalTime lhs, GstRationalTime rhs)
{
  guint32 gcd;
  GstRationalTime sum;

  g_return_val_if_fail (GST_RATIONAL_TIME_IS_VALID (lhs)
      && GST_RATIONAL_TIME_IS_VALID (rhs), GST_RATIONAL_TIME_NONE);

  if (lhs.denom == rhs.denom) {
    sum.num = lhs.num + rhs.num;
    sum.denom = lhs.denom;
  } else {
    /* new denominator is least common multiple, calculated via gcd */
    gcd = gst_util_greatest_common_divisor_int64 (lhs.denom, rhs.denom);
    sum.num = lhs.num * (rhs.denom / gcd) + rhs.num * (lhs.denom / gcd);
    sum.denom = lhs.denom * (rhs.denom / gcd);
  }

  return gst_rational_time_reduce (sum);
}

/**
 * gst_rational_time_subtract:
 * @lhs: the minuend
 * @rhs: the subtrahend
 *
 * Subtracts two #GstRationalTime (@lhs - @rhs).
 *
 * Returns: the difference of @lhs and @rhs
 *
 * Since: 1.24
 */
GstRationalTime
gst_rational_time_subtract (GstRationalTime lhs, GstRationalTime rhs)
{
  guint32 gcd;
  GstRationalTime diff;

  g_return_val_if_fail (GST_RATIONAL_TIME_IS_VALID (lhs)
      && GST_RATIONAL_TIME_IS_VALID (rhs), GST_RATIONAL_TIME_NONE);

  if (lhs.denom == rhs.denom) {
    diff.num = lhs.num - rhs.num;
    diff.denom = lhs.denom;
  } else {
    /* new denominator is least common multiple, calculated via gcd */
    gcd = gst_util_greatest_common_divisor_int64 (lhs.denom, rhs.denom);
    diff.num = lhs.num * (rhs.denom / gcd) - rhs.num * (lhs.denom / gcd);
    diff.denom = lhs.denom * (rhs.denom / gcd);
  }

  return gst_rational_time_reduce (diff);
}

/**
 * gst_rational_time_cmp:
 * @lhs: the first #GstRationalTime
 * @rhs: the second #GstRationalTime
 *
 * Compares two #GstRationalTime.
 *
 * Returns: 0 if @lhs == @rhs, < 0 if @lhs < @rhs, > 0 if @lhs > @rhs
 *
 * Since: 1.24
 */
gint64
gst_rational_time_cmp (GstRationalTime lhs, GstRationalTime rhs)
{
  g_return_val_if_fail (GST_RATIONAL_TIME_IS_VALID (lhs)
      && GST_RATIONAL_TIME_IS_VALID (rhs), 0);

  lhs = gst_rational_time_reduce (lhs);
  rhs = gst_rational_time_reduce (rhs);

  return lhs.num * rhs.denom - rhs.num * lhs.denom;
}

/**
 * gst_rational_time_reduce:
 * @time: a #GstRationalTime
 *
 * Reduces @time to lowest terms.
 *
 * Returns: a reduced #GstRationalTime equal to @time in value.
 *
 * Since: 1.24
 */
GstRationalTime
gst_rational_time_reduce (GstRationalTime time)
{
  GstRationalTime res;
  guint32 gcd;

  g_return_val_if_fail (GST_RATIONAL_TIME_IS_VALID (time),
      GST_RATIONAL_TIME_NONE);

  gcd = gst_util_greatest_common_divisor_int64 (time.num, time.denom);

  res.num = time.num / gcd;
  res.denom = time.denom / gcd;

  return res;
}

/**
 * gst_rational_time_to_lowest_common_denom:
 * @lhs: the first #GstRationalTime
 * @rhs: the second #GstRationalTime
 *
 * Extends or reduces both fractions to the lowest common denominator.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 1.24
 */
gboolean
gst_rational_time_to_lowest_common_denom (GstRationalTime * lhs,
    GstRationalTime * rhs)
{
  guint32 gcd, factor;

  g_return_val_if_fail (lhs != NULL && rhs != NULL
      && GST_RATIONAL_TIME_IS_VALID (*lhs) && GST_RATIONAL_TIME_IS_VALID (*rhs),
      FALSE);

  *lhs = gst_rational_time_reduce (*lhs);
  *rhs = gst_rational_time_reduce (*rhs);

  if (lhs->denom != rhs->denom) {
    gcd = gst_util_greatest_common_divisor_int64 (lhs->denom, rhs->denom);

    factor = rhs->denom / gcd;
    lhs->num *= factor;
    lhs->denom *= factor;

    factor = lhs->denom / gcd;
    rhs->num *= factor;
    rhs->denom *= factor;
  }

  return TRUE;
}
