/*
 * Copyright (C) 2024 Piotr Brzeziński <piotr@centricular.com>
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

import CGStreamer
import Collections
import Foundation
import ScreenCaptureKit

var CAT: UnsafeMutablePointer<GstDebugCategory>!

@objc public class SCKitAudioSrc: NSObject {
  private var baseSrc: UnsafeMutablePointer<GstBaseSrc>

  /* This could be changed on the fly in the future,
   * although setters don't support async (which is what we need for that)
   * Will work around it later. */
  @objc public var excludeCurrentProcess: Bool = true

  private var scStream: SCStream?
  private var scOutputHandler: SCKitSrcOutputHandler?

  private var sampleDeque: Deque<CMSampleBuffer> = []
  private var sampleDequeLock: GMutex = GMutex()
  private var sampleDequeCond: GCond = GCond()

  private var firstBufferPts: CMTime?
  private var totalSamples: UInt64 = 0
  private var audioInfo: GstAudioInfo?

  private var isCaptureStarted: Bool {
    return scStream != nil
  }

  @objc public init(
    src: UnsafeMutablePointer<GstBaseSrc>, debugCat: UnsafeMutablePointer<GstDebugCategory>
  ) {
    CAT = debugCat
    self.baseSrc = src
    super.init()

    self.scOutputHandler = SCKitSrcOutputHandler(src: self, streamType: .audio)

    g_mutex_init(&sampleDequeLock)
    g_cond_init(&sampleDequeCond)

    gst_base_src_set_live(src, 1)
    gst_base_src_set_format(src, GST_FORMAT_TIME)
  }

  deinit {
    g_mutex_clear(&sampleDequeLock)
    g_cond_clear(&sampleDequeCond)
  }

  private func setupScreenCapture() async throws -> Bool {
    guard let audioInfo = audioInfo else {
      #gstError(CAT, "audioInfo not present, aborting!")
      return false
    }

    let scContent = try await SCShareableContent.current
    guard let scDisplay = scContent.displays.first else {
      #gstError(CAT, "Couldn't find a display to capture from")
      return false
    }

    let scFilter = SCContentFilter(display: scDisplay, excludingWindows: [])
    let scConfig = SCStreamConfiguration()
    scConfig.capturesAudio = true
    scConfig.sampleRate = Int(audioInfo.rate)
    scConfig.channelCount = Int(audioInfo.channels)
    scConfig.excludesCurrentProcessAudio = excludeCurrentProcess

    if isCaptureStarted {
      /* According to https://developer.apple.com/documentation/screencapturekit/capturing_screen_content_in_macos#4315331,
       * you can change the configuration of an ongoing capture without stopping it. */
      try await scStream!.updateConfiguration(scConfig)
      try await scStream!.updateContentFilter(scFilter)
    } else {
      scStream = SCStream(filter: scFilter, configuration: scConfig, delegate: nil)
      try scStream!.addStreamOutput(scOutputHandler!, type: .audio, sampleHandlerQueue: nil)
      try await scStream!.startCapture()
    }

    #gstDebug(CAT, "setupScreenCapture finished")
    return true
  }

  private func start() async throws -> Bool {
    totalSamples = 0
    return true
  }

  private func stop() async throws -> Bool {
    try await scStream?.stopCapture()
    scStream = nil

    g_mutex_lock(&sampleDequeLock)
    sampleDeque.removeAll()
    g_mutex_unlock(&sampleDequeLock)

    return true
  }

  private func setCaps(caps: UnsafeMutablePointer<GstCaps>) async throws -> Bool {
    guard let audioInfo = gst_audio_info_new_from_caps(caps)?.pointee else {
      #gstError(CAT, "Couldn't parse audio info from caps")
      return false
    }

    /* TODO: In the case of set_caps() being called during ongoing capture,
     * do we have to lock in any way here? I assume create() shouldn't be called
     * in the meantime? */
    self.audioInfo = audioInfo

    return try await setupScreenCapture()
  }

  private func create(
    offset: guint64, size: guint, bufPtr: UnsafeMutablePointer<UnsafeMutablePointer<GstBuffer>>
  ) async throws -> GstFlowReturn {
    #gstDebug(CAT, "create called")
    if !isCaptureStarted {
      if !(try await setupScreenCapture()) {
        return GST_FLOW_ERROR
      }
    }

    g_mutex_lock(&sampleDequeLock)
    while sampleDeque.count == 0 {
      g_cond_wait(&sampleDequeCond, &sampleDequeLock)
    }
    let sampleBuffer = sampleDeque.popFirst()
    g_mutex_unlock(&sampleDequeLock)

    guard let sampleBuffer = sampleBuffer, sampleBuffer.isValid else {
      #gstError(CAT, "Failed to get a sample buffer!")
      return GST_FLOW_ERROR
    }

    if firstBufferPts == nil {
      firstBufferPts = sampleBuffer.presentationTimeStamp
    }

    guard let buffer = gst_core_media_buffer_new(sampleBuffer, 0, nil) else {
      #gstError(CAT, "Failed to create a CoreMedia buffer!")
      return GST_FLOW_ERROR
    }

    guard let (timestamp, duration) = figureTimingOut(sampleBuffer: sampleBuffer) else {
      #gstError(CAT, "Failed to determine sample timing!")
      return GST_FLOW_ERROR
    }

    let numSamples = sampleBuffer.numSamples

    buffer.pointee.duration = duration
    buffer.pointee.pts = timestamp
    buffer.pointee.offset = totalSamples
    totalSamples += UInt64(numSamples)
    buffer.pointee.offset_end = totalSamples

    guard gst_buffer_add_audio_meta(buffer, &audioInfo!, UInt(numSamples), nil) != nil else {
      #gstError(CAT, "Failed to add audio meta to buffer!")
      return GST_FLOW_ERROR
    }

    bufPtr.pointee = buffer
    return GST_FLOW_OK
  }

  func handleBuffer(sampleBuffer: CMSampleBuffer) {
    g_mutex_lock(&sampleDequeLock)
    sampleDeque.append(sampleBuffer)
    g_cond_signal(&sampleDequeCond)
    g_mutex_unlock(&sampleDequeLock)
  }

  func figureTimingOut(
    sampleBuffer: CMSampleBuffer
  ) -> (timestamp: GstClockTime, duration: GstClockTime)? {
    let (ourClock, baseTime) = baseSrc.withMemoryRebound(to: GstElement.self, capacity: 1) {
      (gst_element_get_clock($0), gst_element_get_base_time($0))
    }

    guard let ourClock = ourClock else {
      #gstError(CAT, "Couldn't get pipeline clock!")
      return nil
    }

    guard let scClock = scStream?.synchronizationClock else {
      #gstError(CAT, "Couldn't get SC stream clock!")
      return nil
    }

    let sckitPts = gst_util_uint64_scale(
      GST_SECOND, UInt64(sampleBuffer.outputPresentationTimeStamp.value),
      UInt64(sampleBuffer.outputPresentationTimeStamp.timescale))

    let duration = gst_util_uint64_scale(
      GST_SECOND, UInt64(sampleBuffer.duration.value), UInt64(sampleBuffer.duration.timescale))

    let now = scClock.time
    let gstNow = gst_util_uint64_scale(
      GST_SECOND, UInt64(now.value), UInt64(now.timescale))
    let gstNowDiff = gstNow - sckitPts

    let runningTime = gst_clock_get_time(ourClock) - baseTime
    let timestamp = runningTime >= gstNowDiff ? runningTime - gstNowDiff : runningTime

    return (timestamp, duration)
  }

  /* The issue here is that Obj-C has no concept of async/await.
   * If we mark an async method as @objc, on the Obj-C side it will be seen as a
   * "withCompletionHandler" version, which is very annoying to use in Obj-C + GStreamer scenario.
   * To use that properly, we'd have to create separate locks/signals for every method.
   * (see OBS SCKit implementation: https://github.com/obsproject/obs-studio/pull/6717/files)
   * Instead, let's wrap our async methods in a blocking task on the Swift side,
   * which effectively does the same thing, but creates much less noise. */
  @objc public func gstStart() -> Bool {
    return BlockingTaskWithResult<Bool> { try await self.start() }.get() ?? false
  }

  @objc public func gstStop() -> Bool {
    return BlockingTaskWithResult<Bool> { try await self.stop() }.get() ?? false
  }

  @objc public func gstCreate(
    offset: guint64, size: guint, bufPtr: UnsafeMutablePointer<UnsafeMutablePointer<GstBuffer>>
  ) -> GstFlowReturn {
    return BlockingTaskWithResult<GstFlowReturn> {
      try await self.create(offset: offset, size: size, bufPtr: bufPtr)
    }.get()
      ?? GST_FLOW_ERROR
  }

  @objc public func gstSetCaps(caps: UnsafeMutablePointer<GstCaps>) -> Bool {
    return BlockingTaskWithResult<Bool> { try await self.setCaps(caps: caps) }.get() ?? false
  }

  private class SCKitSrcOutputHandler: NSObject, SCStreamOutput {
    private weak var src: SCKitAudioSrc?
    private var firstBufferPts = CMTime.zero
    private var streamType: SCStreamOutputType

    init(src: SCKitAudioSrc, streamType: SCStreamOutputType) {
      self.src = src
      self.streamType = streamType
      super.init()
    }

    func stream(
      _ stream: SCStream, didOutputSampleBuffer sampleBuffer: CMSampleBuffer,
      of type: SCStreamOutputType
    ) {
      if type != streamType || src == nil {
        return
      }

      src!.handleBuffer(sampleBuffer: sampleBuffer)
    }
  }
}
