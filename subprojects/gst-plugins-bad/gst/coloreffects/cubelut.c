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


GST_DEBUG_CATEGORY_STATIC (cubelut_debug);
#define GST_CAT_DEFAULT (cubelut_debug)

enum
{
  PROP_FILENAME = 1,
  PROP_INTERP,
  PROP_PRECOMP,
  N_PROPERTIES
};

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

typedef void (*CubeLUTInterpFunc) (CubeLUT * lut, const gdouble in[],
    gdouble out[]);

#define CUBE_LUT_TYPE_INTERP (cube_lut_interp_get_type())
static GType
cube_lut_interp_get_type (void)
{
  static GType interp_type = 0;

  static const GEnumValue interps[] = {
    {CUBE_LUT_INTERP_NEAREST,
        "Nearest Neighbour", "nearest"},
    {CUBE_LUT_INTERP_TRILINEAR,
        "Trilinear", "trilinear"},
    {CUBE_LUT_INTERP_TETRAHEDRAL,
        "Tetrahedral", "tetrahedral"},
    {0, NULL, NULL},
  };

  if (!interp_type) {
    interp_type = g_enum_register_static ("CubeLUTInterp", interps);
  }
  return interp_type;
}

/*
 * Symbol tokens for GScanner
 * one for each valid keyword in the Cube file spec
 */
enum
{
  CUBE_TITLE = G_TOKEN_LAST + 1,
  CUBE_LUT_1D_SIZE,
  CUBE_LUT_3D_SIZE,
  CUBE_DOMAIN_MIN,
  CUBE_DOMAIN_MAX
};

typedef enum
{
  CUBE_LUT_1D,
  CUBE_LUT_3D,
} CubeLUTType;

/*
 * CubeLUT private members
 */
struct _CubeLUT
{
  /* private */
  GObject parent_instance;

  GScanner *scanner;
  const gchar *filename;
  gchar *title;
  CubeLUTType type;
  gint size;
  glong length;
  gdouble domain_min[3];
  gdouble domain_max[3];

  gdouble *table;
  CubeLUTInterpType interp_type;
  CubeLUTInterpFunc interp_func;

  gboolean precomp;
  guint8 *precomp_table;

  gpointer padding[16];
};

G_DEFINE_TYPE (CubeLUT, cube_lut, G_TYPE_OBJECT)
/*
  CubeLUT serves mainly two purposes:
  - parsing Cube files
  - looking up rgb data from the 3D LUT using preferred interpolation method

  The first section includes public and helper methods for 3D LUT lookup and interpolation
  The second section includes helper function for Cube parsing
*/
/* 3D LUT look-up and interpolation methods */
#define ROUND(x) ((int) ((x) + .5))
#define PREV(x) ((int) floor(x))
#define NEXT(x) (MIN((int) ceil(x), lut->size - 1))
/**
 * transform rgb pixel either interpolating from the LUT or looking up
 * from the precomputed table
 */
     void cube_lut_transform (CubeLUT * lut, guint8 in[], guint8 out[])
{
  int i;
  gdouble scaled_in[3];
  gdouble scaled_out[3];

  if ((lut->precomp) && (lut->precomp_table != NULL)) {
    for (i = 0; i < 3; i++) {
      out[i] =
          lut->precomp_table[(in[0] + in[1] * 256 + in[2] * 256 * 256) * 3 + i];
    }
  } else {
    for (i = 0; i < 3; i++) {
      scaled_in[i] = (in[i] / 255.) * (lut->size - 1);
    }

    lut->interp_func (lut, scaled_in, scaled_out);

    for (i = 0; i < 3; i++) {
      out[i] = (guint8) (255. * CLAMP (scaled_out[i], 0., 1.));
    }
  }
}

static inline float
lerp (gdouble x0, gdouble x1, gdouble x)
{
  return x0 + (x1 - x0) * x;
}

static gdouble
cube_lut_lookup (CubeLUT * lut, gint r, gint g, gint b, gint comp)
{
  glong index = b * lut->size * lut->size + g * lut->size + r;
  return lut->table[index * 3 + comp];
}

/* Nearest Neighbour interpolation */
static void
cube_lut_interp_nearest (CubeLUT * lut, const gdouble in[], gdouble out[])
{
  gint i;
  for (i = 0; i < 3; i++) {
    out[i] =
        cube_lut_lookup (lut, ROUND (in[0]), ROUND (in[1]), ROUND (in[2]), i);
  }
}

/* Trilinear interpolation
   https://en.m.wikipedia.org/wiki/Trilinear_interpolation
 */
