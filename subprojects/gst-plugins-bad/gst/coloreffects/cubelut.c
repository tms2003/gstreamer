/* A simple parser for Cube files
 * Implements Adobe Cube LUT Specification Version 1.0
 * https://wwwimages2.adobe.com/content/dam/acom/en/products/speedgrade/cc/pdfs/cube-lut-specification-1.0.pdf
 *
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

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <gst/gst.h>
#include <gstcoloreffects.h>
#include "cubelut.h"


#define GST_CAT_DEFAULT (coloreffects_debug)

enum
{
  CUBE_TITLE = G_TOKEN_LAST + 1,
  CUBE_LUT_1D_SIZE,
  CUBE_LUT_3D_SIZE,
  CUBE_DOMAIN_MIN,
  CUBE_DOMAIN_MAX
};

#define ROUND(x) ((int) ((x) + .5))
gdouble
cube_lut_lookup (CubeLUT * lut, gint r, gint g, gint b, gint comp)
{
  glong index = b * lut->size * lut->size + g * lut->size + r;
  return lut->table[index * 3 + comp];
}



/* Nearest Neighbour interpolation */
void
cube_lut_interp_nearest (CubeLUT * lut, const gdouble in[], gdouble out[])
{
  gint i;
  for (i = 0; i < 3; i++) {
    out[i] =
        cube_lut_lookup (lut, ROUND (in[0]), ROUND (in[1]), ROUND (in[2]), i);
  }
}

static const gboolean
cube_lut_parse_title (GScanner * scanner, gchar ** title)
{
  if (g_scanner_get_next_token (scanner) != G_TOKEN_STRING) {
    g_scanner_unexp_token (scanner, G_TOKEN_STRING, NULL, NULL, NULL,
        "invalid TITLE", TRUE);
    return FALSE;
  } else {
    *title = g_strdup (scanner->value.v_string);
    return TRUE;
  }
}

static gboolean
cube_lut_parse_domain (GScanner * scanner, gdouble * domain)
{
  gint symbol = GPOINTER_TO_INT (scanner->value.v_symbol);

  for (int i = 0; i < 3; i++) {
    if (g_scanner_get_next_token (scanner) != G_TOKEN_FLOAT) {
      g_scanner_unexp_token (scanner, G_TOKEN_FLOAT, NULL, NULL, NULL,
          "invalid DOMAIN value", TRUE);
      return FALSE;
    } else {
      switch (symbol) {
        case CUBE_DOMAIN_MIN:
          if (scanner->value.v_float != 0) {
            GST_WARNING
                ("unsupported DOMAIN_MIN %f, we only support domains in the [0, 1] range",
                scanner->value.v_float);
            return FALSE;
          }
          domain[i] = scanner->value.v_float;
          break;
        case CUBE_DOMAIN_MAX:
          if (scanner->value.v_float != 1) {
            GST_WARNING
                ("unsupported DOMAIN_MAX %f, we only support domains in the [0, 1] range",
                scanner->value.v_float);
            return FALSE;
          }
          domain[i] = scanner->value.v_float;
          break;
        default:
          g_assert_not_reached ();
      }
    }
  }

  GST_INFO ("DOMAIN_%s: %.1f %.1f %.1f",
      symbol == CUBE_DOMAIN_MIN ? "MIN" : "MAX",
      domain[0], domain[1], domain[2]);

  return TRUE;
}


static CubeLUT *
cube_lut_new (CubeLUTType lut_type, glong lut_size)
{
  CubeLUT *lut = NULL;
  switch (lut_type) {
    case CUBE_LUT_1D:
      if ((lut_size < 2) || (lut_size > 65536)) {
        GST_ERROR ("LUT_1D_size outside valid range");
      } else {
        lut = g_new0 (CubeLUT, 1);
        lut->title = NULL;
        lut->size = lut_size;
        lut->length = lut_size * 3;
        lut->type = lut_type;
        lut->table = g_new0 (gdouble, lut->length);
      }
      break;
    case CUBE_LUT_3D:
      if ((lut_size < 2) || (lut_size > 256)) {
        GST_ERROR ("LUT_3D_SIZE outside valid range");
        return NULL;
      } else {
        lut = g_new0 (CubeLUT, 1);
        lut->title = NULL;
        lut->size = lut_size;
        lut->length = lut_size * lut_size * lut_size * 3;
        lut->type = lut_type;
        lut->table = g_new0 (gdouble, lut->length);
      }
      break;
    default:
      g_assert_not_reached ();
  }

  GST_INFO ("LUT TYPE: %s", lut->type == CUBE_LUT_1D ? "1D" : "3D");
  GST_INFO ("LUT SIZE: %d", (gint) lut_size);

  return lut;
}

