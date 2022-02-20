/* A simple parser for Cube files
 * Copyright (C) <2022> Filippo Argiolas <filippo.argiolas@gmail.com>
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

#ifndef __CUBELUT_H__
#define __CUBELUT_H__

#include <gst/gst.h>

G_BEGIN_DECLS

typedef enum {
  CUBE_LUT_1D,
  CUBE_LUT_3D,
} CubeLUTType;

typedef struct {
  gchar *title;
  double *table;
  CubeLUTType type;
  gint size;
  glong length;
  gdouble domain_min[3];
  gdouble domain_max[3];
} CubeLUT;

CubeLUT *cube_lut_load(const char *filename);
void     cube_lut_free(CubeLUT *lut);
gdouble  cube_lut_lookup (CubeLUT * lut, gint r, gint g, gint b, gint comp);

G_END_DECLS

#endif /* __CUBELUT_H__ */