static void
cube_lut_interp_trilinear (CubeLUT * lut, const gdouble in[], gdouble out[])
{
  gint i;
  gint prev[3];
  gint next[3];

  gdouble d[3];
  gdouble c000[3], c001[3], c010[3], c011[3];
  gdouble c100[3], c101[3], c110[3], c111[3];
  gdouble c00[3], c10[3], c01[3], c11[3];
  gdouble c0[3], c1[3];

  for (i = 0; i < 3; i++) {
    prev[i] = PREV (in[i]);
    next[i] = NEXT (in[i]);
    d[i] = in[i] - prev[i];
  }

  for (i = 0; i < 3; i++) {
    c000[i] = cube_lut_lookup (lut, prev[0], prev[1], prev[2], i);
    c001[i] = cube_lut_lookup (lut, prev[0], prev[1], next[2], i);
    c010[i] = cube_lut_lookup (lut, prev[0], next[1], prev[2], i);
    c011[i] = cube_lut_lookup (lut, prev[0], next[1], next[2], i);
    c100[i] = cube_lut_lookup (lut, next[0], prev[1], prev[2], i);
    c101[i] = cube_lut_lookup (lut, next[0], prev[1], next[2], i);
    c110[i] = cube_lut_lookup (lut, next[0], next[1], prev[2], i);
    c111[i] = cube_lut_lookup (lut, next[0], next[1], next[2], i);

    c00[i] = lerp (c000[i], c100[i], d[0]);
    c10[i] = lerp (c010[i], c110[i], d[0]);
    c01[i] = lerp (c001[i], c101[i], d[0]);
    c11[i] = lerp (c011[i], c111[i], d[0]);

    c0[i] = lerp (c00[i], c10[i], d[1]);
    c1[i] = lerp (c01[i], c11[i], d[1]);

    out[i] = lerp (c0[i], c1[i], d[2]);
  }
}

/* Tetrahedral interpolation

   This should be the preferred interpolation method as per the Adobe
   spec. They don't give any detail about the implementation.

   Below are some references, best I could find, the Nvidia GTC poster
   has a nice quick summary of the algorithm. There's probably room
   for optimization.

   James M. Kasson, Wil Plouffe, Sigfredo I. Nin, "Tetrahedral
   interpolation technique for color space conversion," Proc. SPIE
   1909, Device-Independent Color Imaging and Imaging Systems
   Integration, (4 August 1993)

   H. Lee, K. Kim and D. Han, "A real time color gamut mapping using
   tetrahedral interpolation for digital tv color reproduction
   enhancement," in IEEE Transactions on Consumer Electronics,
   vol. 55, no. 2, pp. 599-605

   https://www.nvidia.com/content/GTC/posters/2010/V01-Real-Time-Color-Space-Conversion-for-High-Resolution-Video.pdf */
static void
cube_lut_interp_tetrahedral (CubeLUT * lut, const gdouble in[], gdouble out[])
{
  gint i;
  gint prev[3];
  gint next[3];

  gdouble d[3];
  gdouble c000[3], c001[3], c010[3], c011[3];
  gdouble c100[3], c101[3], c110[3], c111[3];

  for (i = 0; i < 3; i++) {
    prev[i] = PREV (in[i]);
    next[i] = NEXT (in[i]);
    d[i] = in[i] - prev[i];
  }

  for (i = 0; i < 3; i++) {
    /* look up neighbour vertices */
    /* we could probably save some lookup here, c000 and c111 are
     * always needed, the other ones could go inside branches */
    c000[i] = cube_lut_lookup (lut, prev[0], prev[1], prev[2], i);
    c001[i] = cube_lut_lookup (lut, prev[0], prev[1], next[2], i);
    c010[i] = cube_lut_lookup (lut, prev[0], next[1], prev[2], i);
    c011[i] = cube_lut_lookup (lut, prev[0], next[1], next[2], i);
    c100[i] = cube_lut_lookup (lut, next[0], prev[1], prev[2], i);
    c101[i] = cube_lut_lookup (lut, next[0], prev[1], next[2], i);
    c110[i] = cube_lut_lookup (lut, next[0], next[1], prev[2], i);
    c111[i] = cube_lut_lookup (lut, next[0], next[1], next[2], i);


    if (d[0] > d[1]) {
      if (d[1] > d[2]) {
        out[i] =
            (1 - d[0]) * c000[i] + (d[0] - d[1]) * c100[i] + (d[1] -
            d[2]) * c110[i] + d[2] * c111[i];
      } else if (d[0] > d[2]) {
        out[i] =
            (1 - d[0]) * c000[i] + (d[0] - d[2]) * c100[i] + (d[2] -
            d[1]) * c101[i] + d[1] * c111[i];
      } else {
        out[i] =
            (1 - d[2]) * c000[i] + (d[2] - d[0]) * c001[i] + (d[0] -
            d[1]) * c101[i] + d[1] * c111[i];
      }
    } else {
      if (d[2] > d[1]) {
        out[i] =
            (1 - d[2]) * c000[i] + (d[2] - d[1]) * c001[i] + (d[1] -
            d[0]) * c011[i] + d[0] * c111[i];
      } else if (d[2] > d[0]) {
        out[i] =
            (1 - d[1]) * c000[i] + (d[1] - d[2]) * c010[i] + (d[2] -
            d[0]) * c011[i] + d[0] * c111[i];
      } else {
        out[i] =
            (1 - d[1]) * c000[i] + (d[1] - d[0]) * c010[i] + (d[0] -
            d[2]) * c110[i] + d[2] * c111[i];
      }
    }
  }
}



