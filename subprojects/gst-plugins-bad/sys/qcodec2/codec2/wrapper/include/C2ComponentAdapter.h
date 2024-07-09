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

#ifndef __C2COMPONENTADAPTER_H__
#define __C2COMPONENTADAPTER_H__

#include "types.h"
#include "C2ComponentInterfaceAdapter.h"
#include "codec2wrapper.h"

#include <C2Config.h>
#include <C2Component.h>
#include <mutex>
#include <map>
#include <set>
#include <condition_variable>
#include <C2Buffer.h>

namespace android {
class C2AllocatorGBM;
class C2AllocatorIon;
}

namespace QTI {

class C2ComponentAdapter;

class C2ComponentListenerAdapter : public C2Component::Listener {

public:
    C2ComponentListenerAdapter(C2ComponentAdapter* comp);
    ~C2ComponentListenerAdapter();

    /* Methods implementing C2Component::Listener */
    void onWorkDone_nb(std::weak_ptr<C2Component> component, std::list<std::unique_ptr<C2Work> > workItems) override;
    void onTripped_nb(std::weak_ptr<C2Component> component, std::vector<std::shared_ptr<C2SettingResult> > settingResult) override;
    void onError_nb(std::weak_ptr<C2Component> component, uint32_t errorCode) override;

private:
    C2ComponentAdapter* mComp;
};

class C2ComponentAdapter {

public:
    C2ComponentAdapter(std::shared_ptr<C2Component> comp);
    ~C2ComponentAdapter();

    /* Methods implementing C2Component */
    std::shared_ptr<C2Buffer> alloc(BufferDescriptor* buffer);

    /* Queue buffer with va (copy) */
    c2_status_t queue(BufferDescriptor* buffer);

    /* Queue buffer with fd (zero-copy) */
    c2_status_t queue(
        uint32_t fd,
        C2FrameData::flags_t inputFrameFlag,
        uint64_t frame_index,
        uint64_t timestamp,
        C2BlockPool::local_id_t poolType);

    c2_status_t flush(C2Component::flush_mode_t mode);
    c2_status_t drain(C2Component::drain_mode_t mode);
    c2_status_t start();
    c2_status_t stop();
    c2_status_t reset();
    c2_status_t release();
    C2ComponentInterfaceAdapter* intf();
    c2_status_t createBlockpool(C2BlockPool::local_id_t poolType);
    c2_status_t configBlockPool(C2BlockPool::local_id_t poolType);

    /* Methods implementing Listener */
    void handleWorkDone(std::weak_ptr<C2Component> component, std::list<std::unique_ptr<C2Work> > workItems);
    void handleTripped(std::weak_ptr<C2Component> component, std::vector<std::shared_ptr<C2SettingResult> > settingResult);
    void handleError(std::weak_ptr<C2Component> component, uint32_t errorCode);

    uint32_t getInterlaceMode(std::vector<std::unique_ptr<C2Param> >& configUpdate);

    /* This class methods */
    c2_status_t setListenercallback(std::unique_ptr<EventCallback> callback, c2_blocking_t mayBlock);
    c2_status_t setDataCopyFunc(void* func, void* param);
    c2_status_t setCompStore(std::weak_ptr<C2ComponentStore> store);
    c2_status_t freeOutputBuffer(uint64_t bufferIdx);
    c2_status_t attachExternalFd(BUFFER_POOL_TYPE type, int fd);
    c2_status_t setUseExternalBuffer(BUFFER_POOL_TYPE type, bool useExternal);
    void acquireExtBuf(uint32_t width, uint32_t height);

private:
    c2_status_t prepareC2Buffer(std::shared_ptr<C2Buffer>* c2Buf, BufferDescriptor* buffer);
    c2_status_t writePlane(uint8_t* dest, BufferDescriptor* buffer_info);
    c2_status_t waitForProgressOrStateChange(
        uint32_t numPendingWorks,
        uint32_t timeoutMs);
    void registerTrackBuffer(const C2FrameData& input);
    void unregisterTrackBuffer(std::list<std::unique_ptr<C2Work> >& workItems);
    void unregisterTrackBufferAll();
    void onBufferDestroyed(const C2Buffer* buf, void* arg);
    static void onDestroyNotify(const C2Buffer* buf, void* arg);
    bool isUseExternalBuffer(BUFFER_POOL_TYPE type);
    c2_status_t importExternalBuf(std::shared_ptr<C2Buffer>& c2Buf, int fd, uint32_t size);

    std::weak_ptr<C2ComponentStore> mStore;
    std::shared_ptr<C2Component> mComp;
    std::shared_ptr<C2ComponentInterfaceAdapter> mIntf;
    std::shared_ptr<C2Component::Listener> mListener;
    std::unique_ptr<EventCallback> mCallback;

    struct TrackBuffer {
        C2ComponentAdapter* adapter;
        uint64_t frameIndex;
        std::weak_ptr<C2Buffer> buffer;
        TrackBuffer(C2ComponentAdapter* adapter,
            uint64_t frameIndex,
            const std::shared_ptr<C2Buffer>& buffer)
            : adapter(adapter)
            , frameIndex(frameIndex)
            , buffer(buffer)
        {
        }
    };

    std::shared_ptr<C2BlockPool> mLinearPool; // C2PlatformLinearBlockPool
    std::shared_ptr<C2BlockPool> mGraphicPool; // C2PlatformGraphicBlockPool
    std::map<uint64_t, std::shared_ptr<C2GraphicBlock> > mInPendingBuffer;
    std::map<uint64_t, std::shared_ptr<C2Buffer> > mOutPendingBuffer;
    std::set<TrackBuffer*> mTrackBuffers;
    std::shared_ptr<android::C2AllocatorGBM> mC2AllocatorGBM;
    std::shared_ptr<android::C2AllocatorIon> mC2AllocatorIon;

    uint32_t mNumPendingWorks;
    std::mutex mLock;
    std::mutex mLockOut;
    std::condition_variable mCondition;
    fnDataCopy mDataCopyFunc;
    void* mDataCopyFuncParam;
};

} // namespace QTI

#endif /* __C2COMPONENTADAPTER_H__ */
