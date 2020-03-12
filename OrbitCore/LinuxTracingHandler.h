#ifndef ORBIT_CORE_LINUX_TRACING_HANDLER_H_
#define ORBIT_CORE_LINUX_TRACING_HANDLER_H_

#include <OrbitLinuxTracing/Tracer.h>
#include <OrbitLinuxTracing/TracerListener.h>

#include "CoreApp.h"
#include "OrbitProcess.h"
#include "ScopeTimer.h"
#include "absl/container/flat_hash_map.h"

class LinuxTracingHandler : LinuxTracing::TracerListener {
 public:
  static constexpr double DEFAULT_SAMPLING_FREQUENCY = 99.0;

  LinuxTracingHandler(CoreApp* core_app, Process* target_process,
                      std::map<ULONG64, Function*>* selected_function_map,
                      ULONG64* num_context_switches)
      : core_app_{core_app},
        target_process_{target_process},
        selected_function_map_{selected_function_map},
        num_context_switches_{num_context_switches} {}

  ~LinuxTracingHandler() = default;
  LinuxTracingHandler(const LinuxTracingHandler&) = delete;
  LinuxTracingHandler& operator=(const LinuxTracingHandler&) = delete;
  LinuxTracingHandler(LinuxTracingHandler&&) = default;
  LinuxTracingHandler& operator=(LinuxTracingHandler&&) = default;

  void Start();

  void Stop();

  void OnTid(pid_t tid) override;
  void OnContextSwitchIn(
      const LinuxTracing::ContextSwitchIn& context_switch_in) override;
  void OnContextSwitchOut(
      const LinuxTracing::ContextSwitchOut& context_switch_out) override;
  void OnCallstack(const LinuxTracing::Callstack& callstack) override;
  void OnFunctionBegin(
      const LinuxTracing::FunctionBegin& function_begin) override;
  void OnFunctionEnd(const LinuxTracing::FunctionEnd& function_end) override;
  void OnGpuExecutionEvent(const LinuxTracing::GpuExecutionEvent& gpu_event) override;

 private:
  CoreApp* core_app_;
  Process* target_process_;
  std::map<ULONG64, Function*>* selected_function_map_;
  ULONG64* num_context_switches_;

  std::unique_ptr<LinuxTracing::Tracer> tracer_;
  absl::flat_hash_map<pid_t, std::stack<Timer, std::vector<Timer>>>
      tid_timer_stacks_{};
  pid_t TimelineToThreadId(std::string timeline);
  absl::flat_hash_map<std::string, pid_t> timeline_to_thread_id_;
  pid_t current_timeline_thread_id = 100000;
};

#endif  // ORBIT_CORE_LINUX_TRACING_HANDLER_H_
