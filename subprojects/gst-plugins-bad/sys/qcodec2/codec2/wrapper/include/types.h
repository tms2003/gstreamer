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

#ifndef __C2_WRAPPER_TYPES_H__
#define __C2_WRAPPER_TYPES_H__

#include <stdio.h>
#include <C2Component.h>
#include <glib.h>
#include <C2Buffer.h>
#include <gst/gst.h>

namespace QTI {

#define LOG_MESSAGE GST_LOG
#define LOG_INFO GST_INFO
#define LOG_WARNING GST_WARNING
#define LOG_DEBUG GST_DEBUG
#define LOG_ERROR GST_ERROR

#define UNUSED(x) (void)(x)

typedef std::unique_ptr<C2Param> (*configFunction)(gpointer data);
typedef std::unique_ptr<C2Param> (*configFunctionForVendorParams)(gpointer data, void* const comp_intf);

class EventCallback {
public:
    // Notify that an output buffer is available with given index.
    virtual void onOutputBufferAvailable(
        const std::shared_ptr<C2Buffer>& buffer,
        uint64_t index,
        uint64_t timestamp,
        uint32_t interlace,
        C2FrameData::flags_t flag)
        = 0;

    virtual void onTripped(uint32_t errorCode) = 0;
    virtual void onError(uint32_t errorCode) = 0;
    virtual void onUpdateMaxBufCount(uint32_t outputDelay) = 0;
    virtual void onAcquireExtBuffer(uint32_t width, uint32_t height) = 0;

    virtual ~EventCallback() {}
};

} // namespace QTI

#endif /* __C2_WRAPPER_TYPES_H__ */
