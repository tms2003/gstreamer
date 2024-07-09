/*
* Copyright (c) 2021, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of The Linux Foundation nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
* ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

// Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear OR LGPL-2.1

#ifndef __CODEC2WRAPPER_H__
#define __CODEC2WRAPPER_H__

#include <glib.h>
#include <gmodule.h>
#include <dlfcn.h>
#include <gst/video/video.h>
#include <stdint.h>

#ifdef GST_USE_MMM_COLOR_FMT
#include <media/mmm_color_fmt.h>

enum color_fmts {
        COLOR_FMT_NV12 = MMM_COLOR_FMT_NV12,
        COLOR_FMT_NV21 = MMM_COLOR_FMT_NV21,
        COLOR_FMT_NV12_UBWC = MMM_COLOR_FMT_NV12_UBWC,
        COLOR_FMT_NV12_BPP10_UBWC = MMM_COLOR_FMT_NV12_BPP10_UBWC,
        COLOR_FMT_RGBA8888 = MMM_COLOR_FMT_RGBA8888,
        COLOR_FMT_RGBA8888_UBWC = MMM_COLOR_FMT_RGBA8888_UBWC,
        COLOR_FMT_RGBA1010102_UBWC = MMM_COLOR_FMT_RGBA1010102_UBWC,
        COLOR_FMT_RGB565_UBWC = MMM_COLOR_FMT_RGB565_UBWC,
        COLOR_FMT_P010_UBWC = MMM_COLOR_FMT_P010_UBWC,
        COLOR_FMT_P010 = MMM_COLOR_FMT_P010,
        COLOR_FMT_NV12_512 = MMM_COLOR_FMT_NV12_512,
};

#define VENUS_Y_STRIDE MMM_COLOR_FMT_Y_STRIDE
#define VENUS_UV_STRIDE MMM_COLOR_FMT_UV_STRIDE
#define VENUS_Y_SCANLINES MMM_COLOR_FMT_Y_SCANLINES
#define VENUS_UV_SCANLINES MMM_COLOR_FMT_UV_SCANLINES
#define VENUS_Y_META_STRIDE MMM_COLOR_FMT_Y_META_STRIDE
#define VENUS_UV_META_STRIDE MMM_COLOR_FMT_UV_META_STRIDE
#define VENUS_Y_META_SCANLINES MMM_COLOR_FMT_Y_META_SCANLINES
#define VENUS_UV_META_SCANLINES MMM_COLOR_FMT_UV_META_SCANLINES
#define VENUS_BUFFER_SIZE MMM_COLOR_FMT_BUFFER_SIZE
#define VENUS_BUFFER_SIZE_USED MMM_COLOR_FMT_BUFFER_SIZE_USED

#else
#include <media/msm_media_info.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define ALIGN(num, to) (((num) + (to - 1)) & (~(to - 1)))

#define CONFIG_FUNCTION_KEY_PIXELFORMAT "pixelformat"
#define CONFIG_FUNCTION_KEY_RESOLUTION "resolution"
#define CONFIG_FUNCTION_KEY_BITRATE "bitrate"
#define CONFIG_FUNCTION_KEY_MIRROR "mirror"
#define CONFIG_FUNCTION_KEY_ROTATION "rotation"
#define CONFIG_FUNCTION_KEY_RATECONTROL "ratecontrol"
#define CONFIG_FUNCTION_KEY_DEC_LOW_LATENCY "dec_low_latency"
#define CONFIG_FUNCTION_KEY_INTRAREFRESH "intra_refresh"
#define CONFIG_FUNCTION_KEY_INTRAREFRESH_TYPE "intra_refresh_type"
#define CONFIG_FUNCTION_KEY_OUTPUT_PICTURE_ORDER_MODE "output_picture_order_mode"
#define CONFIG_FUNCTION_KEY_DOWNSCALE "downscale"
#define CONFIG_FUNCTION_KEY_ENC_CSC "enc_colorspace_conversion"
#define CONFIG_FUNCTION_KEY_COLOR_ASPECTS_INFO "colorspace_color_aspects"
#define CONFIG_FUNCTION_KEY_SLICE_MODE "slice_mode"
#define CONFIG_FUNCTION_KEY_BLUR_MODE "blur_mode"
#define CONFIG_FUNCTION_KEY_BLUR_RESOLUTION "blur_resolution"
#define CONFIG_FUNCTION_KEY_ROIREGION "roiregion"
#define CONFIG_FUNCTION_KEY_BITRATE_SAVING_MODE "bitrate_saving_mode"
#define CONFIG_FUNCTION_KEY_PROFILE_LEVEL "profile_level"
#define CONFIG_FUNCTION_KEY_INTERLACE_INFO "interlace_info"
#define CONFIG_FUNCTION_KEY_DEINTERLACE "deinterlace"
#define CONFIG_FUNCTION_KEY_FRAMERATE "framerate"
#define CONFIG_FUNCTION_KEY_INTRAFRAMES_PERIOD "intraframes_period"
#define CONFIG_FUNCTION_KEY_INTRA_VIDEO_FRAME_REQUEST "intra_video_frame_request"
#define CONFIG_FUNCTION_KEY_VIDEO_HEADER_MODE "video_header_mode"
#define CONFIG_FUNCTION_KEY_IPB_QP_RANGE "IPB_qp_range"
#define CONFIG_FUNCTION_KEY_IPB_QP_INIT "IPB_qp_init"

#define C2_TICKS_PER_SECOND 1000000

typedef struct comp_cb {
    gpointer data_copy_func;
    gpointer data_copy_func_param;
} comp_cb;

typedef int (*fnDataCopy)(int dstbuf_fd, void* srcbuf, uint32_t* pdatalen, void* param);

typedef enum {
    BUFFER_POOL_BASIC_LINEAR = 0,
    BUFFER_POOL_BASIC_GRAPHIC
} BUFFER_POOL_TYPE;

typedef enum {
    BLOCK_MODE_DONT_BLOCK = 0,
    BLOCK_MODE_MAY_BLOCK
} BLOCK_MODE_TYPE;

typedef enum {
    DRAIN_MODE_COMPONENT_WITH_EOS = 0,
    DRAIN_MODE_COMPONENT_NO_EOS,
    DRAIN_MODE_CHAIN
} DRAIN_MODE_TYPE;

typedef enum {
    FLUSH_MODE_COMPONENT = 0,
    FLUSH_MODE_CHAIN
} FLUSH_MODE_TYPE;

typedef enum {
    INTERLACE_MODE_PROGRESSIVE = 0, ///< progressive
    INTERLACE_MODE_INTERLEAVED_TOP_FIRST, ///< line-interleaved. top-field-first
    INTERLACE_MODE_INTERLEAVED_BOTTOM_FIRST, ///< line-interleaved. bottom-field-first
    INTERLACE_MODE_FIELD_TOP_FIRST, ///< field-sequential. top-field-first
    INTERLACE_MODE_FIELD_BOTTOM_FIRST, ///< field-sequential. bottom-field-first
} INTERLACE_MODE_TYPE;

typedef enum {
    C2_INTERLACE_MODE_PROGRESSIVE = 0, ///< progressive
    C2_INTERLACE_MODE_INTERLEAVED_TOP_FIRST, ///< line-interleaved. top-field-first
    C2_INTERLACE_MODE_INTERLEAVED_BOTTOM_FIRST, ///< line-interleaved. bottom-field-first
    C2_INTERLACE_MODE_FIELD_TOP_FIRST, ///< field-sequential. top-field-first
    C2_INTERLACE_MODE_FIELD_BOTTOM_FIRST, ///< field-sequential. bottom-field-first
} C2_INTERLACE_MODE_TYPE;

typedef enum {
    FLAG_TYPE_DROP_FRAME = 1 << 0,
    FLAG_TYPE_END_OF_STREAM = 1 << 1, ///< For input frames: no output frame shall be generated when processing this frame.
    ///< For output frames: this frame shall be discarded.
    FLAG_TYPE_DISCARD_FRAME = 1 << 2, ///< This frame shall be discarded with its metadata.
    FLAG_TYPE_INCOMPLETE = 1 << 3, ///< This frame is not the last frame produced for the input
    FLAG_TYPE_CODEC_CONFIG = 1 << 4 ///< Frame contains only codec-specific configuration data, and no actual access unit
} FLAG_TYPE;

typedef enum {
    PIXEL_FORMAT_NV12_LINEAR = 0,
    PIXEL_FORMAT_NV12_UBWC,
    PIXEL_FORMAT_RGBA_8888,
    PIXEL_FORMAT_YV12,
    PIXEL_FORMAT_P010,
    PIXEL_FORMAT_TP10_UBWC,
    PIXEL_FORMAT_NV12_512
} PIXEL_FORMAT_TYPE;

typedef enum {
    EVENT_OUTPUTS_DONE = 0,
    EVENT_TRIPPED,
    EVENT_ERROR,
    EVENT_UPDATE_MAX_BUF_CNT,
    EVENT_ACQUIRE_EXT_BUF,
} EVENT_TYPE;

typedef enum {
    DEFAULT_ORDER = 0,
    DISPLAY_ORDER,
    DECODER_ORDER,
} OUTPUT_PIC_ORDER;

typedef enum {
    MIRROR_NONE = 0,
    MIRROR_VERTICAL,
    MIRROR_HORIZONTAL,
    MIRROR_BOTH,
} MIRROR_TYPE;

typedef enum {
    RC_OFF = 0,
    RC_CONST,
    RC_CBR_VFR,
    RC_VBR_CFR,
    RC_VBR_VFR,
    RC_CQ,
    RC_UNSET = 0xFFFF
} RC_MODE_TYPE;

typedef enum {
    SLICE_MODE_DISABLE,
    SLICE_MODE_MB,
    SLICE_MODE_BYTES,
} SLICE_MODE;

typedef enum {
    BLUR_AUTO = 0,
    BLUR_MANUAL,
    BLUR_DISABLE,
} BLUR_MODE;

typedef enum {
    COLOR_PRIMARIES_UNSPECIFIED,
    COLOR_PRIMARIES_BT709,
    COLOR_PRIMARIES_BT470_M,
    COLOR_PRIMARIES_BT601_625,
    COLOR_PRIMARIES_BT601_525,
    COLOR_PRIMARIES_GENERIC_FILM,
    COLOR_PRIMARIES_BT2020,
    COLOR_PRIMARIES_RP431,
    COLOR_PRIMARIES_EG432,
    COLOR_PRIMARIES_EBU3213,
} COLOR_PRIMARIES;

typedef enum {
    COLOR_TRANSFER_UNSPECIFIED,
    COLOR_TRANSFER_LINEAR,
    COLOR_TRANSFER_SRGB,
    COLOR_TRANSFER_170M,
    COLOR_TRANSFER_GAMMA22,
    COLOR_TRANSFER_GAMMA28,
    COLOR_TRANSFER_ST2084,
    COLOR_TRANSFER_HLG,
    COLOR_TRANSFER_240M,
    COLOR_TRANSFER_XVYCC,
    COLOR_TRANSFER_BT1361,
    COLOR_TRANSFER_ST428,
} TRANSFER_CHAR;

typedef enum {
    COLOR_MATRIX_UNSPECIFIED,
    COLOR_MATRIX_BT709,
    COLOR_MATRIX_FCC47_73_682,
    COLOR_MATRIX_BT601,
    COLOR_MATRIX_240M,
    COLOR_MATRIX_BT2020,
    COLOR_MATRIX_BT2020_CONSTANT,
} MATRIX;

typedef enum {
    COLOR_RANGE_UNSPECIFIED,
    COLOR_RANGE_FULL,
    COLOR_RANGE_LIMITED,
} FULL_RANGE;

typedef enum {
    IR_NONE = 0,
    IR_RANDOM,
    IR_CYCLIC,
} IR_MODE_TYPE;

typedef enum {
    BITRATE_SAVING_MODE_DISABLE_ALL = 0,
    BITRATE_SAVING_MODE_ENABLE_8BIT,
    BITRATE_SAVING_MODE_ENABLE_10BIT,
    BITRATE_SAVING_MODE_ENABLE_ALL,
} BITRATE_SAVING_MODE;

typedef enum {
    C2W_PROFILE_UNSPECIFIED,
    C2W_AVC_PROFILE_BASELINE, ///< AVC (H.264) Baseline
    C2W_AVC_PROFILE_CONSTRAINT_BASELINE, ///< AVC (H.264) Constrained Baseline
    C2W_AVC_PROFILE_MAIN, ///< AVC (H.264) Main
    C2W_AVC_PROFILE_HIGH, ///< AVC (H.264) High
    C2W_AVC_PROFILE_CONSTRAINT_HIGH, ///< AVC (H.264) Constrained High

    C2W_HEVC_PROFILE_MAIN = 128, ///< HEVC (H.265) Main
    C2W_HEVC_PROFILE_MAIN10, ///< HEVC (H.265) Main 10
    C2W_HEVC_PROFILE_MAIN_STILL_PIC, ///< HEVC (H.265) Main Still Picture
} C2W_PROFILE_T;

typedef enum {
    C2W_LEVEL_UNSPECIFIED,
    C2W_AVC_LEVEL_1, ///< AVC (H.264) Level 1
    C2W_AVC_LEVEL_1b, ///< AVC (H.264) Level 1b
    C2W_AVC_LEVEL_11, ///< AVC (H.264) Level 1.1
    C2W_AVC_LEVEL_12, ///< AVC (H.264) Level 1.2
    C2W_AVC_LEVEL_13, ///< AVC (H.264) Level 1.3
    C2W_AVC_LEVEL_2, ///< AVC (H.264) Level 2
    C2W_AVC_LEVEL_21, ///< AVC (H.264) Level 2.1
    C2W_AVC_LEVEL_22, ///< AVC (H.264) Level 2.2
    C2W_AVC_LEVEL_3, ///< AVC (H.264) Level 3
    C2W_AVC_LEVEL_31, ///< AVC (H.264) Level 3.1
    C2W_AVC_LEVEL_32, ///< AVC (H.264) Level 3.2
    C2W_AVC_LEVEL_4, ///< AVC (H.264) Level 4
    C2W_AVC_LEVEL_41, ///< AVC (H.264) Level 4.1
    C2W_AVC_LEVEL_42, ///< AVC (H.264) Level 4.2
    C2W_AVC_LEVEL_5, ///< AVC (H.264) Level 5
    C2W_AVC_LEVEL_51, ///< AVC (H.264) Level 5.1
    C2W_AVC_LEVEL_52, ///< AVC (H.264) Level 5.2
    C2W_AVC_LEVEL_6, ///< AVC (H.264) Level 6
    C2W_AVC_LEVEL_61, ///< AVC (H.264) Level 6.1
    C2W_AVC_LEVEL_62, ///< AVC (H.264) Level 6.2

    C2W_HEVC_LEVEL_MAIN_TIER_LEVEL1 = 128, ///< HEVC (H.265) Main Tier Level 1
    C2W_HEVC_LEVEL_MAIN_TIER_LEVEL2, ///< HEVC (H.265) Main Tier Level 2
    C2W_HEVC_LEVEL_MAIN_TIER_LEVEL21, ///< HEVC (H.265) Main Tier Level 2.1
    C2W_HEVC_LEVEL_MAIN_TIER_LEVEL3, ///< HEVC (H.265) Main Tier Level 3
    C2W_HEVC_LEVEL_MAIN_TIER_LEVEL31, ///< HEVC (H.265) Main Tier Level 3.1
    C2W_HEVC_LEVEL_MAIN_TIER_LEVEL4, ///< HEVC (H.265) Main Tier Level 4
    C2W_HEVC_LEVEL_MAIN_TIER_LEVEL41, ///< HEVC (H.265) Main Tier Level 4.1
    C2W_HEVC_LEVEL_MAIN_TIER_LEVEL5, ///< HEVC (H.265) Main Tier Level 5
    C2W_HEVC_LEVEL_MAIN_TIER_LEVEL51, ///< HEVC (H.265) Main Tier Level 5.1
    C2W_HEVC_LEVEL_MAIN_TIER_LEVEL52, ///< HEVC (H.265) Main Tier Level 5.2
    C2W_HEVC_LEVEL_MAIN_TIER_LEVEL6, ///< HEVC (H.265) Main Tier Level 6
    C2W_HEVC_LEVEL_MAIN_TIER_LEVEL61, ///< HEVC (H.265) Main Tier Level 6.1
    C2W_HEVC_LEVEL_MAIN_TIER_LEVEL62, ///< HEVC (H.265) Main Tier Level 6.2

    C2W_HEVC_LEVEL_HIGH_TIER_LEVEL1 = 256, ///< HEVC (H.265) High Tier Level 1
    C2W_HEVC_LEVEL_HIGH_TIER_LEVEL2, ///< HEVC (H.265) High Tier Level 2
    C2W_HEVC_LEVEL_HIGH_TIER_LEVEL21, ///< HEVC (H.265) High Tier Level 2.1
    C2W_HEVC_LEVEL_HIGH_TIER_LEVEL3, ///< HEVC (H.265) High Tier Level 3
    C2W_HEVC_LEVEL_HIGH_TIER_LEVEL31, ///< HEVC (H.265) High Tier Level 3.1
    C2W_HEVC_LEVEL_HIGH_TIER_LEVEL4, ///< HEVC (H.265) High Tier Level 4
    C2W_HEVC_LEVEL_HIGH_TIER_LEVEL41, ///< HEVC (H.265) High Tier Level 4.1
    C2W_HEVC_LEVEL_HIGH_TIER_LEVEL5, ///< HEVC (H.265) High Tier Level 5
    C2W_HEVC_LEVEL_HIGH_TIER_LEVEL51, ///< HEVC (H.265) High Tier Level 5.1
    C2W_HEVC_LEVEL_HIGH_TIER_LEVEL52, ///< HEVC (H.265) High Tier Level 5.2
    C2W_HEVC_LEVEL_HIGH_TIER_LEVEL6, ///< HEVC (H.265) High Tier Level 6
    C2W_HEVC_LEVEL_HIGH_TIER_LEVEL61, ///< HEVC (H.265) High Tier Level 6.1
    C2W_HEVC_LEVEL_HIGH_TIER_LEVEL62, ///< HEVC (H.265) High Tier Level 6.2
} C2W_LEVEL_T;

typedef struct {
    guint8* data;
    gint32 fd;
    gint32 meta_fd;
    guint32 size;
    guint32 capacity; ///< Total allocation size
    guint64 timestamp;
    guint64 index;
    guint32 width;
    guint32 height;
    guint32 stride[2];
    gsize offset[2];
    GstVideoFormat format;
    guint32 ubwc_flag;
    FLAG_TYPE flag;
    BUFFER_POOL_TYPE pool_type;
    guint8* config_data; // codec config data
    guint32 config_size; // size of codec config data
    void* c2Buffer;
    void* gbm_bo;
    gboolean secure;
    guint32 interlaceMode;
    gboolean heic_flag;
} BufferDescriptor;

typedef struct {
    const char* config_name;
    gboolean isInput;
    // Each parameter should only use one member of union. For example,
    // member val and sliceMode can not be used at the same time.
    // Otherwise, date overlapped since members in union shares the
    // same address.
    union {
        guint output_picture_order_mode;
        gboolean low_latency_mode;
        gboolean color_space_conversion;
        gboolean deinterlace;
        gboolean force_idr;
        gboolean inline_sps_pps_headers;

        union {
            guint32 u32;
            guint64 u64;
            gint32 i32;
            gint64 i64;
        } val;

        struct {
            guint32 width;
            guint32 height;
        } resolution;

        struct {
            PIXEL_FORMAT_TYPE fmt;
        } pixelFormat;

        struct {
            INTERLACE_MODE_TYPE type;
        } interlaceMode;

        struct {
            MIRROR_TYPE type;
        } mirror;

        struct {
            RC_MODE_TYPE type;
        } rcMode;

        struct {
            guint32 slice_size;
            SLICE_MODE type;
        } sliceMode;

        struct {
            BLUR_MODE mode;
        } blur;

        struct {
            int64_t timestampUs;
            char* type;
            char* rectPayload;
            char* rectPayloadExt;
        } roiRegion;

        struct {
            IR_MODE_TYPE type;
            guint32 intra_refresh_mbs;
        } irMode;

        struct {
            COLOR_PRIMARIES primaries;
            TRANSFER_CHAR transfer_char;
            MATRIX matrix;
            FULL_RANGE full_range;
        } colorAspects;

        struct {
            BITRATE_SAVING_MODE saving_mode;
        } bitrate_saving_mode;

        struct {
            C2W_PROFILE_T profile;
            C2W_LEVEL_T level;
        } profileAndLevel;

        gfloat framerate;

        struct {
            guint32 min_i_qp;
            guint32 max_i_qp;
            guint32 min_p_qp;
            guint32 max_p_qp;
            guint32 min_b_qp;
            guint32 max_b_qp;
        } qp_ranges;

        struct {
            gboolean quant_i_frames_enable;
            guint32 quant_i_frames;
            gboolean quant_p_frames_enable;
            guint32 quant_p_frames;
            gboolean quant_b_frames_enable;
            guint32 quant_b_frames;
        } qp_init;
    };
} ConfigParams;

typedef struct {
    guint32 width;
    guint32 height;
} BufferResolution;

typedef void (*listener_cb)(const void* handle, EVENT_TYPE type, void* data);

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Component Store API
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void* c2componentStore_create();
const gchar* c2componentStore_getName(void* const comp_store);
gboolean c2componentStore_createComponent(void* const comp_store, const gchar* name, void** const component, comp_cb* cb);
gboolean c2componentStore_createInterface(void* const comp_store, const gchar* name, void** const interface);
gboolean c2componentStore_listComponents(void* const comp_store, GPtrArray* array);
gboolean c2componentStore_isComponentSupported(void* const comp_store, gchar* name);
gboolean c2componentStore_delete(void* comp_store);

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Component API
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
gboolean c2component_setListener(void* const comp, void* cb_context, listener_cb callback, BLOCK_MODE_TYPE block);
gboolean c2component_alloc(void* const comp, BufferDescriptor* buffer);
gboolean c2component_queue(void* const comp, BufferDescriptor* buffer);
gboolean c2component_flush(void* const comp, FLUSH_MODE_TYPE mode);
gboolean c2component_drain(void* const comp, DRAIN_MODE_TYPE mode);
gboolean c2component_start(void* const comp);
gboolean c2component_stop(void* const comp);
gboolean c2component_reset(void* const comp);
gboolean c2component_release(void* const comp);
void* c2component_intf(void* const comp);
gboolean c2component_createBlockpool(void* const comp, BUFFER_POOL_TYPE poolType);
gboolean c2component_configBlockpool(void* comp, BUFFER_POOL_TYPE poolType);
gboolean c2component_freeOutBuffer(void* const comp, guint64 bufferId);
gboolean c2component_delete(void* comp);
gboolean c2component_attachExternalFd(void* comp, BUFFER_POOL_TYPE type, int fd);
gboolean c2component_setUseExternalBuffer(void* comp, BUFFER_POOL_TYPE type, gboolean useExternal);

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ComponentInterface API
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const gchar* c2componentInterface_getName(void* const comp_intf);
const gint c2componentInterface_getId(void* const comp_intf);
gboolean c2componentInterface_initReflectedParamUpdater(void* const comp_store, void* const comp_intf);
gboolean c2componentInterface_config(void* const comp_intf, GPtrArray* config, BLOCK_MODE_TYPE block);

#ifdef __cplusplus
}
#endif

#endif /* __CODEC2WRAPPER_H__ */
