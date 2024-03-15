/* GStreamer
 * Copyright (C) 2024 Seungha Yang <seungha@centricular.com>
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

#include <gst/gst.h>

static void
deinit_cb (guint * cb_count)
{
  *cb_count += 1;
}

int
main (int argc, char **argv)
{
  guint cb_count = 0;

  /* Installing callback before gst_init() is allowed */
  gst_deinit_register_notify ((GstDeinitNotifyFunc) deinit_cb, &cb_count);

  gst_init (NULL, NULL);

  /* Install callback again */
  gst_deinit_register_notify ((GstDeinitNotifyFunc) deinit_cb, &cb_count);

  gst_deinit ();

  g_assert (cb_count == 2);

  return 0;
}