/* 3D Cube file parsing */

static const gboolean
cube_lut_parse_title (CubeLUT * lut, GScanner * scanner)
{
  if (g_scanner_get_next_token (scanner) != G_TOKEN_STRING) {
    g_scanner_unexp_token (scanner, G_TOKEN_STRING, NULL, NULL, NULL,
        "invalid TITLE", TRUE);
    return FALSE;
  } else {
    lut->title = g_strdup (scanner->value.v_string);
    return TRUE;
  }
}

static gboolean
cube_lut_parse_domain (CubeLUT * lut, GScanner * scanner)
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
          lut->domain_min[i] = scanner->value.v_float;
          break;
        case CUBE_DOMAIN_MAX:
          if (scanner->value.v_float != 1) {
            GST_WARNING
                ("unsupported DOMAIN_MAX %f, we only support domains in the [0, 1] range",
                scanner->value.v_float);
            return FALSE;
          }
          lut->domain_max[i] = scanner->value.v_float;
          break;
        default:
          g_assert_not_reached ();
      }
    }
  }

  return TRUE;
}

static gboolean
cube_lut_parse_lut_size (CubeLUT * lut, GScanner * scanner)
{
  if (g_scanner_get_next_token (scanner) != G_TOKEN_FLOAT) {
    g_scanner_unexp_token (scanner, G_TOKEN_FLOAT, NULL, NULL, NULL,
        "invalid LUT SIZE", TRUE);
    return FALSE;
  } else {
    lut->size = (glong) scanner->value.v_float;
    return TRUE;
  }
}

