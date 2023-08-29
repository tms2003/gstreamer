/* GStreamer
 * Copyright (C) 2023 Seungha Yang <seungha@centricular.com>
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

#include <gst/gst.h>
#include <gst/video/video-prelude.h>

G_BEGIN_DECLS

typedef struct _GstTextAttr GstTextAttr;
typedef struct _GstTextColor GstTextColor;
typedef struct _GstTextLayout GstTextLayout;
typedef struct _GstTextAttrIterator GstTextAttrIterator;

#define GST_TYPE_TEXT_LAYOUT      (gst_text_layout_get_type())
#define GST_IS_TEXT_LAYOUT(obj)   (GST_IS_MINI_OBJECT_TYPE (obj, GST_TYPE_TEXT_LAYOUT))

/**
 * GstTextAttr:
 *
 * Opaque text attribute struct
 *
 * Since: 1.24
 */

/**
 * GstTextAttrType:
 *
 * Text attribute type
 *
 * Since: 1.24
 */
typedef enum
{
  /**
   * GST_TEXT_ATTR_INVALID:
   *
   * Invalid attribute
   *
   * Since:1.24
   */
  GST_TEXT_ATTR_INVALID,

  /**
   * GST_TEXT_ATTR_FONT_FAMILY:
   *
   * Font family attribute.
   * Use gst_text_attr_string_new() to create an attribute
   *
   * Since: 1.24
   */
  GST_TEXT_ATTR_FONT_FAMILY,

  /**
   * GST_TEXT_ATTR_FONT_SIZE:
   *
   * Font size attribute.
   * Use gst_text_attr_double_new() to create an attribute
   *
   * Since: 1.24
   */
  GST_TEXT_ATTR_FONT_SIZE,

  /**
   * GST_TEXT_ATTR_FONT_WEIGHT:
   *
   * Font weight attribute.
   * Use gst_text_attr_int_new() to create an attribute
   *
   * Since: 1.24
   */
  GST_TEXT_ATTR_FONT_WEIGHT,

  /**
   * GST_TEXT_ATTR_FONT_STYLE:
   *
   * Font style attribute.
   * Use gst_text_attr_int_new() to create an attribute
   *
   * Since: 1.24
   */
  GST_TEXT_ATTR_FONT_STYLE,

  /**
   * GST_TEXT_ATTR_FONT_STRETCH:
   *
   * Font stretch attribute.
   * Use gst_text_attr_int_new() to create an attribute
   *
   * Since: 1.24
   */
  GST_TEXT_ATTR_FONT_STRETCH,

  /**
   * GST_TEXT_ATTR_UNDERLINE:
   *
   * Underline attribute.
   * Use gst_text_attr_int_new() to create an attribute
   *
   * Since: 1.24
   */
  GST_TEXT_ATTR_UNDERLINE,

  /**
   * GST_TEXT_ATTR_STRIKETHROUGH:
   *
   * Strikethrough attribute.
   * Use gst_text_attr_int_new() to create an attribute
   *
   * Since: 1.24
   */
  GST_TEXT_ATTR_STRIKETHROUGH,

  /**
   * GST_TEXT_ATTR_FOREGROUND_COLOR:
   *
   * Foreground color attribute.
   * Use gst_text_attr_color_new() to create an attribute
   *
   * Since: 1.24
   */
  GST_TEXT_ATTR_FOREGROUND_COLOR,

  /**
   * GST_TEXT_ATTR_BACKGROUND_COLOR:
   *
   * Background color attribute.
   * Use gst_text_attr_color_new() to create an attribute
   *
   * Since: 1.24
   */
  GST_TEXT_ATTR_BACKGROUND_COLOR,

  /**
   * GST_TEXT_ATTR_OUTLINE_COLOR:
   *
   * Outline color attribute.
   * Use gst_text_attr_color_new() to create an attribute
   *
   * Since: 1.24
   */
  GST_TEXT_ATTR_OUTLINE_COLOR,

  /**
   * GST_TEXT_ATTR_UNDERLINE_COLOR:
   *
   * Underline color attribute.
   * Use gst_text_attr_color_new() to create an attribute
   *
   * Since: 1.24
   */
  GST_TEXT_ATTR_UNDERLINE_COLOR,

  /**
   * GST_TEXT_ATTR_STRIKETHROUGH_COLOR:
   *
   * Strikethrough color attribute.
   * Use gst_text_attr_color_new() to create an attribute
   *
   * Since: 1.24
   */
  GST_TEXT_ATTR_STRIKETHROUGH_COLOR,

  /**
   * GST_TEXT_ATTR_SHADOW_COLOR:
   *
   * Shadow color attribute.
   * Use gst_text_attr_color_new() to create an attribute
   *
   * Since: 1.24
   */
  GST_TEXT_ATTR_SHADOW_COLOR,
} GstTextAttrType;

