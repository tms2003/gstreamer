// Copyright (C) 2022 Seungha Yang <seungha@centricular.com>
//
// SPDX-License-Identifier: MIT

#pragma once
#include "pch.h"
#include <condition_variable>
#include <gst/gst.h>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

using namespace Windows::UI::Core;

namespace gst_uwp_example {
ref class GstWrapper sealed
{
  internal
    : static property GstWrapper ^
      Instance {
        GstWrapper ^ get() {
          if (instance_ == nullptr) {
            instance_ = ref new GstWrapper();
            instance_->pipelineId_ = 0;
          }
          return instance_;
        }
      }

      std::vector<std::pair<std::string, bool>>
      GetPluginList(void)
  {
    return pluginList_;
  }

  struct PipelineData
  {
    GstWrapper ^ self = nullptr;
    GstElement* pipeline = nullptr;
  };

  unsigned int LaunchPipeline(GstElement* pipeline);
  void DestroyPipeline(unsigned int id);

  GstElement* GetHardwareVideoEncoder(const std::string& format);
  GstElement* GetHardwareVideoDecoder(const std::string& format);

private:
  GstWrapper();
  ~GstWrapper();

  static gboolean threadRunningCb(gpointer userData);
  static gpointer threadFunc(gpointer userData);
  static gboolean runPipeline(PipelineData* data);
  static gboolean stopPipeline(PipelineData* data);
  static gboolean busHandler(GstBus* bus, GstMessage* msg, PipelineData* data);

  static GstWrapper ^ instance_;
  std::vector<std::pair<std::string, bool>> pluginList_;
  std::map<unsigned int, PipelineData*> pipelineList_;
  unsigned int pipelineId_;

  // Internal thread for pipeline management
  std::mutex lock_;
  std::condition_variable cond_;
  GThread* thread_;
  GMainLoop* loop_;
  GMainContext* context_;
};
}
