// Copyright (C) 2022 Seungha Yang <seungha@centricular.com>
//
// SPDX-License-Identifier: MIT

#include "pch.h"
#include "MainPage.xaml.h"
#include "GstWrapper.h"
#include "Utils.h"
#include <string>

using namespace concurrency;

using namespace gst_uwp_example;

using namespace Platform;
using namespace Windows::Storage;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Controls::Primitives;
using namespace Windows::UI::Xaml::Data;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Navigation;
using namespace Windows::UI::Xaml::Interop;
using namespace Windows::System;

// The Blank Page item template is documented at
// https://go.microsoft.com/fwlink/?LinkId=402352&clcid=0x409

MainPage ^ MainPage::Current = nullptr;

MainPage::MainPage()
{
  InitializeComponent();
  SampleTitle->Text = FEATURE_NAME;

  MainPage::Current = this;
}

void MainPage::OnNavigatedTo(NavigationEventArgs ^ e)
{
  // Init gstreamer
  SetupLogger();

  auto gstHelper = GstWrapper::Instance::get();

  // Print log if there was plugin load fail
  auto pluginList = gstHelper->GetPluginList();
  for (auto iter : pluginList) {
    if (iter.second)
      continue;

    Platform::String ^ msg =
      "[WARNING] Failed to load " + "\"" + ToPlatformString(iter.first) + "\"";
    AddLogMessageAsync(msg);
  }

  // Populate the ListBox with the scenarios as defined in
  // SampleConfiguration.cpp.
  auto itemCollection = ref new Platform::Collections::Vector<Object ^>();
  for (auto s : MainPage::Current->scenarios)
    itemCollection->Append(s);

  // Set the newly created itemCollection as the ListBox ItemSource.
  ScenarioControl->ItemsSource = itemCollection;
  int startingScenarioIndex;

  if (Window::Current->Bounds.Width < 640)
    startingScenarioIndex = -1;
  else
    startingScenarioIndex = 0;

  ScenarioControl->SelectedIndex = startingScenarioIndex;
  ScenarioControl->ScrollIntoView(ScenarioControl->SelectedItem);
}

void MainPage::ScenarioControl_SelectionChanged(Object ^ sender,
                                                SelectionChangedEventArgs ^ e)
{
  ListBox ^ scenarioListBox = safe_cast<ListBox ^>(sender);
  if (scenarioListBox->SelectedItem != nullptr) {
    // Navigate to the selected scenario.
    Scenario s = (Scenario)scenarioListBox->SelectedItem;
    TypeName scenarioType = { s.ClassName, TypeKind::Custom };
    ScenarioFrame->Navigate(scenarioType, this);

    if (Window::Current->Bounds.Width < 640)
      Splitter->IsPaneOpen = false;
  }
}

void MainPage::Button_Click(Object ^ sender, RoutedEventArgs ^ e)
{
  Splitter->IsPaneOpen = !Splitter->IsPaneOpen;
}

void MainPage::AddLogMessage(Platform::String ^ message)
{
  AddLogMessageAsync(message);
}

void MainPage::AddLogMessageInternal(Platform::String ^ message)
{
  LoggingListBox->Items->InsertAt(0, message);

  // Do not hold too many log messages
  while (LoggingListBox->Items->Size > 500)
    LoggingListBox->Items->RemoveAtEnd();
}

concurrency::task<void> MainPage::AddLogMessageAsync(Platform::String ^ message)
{
  return create_task(
    Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Low,
                         ref new Windows::UI::Core::DispatchedHandler(
                           [=]() { AddLogMessageInternal(message); })));
}

void MainPage::UpdateStatusMessage(Platform::String ^ message)
{
  UpdateStatusMessageAsync("Status: " + message);
}

void MainPage::UpdateStatusMessageInternal(Platform::String ^ message)
{
  StatusLabel->Text = message;
}

concurrency::task<void> MainPage::UpdateStatusMessageAsync(Platform::String ^
                                                           message)
{
  return create_task(
    Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Low,
                         ref new Windows::UI::Core::DispatchedHandler(
                           [=]() { UpdateStatusMessageInternal(message); })));
}

void
MainPage::SetupLogger(void)
{
  // install our log handler
  gst_debug_add_log_function(
    (GstLogFunction)MainPage::GstDebugCb, nullptr, nullptr);

  // Can be updated via UI
  gst_debug_set_default_threshold(GST_LEVEL_ERROR);

  std::string appDir = ToStdString(ApplicationData::Current->LocalFolder->Path);
  std::string logFileName = appDir + "\\gst.log";

  g_setenv("GST_DEBUG_FILE", logFileName.c_str(), TRUE);
}

void
MainPage::GstDebugCb(GstDebugCategory* category,
                     GstDebugLevel level,
                     const gchar* file,
                     const gchar* function,
                     gint line,
                     GObject* obj,
                     GstDebugMessage* message,
                     gpointer user_data)
{
  // XXX: In case of higher log level then WARNING,
  // it would hurt UI tasks.
  if ((guint)level > (guint)GST_LEVEL_WARNING)
    return;

  gchar* msg =
    gst_debug_log_get_line(category, level, file, function, line, obj, message);

  std::string msgStr = std::string(msg);
  g_free(msg);

  // Drop training newline if any
  msgStr = msgStr.erase(msgStr.find_last_not_of("\t\n") + 1);

  Platform::String ^ platformMsg = ToPlatformString(msgStr);

  Current->AddLogMessageAsync(platformMsg);
}

void MainPage::btnOpenAppDir_Click(Platform::Object ^ sender,
                                   Windows::UI::Xaml::RoutedEventArgs ^ e)
{
  Launcher::LaunchFolderPathAsync(ApplicationData::Current->LocalFolder->Path);
}

void MainPage::comboDebugLevel_SelectionChanged(Object^ sender, SelectionChangedEventArgs^ e)
{
  GstDebugLevel level = GST_LEVEL_ERROR;
  String^ newLevel = e->AddedItems->GetAt(0)->ToString();

  if (newLevel == "NONE") {
    level = GST_LEVEL_NONE;
  } else if (newLevel == "ERROR") {
    level = GST_LEVEL_NONE;
  } else if (newLevel == "WARNING") {
    level = GST_LEVEL_WARNING;
  } else if (newLevel == "FIXME") {
    level = GST_LEVEL_FIXME;
  } else if (newLevel == "INFO") {
    level = GST_LEVEL_INFO;
  } else if (newLevel == "DEBUG") {
    level = GST_LEVEL_DEBUG;
  } else if (newLevel == "LOG") {
    level = GST_LEVEL_LOG;
  } else if (newLevel == "TRACE") {
    level = GST_LEVEL_TRACE;
  }

  gst_debug_set_default_threshold(level);
}
