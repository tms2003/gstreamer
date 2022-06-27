// Copyright (C) 2022 Seungha Yang <seungha@centricular.com>
//
// SPDX-License-Identifier: MIT

#include "pch.h"
#include "GstWrapper.h"
#include "MainPage.xaml.h"
#include "Utils.h"

using namespace gst_uwp_example;

GstWrapper ^ GstWrapper::instance_;

GstWrapper::GstWrapper()
{
  GstRegistry* reg;

  gst_init(nullptr, nullptr);

  reg = gst_registry_get();

  static gchar* plugin_list[] = {
    { "gstapp.dll" },
    { "gstaudiobuffersplit.dll" },
    { "gstaudioconvert.dll" },
    { "gstaudiorate.dll" },
    { "gstaudioresample.dll" },
    { "gstaudiotestsrc.dll" },
    { "gstautodetect.dll" },
    { "gstcoreelements.dll" },
    { "gstd3d11.dll" },
    { "gstdtls.dll" },
    { "gstinterleave.dll" },
    { "gstmediafoundation.dll" },
    { "gstnice.dll" },
    { "gstopengl.dll" },
    { "gstopenh264.dll" },
    { "gstopus.dll" },
    { "gstplayback.dll" },
    { "gstproxy.dll" },
    { "gstrtp.dll" },
    { "gstrtpmanager.dll" },
    { "gstsctp.dll" },
    { "gstsrtp.dll" },
    { "gstvideoconvert.dll" },
    { "gstvideoparsersbad.dll" },
    { "gstvideorate.dll" },
    { "gstvideoscale.dll" },
    { "gstvideotestsrc.dll" },
    { "gstvpx.dll" },
    { "gstwasapi2.dll" },
    { "gstwebrtc.dll" },
  };

  for (int i = 0; i < G_N_ELEMENTS(plugin_list); i++) {
    GstPlugin* plugin = gst_plugin_load_file(plugin_list[i], NULL);

    pluginList_.emplace_back(
      std::make_pair(std::string(plugin_list[i]), plugin != nullptr));

    if (!plugin)
      continue;

    gst_registry_add_plugin(reg, plugin);
    gst_object_unref(plugin);
  }

  context_ = g_main_context_new();
  loop_ = g_main_loop_new(context_, FALSE);

  thread_ = g_thread_new(
    "GstWrapperThread", (GThreadFunc)GstWrapper::threadFunc, (gpointer)this);

  std::unique_lock<std::mutex> Lock(lock_);
  while (!g_main_loop_is_running(loop_))
    cond_.wait(Lock);
}

GstWrapper::~GstWrapper()
{
  g_main_loop_quit(loop_);
  g_thread_join(thread_);
  g_main_loop_unref(loop_);
  g_main_context_unref(context_);
}

static void
LogPipelineErrorOrWarning(GstMessage* msg, bool isError)
{
  GError* err = NULL;
  gchar *name, *debug = NULL;
  Platform::String ^ logMsg;

  name = gst_object_get_path_string(msg->src);
  if (isError) {
    gst_message_parse_error(msg, &err, &debug);
    logMsg = "[ERROR] ";
  } else {
    gst_message_parse_warning(msg, &err, &debug);
    logMsg = "[WARNING] ";
  }

  logMsg += ToPlatformString(err->message);

  if (debug)
    logMsg += " Additional debug info: " + ToPlatformString(debug);

  MainPage::Current->AddLogMessage(logMsg);

  g_clear_error(&err);
  g_free(debug);
  g_free(name);
}

gboolean
GstWrapper::busHandler(GstBus* bus, GstMessage* msg, PipelineData* data)
{
  GstWrapper ^ self = data->self;
  GstElement* pipeline = data->pipeline;

  switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_CLOCK_LOST:
      gst_element_set_state(pipeline, GST_STATE_PAUSED);
      gst_element_set_state(pipeline, GST_STATE_PLAYING);
      break;
    case GST_MESSAGE_EOS:
      // DO SOMETHING
      break;
    case GST_MESSAGE_ERROR:
      LogPipelineErrorOrWarning(msg, true);
      break;
    case GST_MESSAGE_WARNING:
      LogPipelineErrorOrWarning(msg, false);
      break;
    // TODO: HANDLE MORE MESSAGE
    default:
      break;
  }

  return TRUE;
}

gboolean
GstWrapper::runPipeline(PipelineData* data)
{
  GstElement* pipeline = data->pipeline;

  GstBus* bus = gst_element_get_bus(pipeline);
  gst_bus_add_watch(bus, (GstBusFunc)GstWrapper::busHandler, data);
  gst_object_unref(bus);

  // FIXME: check return
  gst_element_set_state(pipeline, GST_STATE_PLAYING);

  return G_SOURCE_REMOVE;
}

unsigned int
GstWrapper::LaunchPipeline(GstElement* pipeline)
{
  std::unique_lock<std::mutex> Lock(lock_);
  unsigned int id;

  if (!pipeline || !GST_IS_PIPELINE(pipeline))
    return 0;

  id = ++pipelineId_;

  PipelineData* data = new PipelineData();

  data->self = this;
  data->pipeline = pipeline;

  pipelineList_.insert(std::make_pair(id, data));
  Lock.unlock();

  g_main_context_invoke(context_, (GSourceFunc)GstWrapper::runPipeline, data);

  return id;
}