/**
 * GstTextLayout:
 *
 * Opaque struct representing text layout
 *
 * Since: 1.24
 */

/**
 * GstWordWrapMode:
 *
 * Word wrapping mode
 *
 * Since: 1.24
 */
typedef enum
{
  /**
   * GST_WORD_WRAP_UNKNOWN:
   *
   * Unknown word wrap mode
   *
   * Since: 1.24
   */
  GST_WORD_WRAP_UNKNOWN,

  /**
   * GST_WORD_WRAP_WORD:
   *
   * Words are broken across lines
   *
   * Since: 1.24
   */
  GST_WORD_WRAP_WORD,

  /**
   * GST_WORD_WRAP_CHAR:
   *
   * Characters are broken across lines
   *
   * Since: 1.24
   */
  GST_WORD_WRAP_CHAR,

  /**
   * GST_WORD_WRAP_NO_WRAP:
   *
   * Words are kept within the same line
   *
   * Since: 1.24
   */
  GST_WORD_WRAP_NO_WRAP,
} GstWordWrapMode;

/**
 * GstTextAlignment:
 *
 * Text alignment mode
 *
 * Since: 1.24
 */
typedef enum
{
  /**
   * GST_TEXT_ALIGNMENT_UNKNOWN:
   *
   * Unknown text alignment
   *
   * Since: 1.24
   */
  GST_TEXT_ALIGNMENT_UNKNOWN,

  /**
   * GST_TEXT_ALIGNMENT_LEADING:
   *
   * The text is aligned to the left edge of the layout box
   *
   * Since: 1.24
   */
  GST_TEXT_ALIGNMENT_LEFT,

  /**
   * GST_TEXT_ALIGNMENT_CENTER:
   *
   * The center of the paragrapha text is aligned to the center of the layout box
   *
   * Since: 1.24
   */
  GST_TEXT_ALIGNMENT_CENTER,

  /**
   * GST_TEXT_ALIGNMENT_RIGHT:
   *
   * The text is aligned to the right edge of the layout box
   *
   * Since: 1.24
   */
  GST_TEXT_ALIGNMENT_RIGHT,

  /**
   * GST_TEXT_ALIGNMENT_JUSTIFIED:
   *
   * Align text to the left, and also justify text to fill the lines
   *
   * Since: 1.24
   */
  GST_TEXT_ALIGNMENT_JUSTIFIED,
} GstTextAlignment;

/**
 * GstParagraphAlignment:
 *
 * Paragrapha alignment mode
 *
 * Since: 1.24
 */
typedef enum
{
  /**
   * GST_PARAGRAPH_ALIGNMENT_UNKNOWN:
   *
   * Unknown paragraph alignment
   *
   * Since: 1.24
   */
  GST_PARAGRAPH_ALIGNMENT_UNKNOWN,

  /**
   * GST_PARAGRAPH_ALIGNMENT_TOP:
   *
   * The top of the text flow is aligned to the top edge of the layout box
   *
   * Since: 1.24
   */
  GST_PARAGRAPH_ALIGNMENT_TOP,

  /**
   * GST_PARAGRAPH_ALIGNMENT_CENTER:
   *
   * The center of the text flow is aligned to the center edge of the layout box
   *
   * Since: 1.24
   */
  GST_PARAGRAPH_ALIGNMENT_CENTER,

  /**
   * GST_PARAGRAPH_ALIGNMENT_BOTTOM:
   *
   * The bottom of the text flow is aligned to the bottom edge of the layout box
   *
   * Since: 1.24
   */
  GST_PARAGRAPH_ALIGNMENT_BOTTOM,
} GstParagraphAlignment;

