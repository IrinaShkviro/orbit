// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "UprobesUnwindingVisitor.h"

#include <absl/container/flat_hash_map.h>
#include <asm/perf_regs.h>
#include <llvm/Demangle/Demangle.h>
#include <sys/mman.h>
#include <unwindstack/MapInfo.h>
#include <unwindstack/Unwinder.h>

#include <algorithm>
#include <array>
#include <optional>
#include <utility>

#include "ElfUtils/LinuxMap.h"
#include "Function.h"
#include "OrbitBase/Logging.h"
#include "OrbitBase/Result.h"
#include "PerfEventRecords.h"
#include "capture.pb.h"
#include "module.pb.h"

namespace orbit_linux_tracing {

using orbit_grpc_protos::Callstack;
using orbit_grpc_protos::FullAddressInfo;
using orbit_grpc_protos::FullCallstackSample;
using orbit_grpc_protos::FunctionCall;

void UprobesUnwindingVisitor::visit(StackSamplePerfEvent* event) {
  CHECK(listener_ != nullptr);

  if (current_maps_ == nullptr) {
    return;
  }

  return_address_manager_.PatchSample(event->GetTid(), event->GetRegisters()[PERF_REG_X86_SP],
                                      event->GetStackData(), event->GetStackSize());

  const std::vector<unwindstack::FrameData>& libunwindstack_callstack =
      unwinder_.Unwind(event->GetPid(), current_maps_.get(), event->GetRegisters(),
                       event->GetStackData(), event->GetStackSize());

  // LibunwindstackUnwinder::Unwind signals an unwinding error with an empty callstack.
  if (libunwindstack_callstack.empty()) {
    if (unwind_error_counter_ != nullptr) {
      ++(*unwind_error_counter_);
    }
    return;
  }

  // Callstacks with only one frame (the sampled address) are also unwinding errors, that were not
  // reported as such by LibunwindstackUnwinder::Unwind.
  // Note that this doesn't exclude samples inside the main function of any thread as the main
  // function is never the outermost frame. For example, for the main thread the outermost function
  // is _start, followed by __libc_start_main. For other threads, the outermost function is clone.
  if (libunwindstack_callstack.size() == 1) {
    if (unwind_error_counter_ != nullptr) {
      ++(*unwind_error_counter_);
    }
    return;
  }

  // Some samples can actually fall inside u(ret)probes code. Discard them,
  // because when they are unwound successfully the result is wrong.
  if (libunwindstack_callstack.front().map_name == "[uprobes]") {
    if (discarded_samples_in_uretprobes_counter_ != nullptr) {
      ++(*discarded_samples_in_uretprobes_counter_);
    }
    return;
  }

  FullCallstackSample sample;
  sample.set_pid(event->GetPid());
  sample.set_tid(event->GetTid());
  sample.set_timestamp_ns(event->GetTimestamp());

  Callstack* callstack = sample.mutable_callstack();
  for (const unwindstack::FrameData& libunwindstack_frame : libunwindstack_callstack) {
    FullAddressInfo address_info;
    address_info.set_absolute_address(libunwindstack_frame.pc);
    address_info.set_function_name(llvm::demangle(libunwindstack_frame.function_name));
    address_info.set_offset_in_function(libunwindstack_frame.function_offset);
    address_info.set_module_name(libunwindstack_frame.map_name);
    listener_->OnAddressInfo(std::move(address_info));

    callstack->add_pcs(libunwindstack_frame.pc);
  }

  listener_->OnCallstackSample(std::move(sample));
}

void UprobesUnwindingVisitor::visit(CallchainSamplePerfEvent* event) {
  CHECK(listener_ != nullptr);

  if (current_maps_ == nullptr) {
    return;
  }

  // TODO(b/179976268): When a sample falls on the first (push rbp) or second (mov rbp,rsp)
  //  instruction of the current function, frame-pointer unwinding skips the caller's frame,
  //  because rbp hasn't yet been updated to rsp. Drop the sample in this case?

  if (!return_address_manager_.PatchCallchain(event->GetTid(), event->GetCallchain(),
                                              event->GetCallchainSize(), current_maps_.get())) {
    return;
  }

  // The top of a callchain is always inside the kernel code.
  if (event->GetCallchainSize() <= 1) {
    return;
  }

  uint64_t top_ip = event->GetCallchain()[1];
  unwindstack::MapInfo* top_ip_map_info = current_maps_->Find(top_ip);

  // Some samples can actually fall inside u(ret)probes code. Discard them,
  // as we don't want to show the unnamed uprobes module in the samples.
  if (top_ip_map_info == nullptr || top_ip_map_info->name == "[uprobes]") {
    if (discarded_samples_in_uretprobes_counter_ != nullptr) {
      ++(*discarded_samples_in_uretprobes_counter_);
    }
    return;
  }

  FullCallstackSample sample;
  sample.set_pid(event->GetPid());
  sample.set_tid(event->GetTid());
  sample.set_timestamp_ns(event->GetTimestamp());

  Callstack* callstack = sample.mutable_callstack();
  uint64_t* raw_callchain = event->GetCallchain();
  // Skip the first frame as the top of a perf_event_open callchain is always
  // inside kernel code.
  callstack->add_pcs(raw_callchain[1]);
  // Only the address of the top of the stack is correct. Frame-based unwinding
  // uses the return address of a function call as the caller's address.
  // However, the actual address of the call instruction is before that.
  // As we don't know the size of the call instruction, we subtract 1 from the
  // return address. This way we fall into the range of the call instruction.
  // Note: This is also done the same way in Libunwindstack.
  for (uint64_t frame_index = 2; frame_index < event->GetCallchainSize(); ++frame_index) {
    callstack->add_pcs(raw_callchain[frame_index] - 1);
  }

  listener_->OnCallstackSample(std::move(sample));
}

void UprobesUnwindingVisitor::visit(UprobesPerfEvent* event) {
  CHECK(listener_ != nullptr);

  // We are seeing that, on thread migration, uprobe events can sometimes be
  // duplicated: the duplicate uprobe event will have the same stack pointer and
  // instruction pointer as the previous uprobe, but different cpu. In that
  // situation, we discard the second uprobe event.
  // We also discard a uprobe event in the general case of strictly-increasing
  // stack pointers, as for a given thread's sequence of u(ret)probe events, two
  // consecutive uprobe events must be associated with non-increasing stack
  // pointers (the stack grows towards lower addresses).

  // Duplicate uprobe detection.
  uint64_t uprobe_sp = event->GetSp();
  uint64_t uprobe_ip = event->GetIp();
  uint32_t uprobe_cpu = event->GetCpu();
  std::vector<std::tuple<uint64_t, uint64_t, uint32_t>>& uprobe_sps_ips_cpus =
      uprobe_sps_ips_cpus_per_thread_[event->GetTid()];
  if (!uprobe_sps_ips_cpus.empty()) {
    uint64_t last_uprobe_sp = std::get<0>(uprobe_sps_ips_cpus.back());
    uint64_t last_uprobe_ip = std::get<1>(uprobe_sps_ips_cpus.back());
    uint32_t last_uprobe_cpu = std::get<2>(uprobe_sps_ips_cpus.back());
    uprobe_sps_ips_cpus.pop_back();
    if (uprobe_sp > last_uprobe_sp) {
      ERROR("MISSING URETPROBE OR DUPLICATE UPROBE");
      return;
    }
    if (uprobe_sp == last_uprobe_sp && uprobe_ip == last_uprobe_ip &&
        uprobe_cpu != last_uprobe_cpu) {
      ERROR("Duplicate uprobe on thread migration");
      return;
    }
  }
  uprobe_sps_ips_cpus.emplace_back(uprobe_sp, uprobe_ip, uprobe_cpu);

  function_call_manager_.ProcessUprobes(event->GetTid(), event->GetFunction()->function_id(),
                                        event->GetTimestamp(), event->ring_buffer_record.regs);

  return_address_manager_.ProcessUprobes(event->GetTid(), event->GetSp(),
                                         event->GetReturnAddress());
}

void UprobesUnwindingVisitor::visit(UretprobesPerfEvent* event) {
  CHECK(listener_ != nullptr);

  // Duplicate uprobe detection.
  std::vector<std::tuple<uint64_t, uint64_t, uint32_t>>& uprobe_sps_ips_cpus =
      uprobe_sps_ips_cpus_per_thread_[event->GetTid()];
  if (!uprobe_sps_ips_cpus.empty()) {
    uprobe_sps_ips_cpus.pop_back();
  }

  std::optional<FunctionCall> function_call = function_call_manager_.ProcessUretprobes(
      event->GetPid(), event->GetTid(), event->GetTimestamp(), event->GetAx());
  if (function_call.has_value()) {
    listener_->OnFunctionCall(std::move(function_call.value()));
  }

  return_address_manager_.ProcessUretprobes(event->GetTid());
}

void UprobesUnwindingVisitor::visit(MmapPerfEvent* event) {
  CHECK(listener_ != nullptr);

  // Obviously the uprobes map cannot be successfully processed by orbit_elf_utils::CreateModule,
  // but it's important that current_maps_ contain it.
  // For example, UprobesReturnAddressManager::PatchCallchain needs it to check whether a program
  // counter is inside the uprobes map, and UprobesUnwindingVisitor::visit(StackSamplePerfEvent*)
  // needs it to throw away incorrectly-unwound samples.
  // As below we are only adding maps successfully parsed with orbit_elf_utils::CreateModule,
  // we add the uprobes map manually. We are using the same values that that uprobes map would get
  // if unwindstack::BufferMaps was built by passing the full content of /proc/<pid>/maps to its
  // constructor.
  if (event->filename() == "[uprobes]") {
    current_maps_->Add(event->address(), event->address() + event->length(), 0, PROT_EXEC,
                       event->filename(), INT64_MAX);
    current_maps_->Sort();
    return;
  }

  ErrorMessageOr<orbit_grpc_protos::ModuleInfo> module_info_or_error =
      orbit_elf_utils::CreateModule(event->filename(), event->address(),
                                    event->address() + event->length());
  if (module_info_or_error.has_error()) {
    ERROR("Unable to create module: %s", module_info_or_error.error().message());
    return;
  }

  auto& module_info = module_info_or_error.value();

  // For flags we assume PROT_READ and PROT_EXEC, MMAP event does not return flags.
  current_maps_->Add(module_info.address_start(), module_info.address_end(), event->page_offset(),
                     PROT_READ | PROT_EXEC, event->filename(), module_info.load_bias());

  // This Sort is important here since libunwindstack does binary search for module by pc.
  current_maps_->Sort();

  orbit_grpc_protos::ModuleUpdateEvent module_update_event;
  module_update_event.set_pid(event->pid());
  module_update_event.set_timestamp_ns(event->GetTimestamp());
  *module_update_event.mutable_module() = std::move(module_info);

  listener_->OnModuleUpdate(std::move(module_update_event));
}

}  // namespace orbit_linux_tracing