gboolean
GstWrapper::stopPipeline(PipelineData* data)
{
  GstElement* pipeline = data->pipeline;

  GstBus* bus = gst_element_get_bus(pipeline);
  // Assuming bus watch was installed
  gst_bus_remove_watch(bus);
  gst_object_unref(bus);

  gst_element_set_state(pipeline, GST_STATE_NULL);
  gst_object_unref(pipeline);

  delete data;

  return G_SOURCE_REMOVE;
}

void
GstWrapper::DestroyPipeline(unsigned int id)
{
  std::unique_lock<std::mutex> Lock(lock_);
  auto iter = pipelineList_.find(id);

  if (iter == pipelineList_.end())
    return;

  PipelineData* data = iter->second;
  pipelineList_.erase(iter);

  Lock.unlock();

  g_main_context_invoke(context_, (GSourceFunc)GstWrapper::stopPipeline, data);
}

gboolean
GstWrapper::threadRunningCb(gpointer userData)
{
  GstWrapper ^ self = reinterpret_cast<GstWrapper ^>(userData);

  std::lock_guard<std::mutex> Lock(self->lock_);
  self->cond_.notify_one();

  return G_SOURCE_REMOVE;
}

gpointer
GstWrapper::threadFunc(gpointer userData)
{
  GstWrapper ^ self = reinterpret_cast<GstWrapper ^>(userData);
  GSource* source;

  g_main_context_push_thread_default(self->context_);
  source = g_idle_source_new();
  g_source_set_callback(
    source, (GSourceFunc)GstWrapper::threadRunningCb, (gpointer)self, nullptr);
  g_source_attach(source, self->context_);
  g_source_unref(source);

  g_main_loop_run(self->loop_);

  g_main_context_pop_thread_default(self->context_);

  return nullptr;
}

GstElement*
GstWrapper::GetHardwareVideoEncoder(const std::string& format)
{
  GstElementFactoryListType type = (GstElementFactoryListType)(
    GST_ELEMENT_FACTORY_TYPE_VIDEO_ENCODER | GST_ELEMENT_FACTORY_TYPE_HARDWARE);
  GList* allEncoders = nullptr;
  GList* targetEncoders = nullptr;
  GstCaps* caps = nullptr;
  GstElement* ret = nullptr;

  allEncoders = gst_element_factory_list_get_elements(type, GST_RANK_SECONDARY);
  if (!allEncoders) {
    MainPage::Current->AddLogMessage("No available hardware video encoder");
    return nullptr;
  }

  caps = gst_caps_new_empty_simple(format.c_str());
  if (!caps) {
    MainPage::Current->AddLogMessage("Invalid format " +
                                     ToPlatformString(format));
    goto done;
  }

  targetEncoders =
    gst_element_factory_list_filter(allEncoders, caps, GST_PAD_SRC, FALSE);
  if (!targetEncoders) {
    MainPage::Current->AddLogMessage("No available hardware video encoder for" +
                                     ToPlatformString(format));
    return nullptr;
  }

  // Return just the first one
  ret = gst_element_factory_create((GstElementFactory*)targetEncoders->data,
                                   nullptr);

done:
  if (allEncoders)
    gst_plugin_feature_list_free(allEncoders);
  if (targetEncoders)
    gst_plugin_feature_list_free(targetEncoders);
  if (caps)
    gst_caps_unref(caps);

  return ret;
}

GstElement*
GstWrapper::GetHardwareVideoDecoder(const std::string& format)
{
  GstElementFactoryListType type = (GstElementFactoryListType)(
    GST_ELEMENT_FACTORY_TYPE_DECODER | GST_ELEMENT_FACTORY_TYPE_MEDIA_VIDEO |
    GST_ELEMENT_FACTORY_TYPE_MEDIA_IMAGE | GST_ELEMENT_FACTORY_TYPE_HARDWARE);
  GList* allDecoders = nullptr;
  GList* targetDecoders = nullptr;
  GstCaps* caps = nullptr;
  GstElement* ret = nullptr;

  allDecoders = gst_element_factory_list_get_elements(type, GST_RANK_SECONDARY);
  if (!allDecoders) {
    MainPage::Current->AddLogMessage("No available hardware video decoder");
    return nullptr;
  }

  caps = gst_caps_new_empty_simple(format.c_str());
  if (!caps) {
    MainPage::Current->AddLogMessage("Invalid format " +
                                     ToPlatformString(format));
    goto done;
  }

  targetDecoders =
    gst_element_factory_list_filter(allDecoders, caps, GST_PAD_SINK, FALSE);
  if (!targetDecoders) {
    MainPage::Current->AddLogMessage("No available hardware video encoder for" +
                                     ToPlatformString(format));
    return nullptr;
  }

  // Return just the first one
  ret = gst_element_factory_create((GstElementFactory*)targetDecoders->data,
                                   nullptr);

done:
  if (allDecoders)
    gst_plugin_feature_list_free(allDecoders);
  if (targetDecoders)
    gst_plugin_feature_list_free(targetDecoders);
  if (caps)
    gst_caps_unref(caps);

  return ret;
}