/**
 * GstFontWeight:
 *
 * Represents the density of a typeface, correspond to the usWeightClass
 * definition in the OpenType specification
 *
 * Since: 1.24
 */
typedef enum
{
  /**
   * GST_FONT_WEIGHT_THIN:
   *
   * Predefined font weight value 100
   *
   * Since: 1.24
   */
  GST_FONT_WEIGHT_THIN = 100,

  /**
   * GST_FONT_WEIGHT_ULTRA_LIGHT:
   *
   * Predefined font weight value 200
   *
   * Since: 1.24
   */
  GST_FONT_WEIGHT_ULTRA_LIGHT = 200,

  /**
   * GST_FONT_WEIGHT_LIGHT:
   *
   * Predefined font weight value 300
   *
   * Since: 1.24
   */
  GST_FONT_WEIGHT_LIGHT = 300,

  /**
   * GST_FONT_WEIGHT_SEMI_LIGHT:
   *
   * Predefined font weight value 350
   *
   * Since: 1.24
   */
  GST_FONT_WEIGHT_SEMI_LIGHT = 350,

  /**
   * GST_FONT_WEIGHT_NORMAL:
   *
   * Predefined font weight value 400
   *
   * Since: 1.24
   */
  GST_FONT_WEIGHT_NORMAL = 400,

  /**
   * GST_FONT_WEIGHT_MEDIUM:
   *
   * Predefined font weight value 500
   *
   * Since: 1.24
   */
  GST_FONT_WEIGHT_MEDIUM = 500,

  /**
   * GST_FONT_WEIGHT_SEMI_BOLD:
   *
   * Predefined font weight value 600
   *
   * Since: 1.24
   */
  GST_FONT_WEIGHT_SEMI_BOLD = 600,

  /**
   * GST_FONT_WEIGHT_BOLD:
   *
   * Predefined font weight value 700
   *
   * Since: 1.24
   */
  GST_FONT_WEIGHT_BOLD = 700,

  /**
   * GST_FONT_WEIGHT_ULTRA_BOLD:
   *
   * Predefined font weight value 800
   *
   * Since: 1.24
   */
  GST_FONT_WEIGHT_ULTRA_BOLD = 800,

  /**
   * GST_FONT_WEIGHT_HEAVY:
   *
   * Predefined font weight value 900
   *
   * Since: 1.24
   */
  GST_FONT_WEIGHT_HEAVY = 900,

  /**
   * GST_FONT_WEIGHT_ULTRA_BLACK:
   *
   * Predefined font weight value 950
   *
   * Since: 1.24
   */
  GST_FONT_WEIGHT_ULTRA_BLACK = 950,
} GstFontWeight;

/**
 * GstFontStyle:
 *
 * Font style
 *
 * Since: 1.24
 */
typedef enum
{
  /**
   * GST_FONT_STYLE_NORMAL:
   *
   * Normal font style
   *
   * Since: 1.24
   */
  GST_FONT_STYLE_NORMAL,

  /**
   * GST_FONT_STYLE_OBLIQUE:
   *
   * Oblique font style
   *
   * Since: 1.24
   */
  GST_FONT_STYLE_OBLIQUE,

  /**
   * GST_FONT_STYLE_ITALIC:
   *
   * Italic font style
   *
   * Since: 1.24
   */
  GST_FONT_STYLE_ITALIC,
} GstFontStyle;

/**
 * GstFontStretch:
 *
 * Font stretch correspond to the usWidthClass definition in the OpenType
 * specification
 *
 * Since: 1.24
 */
