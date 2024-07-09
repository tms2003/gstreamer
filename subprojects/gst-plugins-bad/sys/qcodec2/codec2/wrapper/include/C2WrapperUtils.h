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

#ifndef __C2_WRAPPER_UTILS_H__
#define __C2_WRAPPER_UTILS_H__

#include "types.h"
#include "gbm_priv.h"
#include "codec2wrapper.h"
#include <gst/video/video.h>
#include <C2Config.h>

namespace QTI {

uint32_t toC2InterlaceType(INTERLACE_MODE_TYPE interlace_type);

C2BlockPool::local_id_t toC2BufferPoolType(BUFFER_POOL_TYPE pool_type);

c2_blocking_t toC2BlocingType(BLOCK_MODE_TYPE block_type);

C2Component::drain_mode_t toC2DrainMode(DRAIN_MODE_TYPE mode);

C2Component::flush_mode_t toC2FlushMode(FLUSH_MODE_TYPE mode);

uint32_t toC2RateControlMode(RC_MODE_TYPE mode);

FLAG_TYPE toWrapperFlag(C2FrameData::flags_t flag);

C2FrameData::flags_t toC2Flag(FLAG_TYPE flag);

uint32_t toC2PixelFormat(PIXEL_FORMAT_TYPE pixel);
guint32 gst_to_c2_gbmformat(GstVideoFormat format);
guint32 gbmformat_to_colorformat(guint32 format, guint64 usage);
C2Color::primaries_t toC2Primaries(COLOR_PRIMARIES pixel);
C2Color::transfer_t toC2TransferChar(TRANSFER_CHAR transfer_char);
C2Color::matrix_t toC2Matrix(MATRIX matrix);
C2Color::range_t toC2FullRange(FULL_RANGE full_range);
C2Config::profile_t toC2Profile(C2W_PROFILE_T profile);
C2Config::level_t toC2Level(C2W_LEVEL_T level);

} // namespace QTI

#endif /* __C2_WRAPPER_UTILS_H__ */
