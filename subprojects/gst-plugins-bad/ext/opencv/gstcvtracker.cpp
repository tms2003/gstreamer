/*
 * GStreamer
 * Copyright (C) 2020 Vivek R <123vivekr@gmail.com>
 * Copyright (C) 2021 Cesar Fabian Orccon Chipana <cfoch.fabian@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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
 * SECTION:element-cvtracker
 *
 * Performs object tracking on videos and stores it in video buffer metadata.
 *
 * ## Example launch line
 *
 * ```
 * gst-launch-1.0 v4l2src ! videoconvert ! cvtracker box-x=50 box-y=50 box-wdith=50 box-height=50 ! videoconvert ! xvimagesink
 * ```
 *
 * Since: 1.20
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstcvtracker.h"
#include <gst/analyticmeta/generic/gstanalysismeta.h>
#include <gst/analyticmeta/classification/gstanalysisclassificationmtd.h>
#include <gst/analyticmeta/object_detection/gstobjectdetectionmtd.h>
#include <gst/analyticmeta/tracking/gstobjecttrackingmtd.h>


GST_DEBUG_CATEGORY_STATIC (gst_cvtracker_debug);
#define GST_CAT_DEFAULT gst_cvtracker_debug

#define DEFAULT_PROP_INITIAL_X 50
#define DEFAULT_PROP_INITIAL_Y 50
#define DEFAULT_PROP_INITIAL_WIDTH 50
#define DEFAULT_PROP_INITIAL_HEIGHT 50

enum
{
  PROP_0,
  PROP_INITIAL_X,
  PROP_INITIAL_Y,
  PROP_INITIAL_WIDTH,
  PROP_INITIAL_HEIGHT,
  PROP_ALGORITHM,
  PROP_DRAW,
  PROP_OBJECTS_OF_INTEREST,
  PROP_MAX_UNSEEN_DURATION,
  PROP_MIN_IOU
};

#define GST_OPENCV_TRACKER_ALGORITHM (tracker_algorithm_get_type ())

/**
 * GstOpenCVTrackerAlgorithm:
 *
 * Since: 1.20
 */
static GType
tracker_algorithm_get_type (void)
{
  static GType algorithm = 0;
  static const GEnumValue algorithms[] = {
    {GST_OPENCV_TRACKER_ALGORITHM_BOOSTING, "the Boosting tracker", "Boosting"},
    {GST_OPENCV_TRACKER_ALGORITHM_CSRT, "the CSRT tracker", "CSRT"},
    {GST_OPENCV_TRACKER_ALGORITHM_KCF,
          "the KCF (Kernelized Correlation Filter) tracker",
        "KCF"},
    {GST_OPENCV_TRACKER_ALGORITHM_MEDIANFLOW, "the Median Flow tracker",
        "MedianFlow"},
    {GST_OPENCV_TRACKER_ALGORITHM_MIL, "the MIL tracker", "MIL"},
    {GST_OPENCV_TRACKER_ALGORITHM_MOSSE,
        "the MOSSE (Minimum Output Sum of Squared Error) tracker", "MOSSE"},
    {GST_OPENCV_TRACKER_ALGORITHM_TLD,
          "the TLD (Tracking, learning and detection) tracker",
        "TLD"},
    {0, NULL, NULL},
  };

  if (!algorithm) {
    algorithm =
        g_enum_register_static ("GstOpenCVTrackerAlgorithm", algorithms);
  }
  return algorithm;
}

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("RGB"))
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("RGB"))
    );

G_DEFINE_TYPE_WITH_CODE (GstCVTracker, gst_cvtracker,
    GST_TYPE_OPENCV_VIDEO_FILTER,
    GST_DEBUG_CATEGORY_INIT (gst_cvtracker_debug, "cvtracker", 0,
        "Performs object tracking on videos and stores it in video buffer "
        "metadata"));
GST_ELEMENT_REGISTER_DEFINE (cvtracker, "cvtracker", GST_RANK_NONE,
    GST_TYPE_OPENCV_TRACKER);

static void gst_cvtracker_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_cvtracker_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_cvtracker_transform_ip (GstOpencvVideoFilter
    * filter, GstBuffer * buf, cv::Mat img);