typedef enum
{
  /**
   * GST_FONT_STRETCH_ULTRA_CONDENSED:
   *
   * Predefined font stretch value 1
   *
   * Since: 1.24
   */
  GST_FONT_STRETCH_ULTRA_CONDENSED = 1,

  /**
   * GST_FONT_STRETCH_EXTRA_CONDENSED:
   *
   * Predefined font stretch value 2
   *
   * Since: 1.24
   */
  GST_FONT_STRETCH_EXTRA_CONDENSED = 2,

  /**
   * GST_FONT_STRETCH_CONDENSED:
   *
   * Predefined font stretch value 3
   *
   * Since: 1.24
   */
  GST_FONT_STRETCH_CONDENSED = 3,

  /**
   * GST_FONT_STRETCH_SEMI_CONDENSED:
   *
   * Predefined font stretch value 4
   *
   * Since: 1.24
   */
  GST_FONT_STRETCH_SEMI_CONDENSED = 4,

  /**
   * GST_FONT_STRETCH_NORMAL:
   *
   * Predefined font stretch value 5
   *
   * Since: 1.24
   */
  GST_FONT_STRETCH_NORMAL = 5,

  /**
   * GST_FONT_STRETCH_SEMI_EXPANDED:
   *
   * Predefined font stretch value 6
   *
   * Since: 1.24
   */
  GST_FONT_STRETCH_SEMI_EXPANDED = 6,

  /**
   * GST_FONT_STRETCH_EXPANDED:
   *
   * Predefined font stretch value 7
   *
   * Since: 1.24
   */
  GST_FONT_STRETCH_EXPANDED = 7,

  /**
   * GST_FONT_STRETCH_EXTRA_EXPANDED:
   *
   * Predefined font stretch value 8
   *
   * Since: 1.24
   */
  GST_FONT_STRETCH_EXTRA_EXPANDED = 8,

  /**
   * GST_FONT_STRETCH_EXTRA_EXPANDED:
   *
   * Predefined font stretch value 9
   *
   * Since: 1.24
   */
  GST_FONT_STRETCH_ULTRA_EXPANDED = 9,
} GstFontStretch;

/**
 * GstTextUnderline:
 *
 * Underline type
 *
 * Since: 1.24
 */
typedef enum
{
  /**
   * GST_TEXT_UNDERLINE_NONE:
   *
   * Disables underline
   *
   * Since: 1.24
   */
  GST_TEXT_UNDERLINE_NONE,

  /**
   * GST_TEXT_UNDERLINE_SINGLE:
   *
   * Single underline
   *
   * Since: 1.24
   */
  GST_TEXT_UNDERLINE_SINGLE,

  /**
   * GST_TEXT_UNDERLINE_DOUBLE:
   *
   * Double underline
   *
   * Since: 1.24
   */
  GST_TEXT_UNDERLINE_DOUBLE,
} GstTextUnderline;

/**
 * GstTextStrikethrough:
 *
 * Strikethrough type
 *
 * Since: 1.24
 */
typedef enum
{
  /**
   * GST_TEXT_STRIKETHROUGH_NONE:
   *
   * Disables strikethrough
   *
   * Since: 1.24
   */
  GST_TEXT_STRIKETHROUGH_NONE,

  /**
   * GST_TEXT_STRIKETHROUGH_SINGLE:
   *
   * Single strikethrough
   *
   * Since: 1.24
   */
  GST_TEXT_STRIKETHROUGH_SINGLE,
} GstTextStrikethrough;

/**
 * GstTextColor:
 * @red: The red component of the text brush color
 * @green: The green component of the text brush color
 * @blue: The blue component of the text brush color
 * @alpha: The alpha component of the text brush color
 *
 * Represent brush color
 *
 * Since: 1.24
 */
struct _GstTextColor
{
  guint16 red;
  guint16 green;
  guint16 blue;
  guint16 alpha;
};

GST_VIDEO_API
GstTextAttr * gst_text_attr_string_new (const gchar * value,
                                        GstTextAttrType type,
                                        guint start,
                                        guint length);

GST_VIDEO_API
GstTextAttr * gst_text_attr_double_new (gdouble value,
                                        GstTextAttrType type,
                                        guint start,
                                        guint length);

GST_VIDEO_API
GstTextAttr * gst_text_attr_int_new    (gint value,
                                        GstTextAttrType type,
                                        guint start,
                                        guint length);

GST_VIDEO_API
GstTextAttr * gst_text_attr_color_new  (const GstTextColor * value,
                                        GstTextAttrType type,
                                        guint start,
                                        guint length);

GST_VIDEO_API
GstTextAttrType gst_text_attr_identify  (const GstTextAttr * attr,
                                         guint * start,
                                         guint * length);

