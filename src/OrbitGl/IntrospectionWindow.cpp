// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "IntrospectionWindow.h"

#include "App.h"
#include "OrbitBase/Logging.h"
#include "StringManager.h"
#include "TimeGraph.h"
#include "capture_data.pb.h"

using orbit_client_protos::TimerInfo;

IntrospectionWindow::IntrospectionWindow(OrbitApp* app) : CaptureWindow(app) {}

IntrospectionWindow::~IntrospectionWindow() { StopIntrospection(); }

const char* IntrospectionWindow::GetHelpText() const {
  const char* help_message =
      "Client Side Introspection\n\n"
      "Start/Stop Capture: 'X'\n"
      "Toggle Help: 'H'";
  return help_message;
}

bool IntrospectionWindow::IsIntrospecting() const { return introspection_listener_ != nullptr; }

void IntrospectionWindow::StartIntrospection() {
  CHECK(!IsIntrospecting());
  draw_help_ = false;
  CreateTimeGraph(nullptr);
  introspection_listener_ =
      std::make_unique<orbit_base::TracingListener>([this](const orbit_base::TracingScope& scope) {
        TimerInfo timer_info;
        timer_info.set_thread_id(scope.tid);
        timer_info.set_start(scope.begin);
        timer_info.set_end(scope.end);
        timer_info.set_depth(static_cast<uint8_t>(scope.depth));
        timer_info.set_type(TimerInfo::kIntrospection);
        timer_info.mutable_registers()->Reserve(6);
        timer_info.add_registers(scope.encoded_event.args[0]);
        timer_info.add_registers(scope.encoded_event.args[1]);
        timer_info.add_registers(scope.encoded_event.args[2]);
        timer_info.add_registers(scope.encoded_event.args[3]);
        timer_info.add_registers(scope.encoded_event.args[4]);
        timer_info.add_registers(scope.encoded_event.args[5]);
        time_graph_->ProcessTimer(timer_info, /*FunctionInfo*/ nullptr);
      });
}
void IntrospectionWindow::StopIntrospection() { introspection_listener_ = nullptr; }

void IntrospectionWindow::ToggleRecording() {
  if (!IsIntrospecting()) {
    StartIntrospection();
  } else {
    StopIntrospection();
  }
}

void IntrospectionWindow::RenderImGuiDebugUI() {
  CaptureWindow::RenderImGuiDebugUI();

  if (ImGui::CollapsingHeader("IntrospectionWindow")) {
    IMGUI_VAR_TO_TEXT(IsIntrospecting());
  }
}

bool IntrospectionWindow::ShouldAutoZoom() const { return IsIntrospecting(); }
