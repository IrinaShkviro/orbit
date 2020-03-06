//-----------------------------------
// Copyright Pierric Gimmig 2013-2017
//-----------------------------------

#include "ScopeTimer.h"

#include "CoreApp.h"
#include "ConnectionManager.h"
#include "Log.h"
#include "TimerManager.h"
#include "absl/strings/str_format.h"

thread_local size_t CurrentDepth = 0;
thread_local size_t CurrentDepthLocal = 0;
thread_local size_t IntrospectionDepth = 0;

void Timer::Start() {
  m_TID = GetCurrentThreadId();
  m_Depth = CurrentDepth++;
  m_SessionID = Message::GSessionID;
  m_Start = OrbitTicks();
}

void Timer::Stop() {
  m_End = OrbitTicks();
  --CurrentDepth;
}

ScopeTimer::ScopeTimer(const char* a_Name) { m_Timer.Start(); }

ScopeTimer::~ScopeTimer() { m_Timer.Stop(); }

LocalScopeTimer::LocalScopeTimer() : millis_(nullptr) { ++CurrentDepthLocal; }

LocalScopeTimer::LocalScopeTimer(double* millis) : millis_(millis) {
  ++CurrentDepthLocal;
  timer_.Start();
}

LocalScopeTimer::LocalScopeTimer(const std::string& message)
    : millis_(nullptr), message_(message) {
  std::string tabs;
  for (size_t i = 0; i < CurrentDepthLocal; ++i) {
    tabs += "  ";
  }
  PRINT(absl::StrFormat("%sStarting %s...\n", tabs.c_str(), message_.c_str()));

  ++CurrentDepthLocal;
  timer_.Start();
}

LocalScopeTimer::~LocalScopeTimer() {
  timer_.Stop();
  --CurrentDepthLocal;

  if (millis_ != nullptr) {
    *millis_ = timer_.ElapsedMillis();
  }

  if (!message_.empty()) {
    std::string tabs;
    for (size_t i = 0; i < CurrentDepthLocal; ++i) {
      tabs += "  ";
    }

    PRINT(absl::StrFormat("%s%s took %f ms.\n", tabs.c_str(), message_.c_str(),
                          timer_.ElapsedMillis()));
  }
}

void ConditionalScopeTimer::Start(const char* a_Name) {
  m_Timer.Start();
  m_Active = true;
}

ConditionalScopeTimer::~ConditionalScopeTimer() {
  if (m_Active) {
    m_Timer.Stop();
  }
}

IntrospectionTimer::IntrospectionTimer(const std::string& message)
    : message_(message) {
  ++IntrospectionDepth;
  timer_.Start();
}

IntrospectionTimer::~IntrospectionTimer() {
  timer_.Stop();
  --IntrospectionDepth;

  if(ConnectionManager::Get().IsService()) {
    timer_.m_TID = GetCurrentThreadId();
    timer_.m_Type = Timer::INTROSPECTION;
    timer_.m_Depth = IntrospectionDepth;
    timer_.m_FunctionAddress = StringHash(message_);
    GCoreApp->ProcessTimer(timer_, message_);
  }
}