static gboolean
cube_lut_parse_lut_size (GScanner * scanner, glong * lut_size)
{
  if (g_scanner_get_next_token (scanner) != G_TOKEN_FLOAT) {
    g_scanner_unexp_token (scanner, G_TOKEN_FLOAT, NULL, NULL, NULL,
        "invalid LUT SIZE", TRUE);
    return FALSE;
  } else {
    *lut_size = (glong) scanner->value.v_float;
    return TRUE;
  }
}

void
cube_lut_free (CubeLUT * lut)
{
  if (lut->table != NULL)
    g_free (lut->table);
  if (lut->title != NULL)
    g_free (lut->title);
  g_free (lut);
}

static void
cube_lut_parser_error_handler (GScanner * scanner, gchar * message,
    gboolean error)
{
  if (error) {
    GST_ERROR ("%s", message);
  } else {
    GST_WARNING ("%s", message);
  }
}

CubeLUT *
cube_lut_load (const char *filename)
{
  GScanner *scanner;
  gint fd;
  CubeLUT *lut = NULL;
  gchar *title = NULL;
  gdouble domain_min[3];
  gdouble domain_max[3];
  CubeLUTType lut_type;
  glong lut_size = 0;

  glong idx = 0;

  scanner = g_scanner_new (NULL);
  scanner->config->numbers_2_int = TRUE;
  scanner->config->int_2_float = TRUE;
  scanner->config->symbol_2_token = FALSE;
  scanner->msg_handler = (GScannerMsgFunc) cube_lut_parser_error_handler;

  g_scanner_scope_add_symbol (scanner, 0, "TITLE",
      GINT_TO_POINTER (CUBE_TITLE));
  g_scanner_scope_add_symbol (scanner, 0, "LUT_1D_SIZE",
      GINT_TO_POINTER (CUBE_LUT_1D_SIZE));
  g_scanner_scope_add_symbol (scanner, 0, "LUT_3D_SIZE",
      GINT_TO_POINTER (CUBE_LUT_3D_SIZE));
  g_scanner_scope_add_symbol (scanner, 0, "DOMAIN_MIN",
      GINT_TO_POINTER (CUBE_DOMAIN_MIN));
  g_scanner_scope_add_symbol (scanner, 0, "DOMAIN_MAX",
      GINT_TO_POINTER (CUBE_DOMAIN_MAX));

  fd = open (filename, O_RDONLY, 0);
  if (!fd) {
    GST_ERROR ("could not open %s for reading", filename);
    return NULL;
  }

  g_scanner_input_file (scanner, fd);
  scanner->input_name = "cube-lut-parser";

  GST_INFO ("parsing cube file: %s", filename);

  while (g_scanner_get_next_token (scanner) != G_TOKEN_EOF) {
    switch (scanner->token) {
      case G_TOKEN_SYMBOL:
        switch (GPOINTER_TO_INT (scanner->value.v_symbol)) {
          case CUBE_TITLE:
            cube_lut_parse_title (scanner, &title);
            GST_INFO ("TITLE: %s", title);
            break;
          case CUBE_DOMAIN_MIN:
            if (!cube_lut_parse_domain (scanner, domain_min)) {
              return NULL;
            }
            break;
          case CUBE_DOMAIN_MAX:
            if (!cube_lut_parse_domain (scanner, domain_max)) {
              return NULL;
            }
            break;
          case CUBE_LUT_1D_SIZE:
            lut_type = CUBE_LUT_1D;
          case CUBE_LUT_3D_SIZE:
            lut_type = CUBE_LUT_3D;
            if (lut_size != 0) {
              GST_ERROR ("LUT_SIZE defined repeatedly, invalid cube file");
              return NULL;
            } else if (!cube_lut_parse_lut_size (scanner, &lut_size)) {
              return NULL;
            } else {
              lut = cube_lut_new (lut_type, lut_size);
            }
            break;
        }
        break;
      case G_TOKEN_FLOAT:
        if (lut == NULL) {
          GST_ERROR ("LUT_SIZE undefined, invalid cube file");
          return NULL;
        }
        lut->table[idx] = scanner->value.v_float;
        idx++;

        if (idx > lut->length) {
          GST_ERROR ("LUT seems bigger than expected, invalid cube file");
          cube_lut_free (lut);
          return NULL;
        }
        break;
      default:
        break;
    }
  }

  lut->title = title;

  for (gint i = 0; i < 3; i++) {
    lut->domain_min[i] = domain_min[i];
    lut->domain_max[i] = domain_max[i];
  }

  g_scanner_destroy (scanner);
  close (fd);

  return lut;
}