static void
gst_cvtracker_finalize (GObject * obj)
{
  GstCVTracker *filter = GST_OPENCV_TRACKER (obj);

  g_slist_free (g_steal_pointer (&filter->objects_types_of_interest));

  filter->tracker.release ();
  filter->roi.release ();

  G_OBJECT_CLASS (gst_cvtracker_parent_class)->finalize (obj);
}

static void
gst_cvtracker_class_init (GstCVTrackerClass * klass)
{
  GObjectClass *gobject_class;
  GstOpencvVideoFilterClass *gstopencvbasefilter_class;
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gobject_class = (GObjectClass *) klass;
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_cvtracker_finalize);
  gstopencvbasefilter_class = (GstOpencvVideoFilterClass *) klass;

  klass->track_id_seq = (guint64) 0;

  gstopencvbasefilter_class->cv_trans_ip_func = gst_cvtracker_transform_ip;

  gobject_class->set_property = gst_cvtracker_set_property;
  gobject_class->get_property = gst_cvtracker_get_property;

  /*
   * Tracker API in versions older than OpenCV 4.5.1 worked with a ROI based
   * on Rect<double>. However newer versions use Rect<int>. Running the same
   * tracker type on different versions may lead to round up errors.
   * To avoid inconsistencies from the GStreamer side depending on the OpenCV
   * version, use integer properties independently on the OpenCV.
   **/
  g_object_class_install_property (gobject_class, PROP_INITIAL_X,
      g_param_spec_uint ("object-initial-x", "Initial X coordinate",
          "Track object box's initial X coordinate", 0, G_MAXUINT,
          DEFAULT_PROP_INITIAL_X,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_INITIAL_Y,
      g_param_spec_uint ("object-initial-y", "Initial Y coordinate",
          "Track object box's initial Y coordinate", 0, G_MAXUINT,
          DEFAULT_PROP_INITIAL_Y,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_INITIAL_WIDTH,
      g_param_spec_uint ("object-initial-width", "Object Initial Width",
          "Track object box's initial width", 0, G_MAXUINT,
          DEFAULT_PROP_INITIAL_WIDTH,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_INITIAL_HEIGHT,
      g_param_spec_uint ("object-initial-height", "Object Initial Height",
          "Track object box's initial height", 0, G_MAXUINT,
          DEFAULT_PROP_INITIAL_HEIGHT,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_ALGORITHM,
      g_param_spec_enum ("algorithm", "Algorithm",
          "Algorithm for tracking objects", GST_OPENCV_TRACKER_ALGORITHM,
          GST_OPENCV_TRACKER_ALGORITHM_MEDIANFLOW,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_DRAW,
      g_param_spec_boolean ("draw-rect", "Display",
          "Draw rectangle around tracked object",
          TRUE, (GParamFlags) G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_OBJECTS_OF_INTEREST,
      gst_param_spec_array ("objects-types-of-interest",
          "Objects types of interest",
          "List of objects type to be tracked.",
          g_param_spec_string ("object-type-name", "Object type name",
              "Name of the object type", NULL,
              (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)),
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_MAX_UNSEEN_DURATION,
      g_param_spec_uint64 ("max-unseen-duration",
          "Maximum unseen duration (ns)",
          "Maximum duration without successful tracking upated before marking "
          "track lost and resetting tracker. A value of 0 means no maximum"
          " duration is defined.",
          0, G_MAXUINT64, 0, (GParamFlags) G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_MIN_IOU,
      g_param_spec_float ("objects-min-iou",
          "Mininum IOU", "Minimum intersection over union between object "
          "detection reported area and tracker reported area.",
          0.0f, G_MAXFLOAT, 0.5f, (GParamFlags) G_PARAM_READWRITE));

  gst_element_class_set_static_metadata (element_class,
      "cvtracker",
      "Filter/Effect/Video",
      "Performs object tracking on videos and stores it in video buffer metadata.",
      "Vivek R <123vivekr@gmail.com>");

  gst_element_class_add_static_pad_template (element_class, &src_factory);
  gst_element_class_add_static_pad_template (element_class, &sink_factory);

  gst_type_mark_as_plugin_api (GST_OPENCV_TRACKER_ALGORITHM,
      (GstPluginAPIFlags) 0);
}

static guint64
gst_cvtracker_class_get_track_id (GstCVTrackerClass * klass)
{
  return g_atomic_int_add (&klass->track_id_seq, 1);
}

static void
gst_cvtracker_init (GstCVTracker * filter)
{
  filter->x = DEFAULT_PROP_INITIAL_X;
  filter->y = DEFAULT_PROP_INITIAL_Y;
  filter->width = DEFAULT_PROP_INITIAL_WIDTH;
  filter->height = DEFAULT_PROP_INITIAL_HEIGHT;
#if GST_OPENCV_CHECK_VERSION(4, 5, 1)
  filter->tracker =
      cv::legacy::upgradeTrackingAPI (cv::legacy::TrackerMedianFlow::create ());
#else
  filter->tracker = cv::TrackerMedianFlow::create ();
#endif
  filter->draw = TRUE;
  filter->post_debug_info = TRUE;

  gst_opencv_video_filter_set_in_place (GST_OPENCV_VIDEO_FILTER_CAST (filter),
      TRUE);
  filter->algorithm = GST_OPENCV_TRACKER_ALGORITHM_MEDIANFLOW;
  filter->track_id = 0ul;
  filter->first_time_seen = 0ull;
  filter->last_time_seen = 0ull;
  filter->max_unseen_duration = 0ll;
  filter->objects_types_of_interest = NULL;
  filter->object_type_tracked = 0ul;
  filter->relation_init_params.initial_buf_size = 256;
  filter->relation_init_params.initial_relation_order = 2;
}

static void
gst_cvtracker_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCVTracker *filter = GST_OPENCV_TRACKER (object);

  switch (prop_id) {
    case PROP_INITIAL_X:
      filter->x = g_value_get_uint (value);
      break;
    case PROP_INITIAL_Y:
      filter->y = g_value_get_uint (value);
      break;
    case PROP_INITIAL_WIDTH:
      filter->width = g_value_get_uint (value);
      break;
    case PROP_INITIAL_HEIGHT:
      filter->height = g_value_get_uint (value);
      break;
    case PROP_ALGORITHM:
      filter->algorithm = g_value_get_enum (value);
      break;
    case PROP_DRAW:
      filter->draw = g_value_get_boolean (value);
      break;
    case PROP_OBJECTS_OF_INTEREST:
    {
      gsize size;
      g_slist_free (g_steal_pointer (&filter->objects_types_of_interest));
      if ((size = gst_value_array_get_size (value)) != 0) {
        GSList *list = NULL;
        for (gsize i = 0; i < size; i++) {
          const GValue *val = gst_value_array_get_value (value, i);
          const gchar *str = g_value_get_string (val);
          list = g_slist_prepend (list,
              GUINT_TO_POINTER (g_quark_from_string (str)));
        }
        filter->objects_types_of_interest = list;
      }
      break;
    }
    case PROP_MAX_UNSEEN_DURATION:
      filter->max_unseen_duration = g_value_get_uint64 (value);
      break;
    case PROP_MIN_IOU:
      filter->min_iou = g_value_get_float (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
create_cvtracker (GstCVTracker * filter)
{
  switch (filter->algorithm) {
    case GST_OPENCV_TRACKER_ALGORITHM_BOOSTING:
#if GST_OPENCV_CHECK_VERSION(4, 5, 1)
      filter->tracker =
          cv::legacy::upgradeTrackingAPI (cv::legacy::TrackerBoosting::
          create ());
#else
      filter->tracker = cv::TrackerBoosting::create ();
#endif
      break;
    case GST_OPENCV_TRACKER_ALGORITHM_CSRT:
      filter->tracker = cv::TrackerCSRT::create ();
      break;
    case GST_OPENCV_TRACKER_ALGORITHM_KCF:
      filter->tracker = cv::TrackerKCF::create ();
      break;
    case GST_OPENCV_TRACKER_ALGORITHM_MEDIANFLOW:
#if GST_OPENCV_CHECK_VERSION(4, 5, 1)
      filter->tracker =
          cv::legacy::upgradeTrackingAPI (cv::legacy::TrackerMedianFlow::
          create ());
#else
      filter->tracker = cv::TrackerMedianFlow::create ();
#endif
      break;
    case GST_OPENCV_TRACKER_ALGORITHM_MIL:
      filter->tracker = cv::TrackerMIL::create ();
      break;
    case GST_OPENCV_TRACKER_ALGORITHM_MOSSE:
#if GST_OPENCV_CHECK_VERSION(4, 5, 1)
      filter->tracker =
          cv::legacy::upgradeTrackingAPI (cv::legacy::TrackerMOSSE::create ());
#else
      filter->tracker = cv::TrackerMOSSE::create ();
#endif
      break;
    case GST_OPENCV_TRACKER_ALGORITHM_TLD:
#if GST_OPENCV_CHECK_VERSION(4, 5, 1)
      filter->tracker =
          cv::legacy::upgradeTrackingAPI (cv::legacy::TrackerTLD::create ());
#else
      filter->tracker = cv::TrackerTLD::create ();
#endif
      break;
  }
}

static void
gst_cvtracker_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstCVTracker *filter = GST_OPENCV_TRACKER (object);

  switch (prop_id) {
    case PROP_INITIAL_X:
      g_value_set_uint (value, filter->x);
      break;
    case PROP_INITIAL_Y:
      g_value_set_uint (value, filter->y);
      break;
    case PROP_INITIAL_WIDTH:
      g_value_set_uint (value, filter->width);
      break;
    case PROP_INITIAL_HEIGHT:
      g_value_set_uint (value, filter->height);
      break;
    case PROP_ALGORITHM:
      g_value_set_enum (value, filter->algorithm);
      break;
    case PROP_DRAW:
      g_value_set_boolean (value, filter->draw);
      break;
    case PROP_OBJECTS_OF_INTEREST:
    {
      GQuark ooi_quark;
      GValue val = G_VALUE_INIT;
      g_value_reset (value);
      g_value_init (&val, G_TYPE_STRING);
      GSList *ooi_itr = filter->objects_types_of_interest;
      for (; ooi_itr; ooi_itr = ooi_itr->next) {
        ooi_quark = (GQuark) GPOINTER_TO_UINT (ooi_itr->data);
        g_value_set_string (&val, g_quark_to_string (ooi_quark));
        gst_value_array_append_value (value, &val);
      }
      g_value_unset (&val);
      break;
    }
    case PROP_MAX_UNSEEN_DURATION:
      g_value_set_uint64 (value, filter->max_unseen_duration);
      break;
    case PROP_MIN_IOU:
      g_value_set_float (value, filter->min_iou);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static guint
linear_intersection (guint s1_min, guint s1_max, guint s2_min, guint s2_max)
{
  guint tmp;
  if (s1_max > s2_min && s2_max > s1_min) {
    if (s1_min > s2_min) {
      tmp = (s2_max > s1_max) ? s1_max : s2_max;
      return tmp - s1_min;
    } else {
      tmp = (s1_max > s2_max) ? s2_max : s1_max;
      return tmp - s2_min;
    }
  }
  return 0.0f;
}

static gfloat
iou (guint bb1_x, guint bb1_y, guint bb1_w, guint bb1_h,
    guint bb2_x, guint bb2_y, guint bb2_w, guint bb2_h)
{
  const guint x_intersection =
      linear_intersection (bb1_x, bb1_x + bb1_w, bb2_x, bb2_x + bb2_w);
  const guint y_intersection =
      linear_intersection (bb1_y, bb1_y + bb1_h, bb2_y, bb2_y + bb2_h);
  const guint bb1_area = bb1_w * bb1_h;
  const guint bb2_area = bb2_w * bb2_h;
  const guint intersect_area = x_intersection * y_intersection;
  const guint union_area = bb1_area + bb2_area - intersect_area;
  return union_area == 0 ? 0.0f : ((gfloat) intersect_area) / union_area;
}

static GSList *
gst_cvtracker_is_ooi (GstCVTracker * filter, GQuark obj_type)
{
  return g_slist_find (filter->objects_types_of_interest,
      GUINT_TO_POINTER (obj_type));
}

static gboolean
gst_cvtracker_is_outdated (GstCVTracker * filter, GstClockTimeDiff * dur,
    GstClockTime buf_time)
{
  if (*dur == 0) {
    *dur = GST_CLOCK_DIFF (filter->last_time_seen, buf_time);
  }
  return *dur > filter->max_unseen_duration;
}

static GstFlowReturn
gst_cvtracker_track_object (GstCVTracker * filter, GstBuffer * buf, cv::Mat img)
{
  static const GQuark rlt_type = gst_analytic_od_mtd_get_type_quark ();
  gchar *str = NULL;
  gboolean ooi_tracked = FALSE, success;
  gfloat max_iou = 0.0;
  gpointer state = NULL;
  GSList *obj_type_tracked;
  GstAnalyticODMtd max_iou_od_mtd = {};
  GstAnalyticTrackMtd trk_mtd;
  GstAnalyticRelatableMtd rlt_mtd;
  GstStructure *s;
  GstClockTimeDiff track_age = 0ll;
  guint x, y, w, h;
  gfloat _dummy;


  GstAnalyticRelationMeta *rmeta =
      (GstAnalyticRelationMeta *) gst_buffer_get_meta (buf,
      GST_ANALYTIC_RELATION_META_API_TYPE);

  // Update OCV tracker if possible. 
  if (!filter->roi.empty ()) {
    if (filter->tracker->update (img, *filter->roi)) {
#if !GST_OPENCV_CHECK_VERSION(4, 5, 1)
      /* Round values to avoid inconsistencies depending on the OpenCV version. */
      filter->roi->x = cvRound (filter->roi->x);
      filter->roi->y = cvRound (filter->roi->y);
      filter->roi->width = cvRound (filter->roi->width);
      filter->roi->height = cvRound (filter->roi->height);
#endif

      GST_TRACE_OBJECT (filter, "Tracker %u update [(%u,%u)-(%ux%u)]",
          filter->track_id, filter->roi->x, filter->roi->y, filter->roi->width,
          filter->roi->height);
      if (filter->draw)
        cv::rectangle (img, *filter->roi, cv::Scalar (255, 0, 0), 2, 1);
    } else {
      // Tracking is lost, we reset current tracking context.
      GST_DEBUG_OBJECT (filter, "tracker lost");
      filter->roi.reset ();
    }
  }

  if (rmeta) {
    GST_TRACE_OBJECT (filter, "buffer has relation meta");
    while (gst_analytic_relation_meta_iterate (rmeta, &state, rlt_type,
          &rlt_mtd)) {

      GstAnalyticODMtd od_mtd = (GstAnalyticODMtd) rlt_mtd;
      GQuark od_obj_type = gst_analytic_od_mtd_get_type (&od_mtd);
      GST_TRACE_OBJECT (filter, "OD mtd: (type=%s) %u",
          g_quark_to_string (od_obj_type),
          gst_analytic_relatable_mtd_get_id (&rlt_mtd));

      if ((obj_type_tracked = gst_cvtracker_is_ooi (filter, od_obj_type))) {
        gst_analytic_od_mtd_get_location (&od_mtd, &x, &y, &w, &h, &_dummy);
        if ((x > G_MAXUINT16 || y > G_MAXUINT16 || w > G_MAXUINT16 || h > G_MAXUINT16)) {
          GST_DEBUG_OBJECT (filter, "invalid OD, discard");
          continue;
        }
        // If we're not tracking any object, will start tracking the first 
        // object detected.
        if (filter->roi.empty ()) {
#if GST_OPENCV_CHECK_VERSION(4, 5, 1)
          filter->roi = new (cv::Rect);
#else
          filter->roi = new (cv::Rect2d);
#endif
          filter->roi->x = x;
          filter->roi->y = y;
          filter->roi->width = w;
          filter->roi->height = h;
          create_cvtracker (filter);
          filter->tracker->init (img, *filter->roi);

          GstCVTrackerClass *parent_class =
              (GstCVTrackerClass *) gst_cvtracker_parent_class;
          filter->track_id = gst_cvtracker_class_get_track_id (parent_class);

          success = gst_analytic_relation_add_analytic_track_mtd (rmeta,
              filter->track_id, GST_BUFFER_PTS (buf), NULL, NULL, &trk_mtd);
          
          if (!success) {
            GST_ERROR_OBJECT (filter, "Failed to add track");
            continue;
          }

          filter->first_time_seen = GST_BUFFER_PTS (buf);
          filter->last_time_seen = GST_BUFFER_PTS (buf);

          gst_analytic_relation_meta_set_relation (rmeta,
              GstAnalyticRelTypes::GST_ANALYTIC_REL_TYPE_CONTAIN,
              GST_RELATABLE_META_CAST (&trk_mtd),
              GST_RELATABLE_META_CAST (&od_mtd));

          gst_analytic_relation_meta_set_relation (rmeta,
              GstAnalyticRelTypes::GST_ANALYTIC_REL_TYPE_IS_PART_OF,
              GST_RELATABLE_META_CAST (&od_mtd),
              GST_RELATABLE_META_CAST (&trk_mtd));

          GST_TRACE_OBJECT (filter, "Tracker start %u [(%u,%u)-(%ux%u)]",
              filter->track_id,
              filter->roi->x, filter->roi->y,
              filter->roi->width, filter->roi->height);
          return GST_FLOW_OK;
        } else {
          // find maximum IoU
          gfloat iou_val;
          iou_val = iou (filter->roi->x, filter->roi->y, filter->roi->width,
              filter->roi->height, x, y, w, h);

          if (iou_val > filter->min_iou && iou_val > max_iou) {
            max_iou_od_mtd = od_mtd;
            max_iou = iou_val;
            ooi_tracked = TRUE;
            filter->object_type_tracked =
                (GQuark) GPOINTER_TO_UINT (obj_type_tracked->data);
            GST_TRACE_OBJECT (filter,
                "Tracker %u new max iou (%f): [(%u,%u)-(%ux%u)]",
                filter->track_id, max_iou, x, y, w, h);
          }
        }
      }
    }
  }

  if (!rmeta && !filter->roi.empty ()) {
    // We didn't get any detection but we have tracking in-progress. We can
    // rely on tracking only, for @max_unseen_duration.
    if (!gst_cvtracker_is_outdated (filter, &track_age, GST_BUFFER_PTS (buf))) {
      rmeta = gst_buffer_add_analytic_relation_meta_full (buf,
          &filter->relation_init_params);
      ooi_tracked = TRUE;
    }
  }

  if (ooi_tracked) {
    if (!max_iou_od_mtd.ptr) {
      if (!gst_cvtracker_is_outdated (filter, &track_age, GST_BUFFER_PTS (buf))) {
        success = gst_analytic_relation_add_analytic_od_mtd (rmeta,
            filter->object_type_tracked, filter->roi->x, filter->roi->y,
            filter->roi->width, filter->roi->height, -2.0, NULL, NULL,
            &max_iou_od_mtd);

        GST_TRACE_OBJECT (filter, "Tracker %u missing OD, using tracker roi"
            "[(%u,%u)-(%ux%u)] instead, id=%u", filter->track_id,
            filter->roi->x, filter->roi->y, filter->roi->width,
            filter->roi->height,
            gst_analytic_relatable_mtd_get_id ((GstAnalyticRelatableMtd *)
                &max_iou_od_mtd));
      } else {
        GST_DEBUG_OBJECT (filter, "Tracking %u outdated", filter->track_id);
        filter->roi.reset ();
      }
    } else {
      filter->last_time_seen = GST_BUFFER_PTS (buf);
    }

    if (max_iou_od_mtd.ptr) {
      success = gst_analytic_relation_add_analytic_track_mtd (rmeta,
          filter->track_id, GST_BUFFER_PTS (buf), NULL, NULL, &trk_mtd);
      g_return_val_if_fail (success, GST_FLOW_ERROR);

      gst_analytic_relation_meta_set_relation (rmeta,
          GstAnalyticRelTypes::GST_ANALYTIC_REL_TYPE_IS_PART_OF,
          GST_RELATABLE_META_CAST (&max_iou_od_mtd),
          GST_RELATABLE_META_CAST (&trk_mtd));

      gst_analytic_relation_meta_set_relation (rmeta,
          GstAnalyticRelTypes::GST_ANALYTIC_REL_TYPE_CONTAIN,
          GST_RELATABLE_META_CAST (&trk_mtd),
          GST_RELATABLE_META_CAST (&max_iou_od_mtd));

      str = g_strdup_printf ("object.%s",
          g_quark_to_string (filter->object_type_tracked));

      s = gst_structure_new (str,
          "x", G_TYPE_UINT, (guint) filter->roi->x,
          "y", G_TYPE_UINT, (guint) filter->roi->y,
          "width", G_TYPE_UINT, (guint) filter->roi->width,
          "height", G_TYPE_UINT, (guint) filter->roi->height, NULL);
      g_free (g_steal_pointer (&str));

      gst_element_post_message (GST_ELEMENT (filter),
          gst_message_new_element (GST_OBJECT (filter), s));

      GST_DEBUG_OBJECT (filter, "Tracker update %u [(%u,%u)-(%ux%u)]",
          filter->track_id,
          filter->roi->x, filter->roi->y,
          filter->roi->width, filter->roi->height);
    }
  } else if (!filter->roi.empty ()) {
    if (gst_cvtracker_is_outdated (filter, &track_age, GST_BUFFER_PTS (buf))) {
      GST_DEBUG_OBJECT (filter, "Tracking %u outdated", filter->track_id);
      filter->roi.reset ();
    }
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_cvtracker_track_area (GstCVTracker * filter, GstBuffer * buf, cv::Mat img)
{
  GstStructure *s;
  GstMessage *msg;

  if (filter->roi.empty ()) {
#if GST_OPENCV_CHECK_VERSION(4, 5, 1)
    filter->roi = new (cv::Rect);
#else
    filter->roi = new (cv::Rect2d);
#endif
    filter->roi->x = filter->x;
    filter->roi->y = filter->y;
    filter->roi->width = filter->width;
    filter->roi->height = filter->height;
    create_cvtracker (filter);
    filter->tracker->init (img, *filter->roi);
  } else if (filter->tracker->update (img, *filter->roi)) {
#if !GST_OPENCV_CHECK_VERSION(4, 5, 1)
    /* Round values to avoid inconsistencies depending on the OpenCV version. */
    filter->roi->x = cvRound (filter->roi->x);
    filter->roi->y = cvRound (filter->roi->y);
    filter->roi->width = cvRound (filter->roi->width);
    filter->roi->height = cvRound (filter->roi->height);
#endif
    s = gst_structure_new ("object",
        "x", G_TYPE_UINT, (guint) filter->roi->x,
        "y", G_TYPE_UINT, (guint) filter->roi->y,
        "width", G_TYPE_UINT, (guint) filter->roi->width,
        "height", G_TYPE_UINT, (guint) filter->roi->height, NULL);
    msg = gst_message_new_element (GST_OBJECT (filter), s);
    gst_buffer_add_video_region_of_interest_meta (buf, "object",
        filter->roi->x, filter->roi->y, filter->roi->width,
        filter->roi->height);
    gst_element_post_message (GST_ELEMENT (filter), msg);
    if (filter->draw)
      cv::rectangle (img, *filter->roi, cv::Scalar (255, 0, 0), 2, 1);
    if (!(filter->post_debug_info))
      filter->post_debug_info = TRUE;
  } else if (filter->post_debug_info) {
    GST_DEBUG_OBJECT (filter, "tracker lost");
    filter->post_debug_info = FALSE;
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_cvtracker_transform_ip (GstOpencvVideoFilter * base,
    GstBuffer * buf, cv::Mat img)
{
  GstCVTracker *filter = GST_OPENCV_TRACKER (base);
  if (filter->objects_types_of_interest == NULL) {
    return gst_cvtracker_track_area (filter, buf, img);
  } else {
    return gst_cvtracker_track_object (filter, buf, img);
  }
}