static void
cube_lut_init_table (CubeLUT * lut)
{
  switch (lut->type) {
    case CUBE_LUT_1D:
      if ((lut->size < 2) || (lut->size > 65536)) {
        GST_ERROR ("LUT_1D_size outside valid range");
      } else {
        lut->title = NULL;
        lut->length = lut->size * 3;
        GST_INFO ("allocating table of len: %ld", lut->length);
        lut->table = g_new0 (gdouble, lut->length);
      }
      break;
    case CUBE_LUT_3D:
      if ((lut->size < 2) || (lut->size > 256)) {
        GST_ERROR ("LUT_3D_SIZE outside valid range");
      } else {
        lut->title = NULL;
        lut->length = lut->size * lut->size * lut->size * 3;
        GST_INFO ("allocating table of len: %ld", lut->length);
        lut->table = g_new0 (gdouble, lut->length);
      }
      break;
    default:
      g_assert_not_reached ();
  }

  GST_INFO ("LUT TYPE: %s", lut->type == CUBE_LUT_1D ? "1D" : "3D");
  GST_INFO ("LUT SIZE: %d", (gint) lut->size);
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

static void
cube_lut_precompute_table (CubeLUT * lut)
{
  GST_INFO ("precomputing the full 3D LUT for 24bit RGB colorspace");

  if (lut->precomp_table == NULL) {
    lut->precomp_table = g_new0 (guint8, 256 * 256 * 256 * 3);
  }
  for (guint i = 0; i < 256; i++) {
    for (guint j = 0; j < 256; j++) {
      for (guint k = 0; k < 256; k++) {
        gdouble in[] = {
          (i / 255.) * (lut->size - 1),
          (j / 255.) * (lut->size - 1),
          (k / 255.) * (lut->size - 1)
        };

        gdouble out[3];
        lut->interp_func (lut, in, out);

        /* GST_INFO("%d %d %d -> %.1f %.1f %.1f", */
        /*          i, j, k, */
        /*          out[0], out[1], out[2]); */

        for (gint l = 0; l < 3; l++) {
          lut->precomp_table[(i + j * 256 + k * 256 * 256) * 3 + l] =
              (guint8) (255. * CLAMP (out[l], 0., 1.));
        }
      }
    }
  }
}

static void
cube_lut_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  CubeLUT *lut = CUBE_LUT (object);

  switch (prop_id) {
    case PROP_FILENAME:
      lut->filename = g_value_get_string (value);
      break;
    case PROP_INTERP:
      lut->interp_type = g_value_get_enum (value);
      GST_INFO ("interpolation: %s", g_enum_to_string (CUBE_LUT_TYPE_INTERP,
              lut->interp_type));
      switch (lut->interp_type) {
        case CUBE_LUT_INTERP_NEAREST:
          lut->interp_func = cube_lut_interp_nearest;
          break;
        case CUBE_LUT_INTERP_TRILINEAR:
          lut->interp_func = cube_lut_interp_trilinear;
          break;
        case CUBE_LUT_INTERP_TETRAHEDRAL:
          lut->interp_func = cube_lut_interp_tetrahedral;
          break;
        default:
          g_assert_not_reached ();
      }
      break;
    case PROP_PRECOMP:
      lut->precomp = g_value_get_boolean (value);
      if (lut->precomp) {
        cube_lut_precompute_table (lut);
      } else {
        g_free (lut->precomp_table);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
cube_lut_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  CubeLUT *lut = CUBE_LUT (object);


  switch (prop_id) {
    case PROP_FILENAME:
      g_value_set_string (value, lut->filename);
      break;
    case PROP_INTERP:
      g_value_set_enum (value, lut->interp_type);
      break;
    case PROP_PRECOMP:
      g_value_set_boolean (value, lut->precomp);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
cube_lut_finalize (GObject * object)
{
  CubeLUT *lut = CUBE_LUT (object);

  g_free (lut->table);
  g_free (lut->title);
  g_free (lut->precomp_table);
}

static void
cube_lut_class_init (CubeLUTClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (cubelut_debug, "cubelut", 0, "cubelut");

  object_class->get_property = cube_lut_get_property;
  object_class->set_property = cube_lut_set_property;
  object_class->finalize = cube_lut_finalize;

  obj_properties[PROP_FILENAME] =
      g_param_spec_string ("filename",
      "Filename",
      "Cube LUT Filename",
      NULL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  obj_properties[PROP_INTERP] =
      g_param_spec_enum ("interpolation",
      "Interpolation",
      "3D Cube interpolation type",
      CUBE_LUT_TYPE_INTERP,
      CUBE_LUT_INTERP_TETRAHEDRAL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  obj_properties[PROP_PRECOMP] =
      g_param_spec_boolean ("precomp", "precompute LUT",
      "Precompute LUT interpolating table over the whole RGB colorspace", FALSE,
      G_PARAM_READWRITE);

  g_object_class_install_properties (object_class,
      N_PROPERTIES, obj_properties);
}

static void
cube_lut_init (CubeLUT * lut)
{
  lut->table = NULL;
  lut->size = 0;
  lut->length = 0;
  lut->interp_type = CUBE_LUT_INTERP_TETRAHEDRAL;
  lut->interp_func = cube_lut_interp_tetrahedral;
  lut->precomp = FALSE;
  lut->precomp_table = NULL;
}

CubeLUT *
cube_lut_new (const gchar * filename)
{
  CubeLUT *lut;
  GScanner *scanner;

  gint fd;
  glong idx = 0;

  lut = g_object_new (CUBE_TYPE_LUT, "filename", filename, NULL);

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
            cube_lut_parse_title (lut, scanner);
            GST_INFO ("TITLE: %s", lut->title);
            break;
          case CUBE_DOMAIN_MIN:
            if (!cube_lut_parse_domain (lut, scanner)) {
              g_object_unref (lut);
              return NULL;
            } else {
              GST_INFO ("DOMAIN_MIN: %.1f, %.1f, %.1f",
                  lut->domain_min[0], lut->domain_min[1], lut->domain_min[2]);
            }
            break;
          case CUBE_DOMAIN_MAX:
            if (!cube_lut_parse_domain (lut, scanner)) {
              return NULL;
            } else {
              GST_INFO ("DOMAIN_MAX: %.1f, %.1f, %.1f",
                  lut->domain_max[0], lut->domain_max[1], lut->domain_max[2]);
            }
            break;
          case CUBE_LUT_1D_SIZE:
            lut->type = CUBE_LUT_1D;
          case CUBE_LUT_3D_SIZE:
            lut->type = CUBE_LUT_3D;
            if (lut->size != 0) {
              GST_ERROR ("LUT_SIZE defined repeatedly, invalid cube file");
              g_object_unref (lut);
              return NULL;
            } else if (!cube_lut_parse_lut_size (lut, scanner)) {
              g_object_unref (lut);
              return NULL;
            } else {
              cube_lut_init_table (lut);
            }
            break;
        }
        break;
      case G_TOKEN_FLOAT:
        if (lut->table == NULL) {
          GST_ERROR ("LUT_SIZE undefined, invalid cube file");
          g_object_unref (lut);
          return NULL;
        }
        lut->table[idx] = scanner->value.v_float;
        idx++;

        if (idx > lut->length) {
          GST_ERROR ("LUT seems bigger than expected, invalid cube file");
          g_object_unref (lut);
          return NULL;
        }
        break;
      default:
        break;
    }
  }

  g_scanner_destroy (scanner);
  close (fd);

  return lut;
}