GST_VIDEO_API
gboolean gst_text_layout_attr_get_string (const GstTextAttr * attr,
                                          const gchar ** value);

GST_VIDEO_API
gboolean gst_text_layout_attr_get_double (const GstTextAttr * attr,
                                          gdouble * value);

GST_VIDEO_API
gboolean gst_text_layout_attr_get_int    (const GstTextAttr * attr,
                                          gint * value);

GST_VIDEO_API
gboolean gst_text_layout_attr_get_color  (const GstTextAttr * attr,
                                          GstTextColor * value);

GST_VIDEO_API
GstTextAttr * gst_text_attr_copy  (GstTextAttr * attr);

GST_VIDEO_API
void          gst_text_attr_free  (GstTextAttr * attr);

GST_VIDEO_API
GType           gst_text_layout_get_type   (void);

GST_VIDEO_API
GstTextLayout * gst_text_layout_new        (const gchar * text);

GST_VIDEO_API
GstTextLayout * gst_text_layout_ref        (GstTextLayout * layout);

GST_VIDEO_API
void            gst_text_layout_unref      (GstTextLayout * layout);

GST_VIDEO_API
GstTextLayout * gst_text_layout_copy       (GstTextLayout * layout);

GST_VIDEO_API
void            gst_clear_text_layout      (GstTextLayout ** layout);

GST_VIDEO_API
GstTextLayout * gst_text_layout_concat     (GstTextLayout * layout1,
                                            GstTextLayout * layout2,
                                            const gchar * glue);

GST_VIDEO_API
const gchar *   gst_text_layout_get_text   (GstTextLayout * layout);

GST_VIDEO_API
gboolean        gst_text_layout_set_width  (GstTextLayout * layout,
                                            guint width);

GST_VIDEO_API
guint           gst_text_layout_get_width  (GstTextLayout * layout);

GST_VIDEO_API
gboolean        gst_text_layout_set_height (GstTextLayout * layout,
                                            guint height);

GST_VIDEO_API
guint           gst_text_layout_get_height (GstTextLayout * layout);

GST_VIDEO_API
gboolean        gst_text_layout_set_xpos (GstTextLayout * layout,
                                          gint xpos);

GST_VIDEO_API
gint            gst_text_layout_get_xpos (GstTextLayout * layout);

GST_VIDEO_API
gboolean        gst_text_layout_set_ypos (GstTextLayout * layout,
                                          gint ypos);

GST_VIDEO_API
gint            gst_text_layout_get_ypos (GstTextLayout * layout);

GST_VIDEO_API
gboolean        gst_text_layout_set_word_wrap (GstTextLayout * layout,
                                               GstWordWrapMode wrap_mode);

GST_VIDEO_API
GstWordWrapMode gst_text_layout_get_word_wrap (GstTextLayout * layout);

GST_VIDEO_API
gboolean        gst_text_layout_set_text_alignment (GstTextLayout * layout,
                                                    GstTextAlignment align);

GST_VIDEO_API
GstTextAlignment gst_text_layout_get_text_alignment (GstTextLayout * layout);

GST_VIDEO_API
gboolean              gst_text_layout_set_paragraph_alignment (GstTextLayout * layout,
                                                               GstParagraphAlignment align);

GST_VIDEO_API
GstParagraphAlignment gst_text_layout_get_paragraph_alignment (GstTextLayout * layout);

GST_VIDEO_API
gboolean                gst_text_layout_set_attr (GstTextLayout * layout,
                                                  GstTextAttr * attr);

GST_VIDEO_API
GstTextAttrIterator *  gst_text_layout_get_attr_iterator (GstTextLayout * layout);

GST_VIDEO_API
gboolean               gst_text_attr_iterator_next (GstTextAttrIterator * iter);

GST_VIDEO_API
guint                  gst_text_attr_iterator_get_size (GstTextAttrIterator * iter);

GST_VIDEO_API
GstTextAttr *          gst_text_attr_iterator_get_attr (GstTextAttrIterator * iter,
                                                        guint idx);

GST_VIDEO_API
void                   gst_text_attr_iterator_free (GstTextAttrIterator * iter);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GstTextLayout, gst_text_layout_unref)

G_END_DECLS
