// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <absl/strings/str_format.h>
#include <gtest/gtest.h>
#include <stdint.h>

#include <string>
#include <vector>

#include "OrbitClientData/FunctionUtils.h"
#include "OrbitClientData/ModuleData.h"
#include "OrbitClientData/ModuleManager.h"
#include "OrbitClientData/ProcessData.h"
#include "capture_data.pb.h"
#include "module.pb.h"
#include "process.pb.h"
#include "symbol.pb.h"

namespace orbit_client_data {

using orbit_client_protos::FunctionInfo;
using orbit_grpc_protos::ModuleInfo;
using orbit_grpc_protos::ModuleSymbols;
using orbit_grpc_protos::ProcessInfo;
using orbit_grpc_protos::SymbolInfo;

TEST(ModuleManager, GetModuleByPathAndBuildId) {
  std::string name = "name of module";
  std::string file_path = "path/of/module";
  uint64_t file_size = 300;
  std::string build_id = "build id 1";
  uint64_t load_bias = 0x400;

  ModuleInfo module_info;
  module_info.set_name(name);
  module_info.set_file_path(file_path);
  module_info.set_file_size(file_size);
  module_info.set_build_id(build_id);
  module_info.set_load_bias(load_bias);

  ModuleManager module_manager;
  module_manager.AddOrUpdateModules({module_info});

  const ModuleData* module = module_manager.GetModuleByPathAndBuildId(file_path, build_id);
  ASSERT_NE(module, nullptr);
  EXPECT_EQ(module->name(), name);
  EXPECT_EQ(module->file_path(), file_path);
  EXPECT_EQ(module->file_size(), file_size);
  EXPECT_EQ(module->build_id(), build_id);
  EXPECT_EQ(module->load_bias(), load_bias);

  const ModuleData* module_invalid_path =
      module_manager.GetModuleByPathAndBuildId("wrong/path", build_id);
  EXPECT_EQ(module_invalid_path, nullptr);

  const ModuleData* module_invalid_build_id =
      module_manager.GetModuleByPathAndBuildId(file_path, "wrong buildid");
  EXPECT_EQ(module_invalid_build_id, nullptr);
}

TEST(ModuleManager, GetMutableModuleByPathAndBuildId) {
  std::string name = "name of module";
  std::string file_path = "path/of/module";
  uint64_t file_size = 300;
  std::string build_id = "build id 1";
  uint64_t load_bias = 0x400;

  ModuleInfo module_info;
  module_info.set_name(name);
  module_info.set_file_path(file_path);
  module_info.set_file_size(file_size);
  module_info.set_build_id(build_id);
  module_info.set_load_bias(load_bias);

  ModuleManager module_manager;
  module_manager.AddOrUpdateModules({module_info});

  ModuleData* module = module_manager.GetMutableModuleByPathAndBuildId(file_path, build_id);
  ASSERT_NE(module, nullptr);
  EXPECT_EQ(module->name(), name);
  EXPECT_EQ(module->file_path(), file_path);
  EXPECT_EQ(module->file_size(), file_size);
  EXPECT_EQ(module->build_id(), build_id);
  EXPECT_EQ(module->load_bias(), load_bias);

  EXPECT_FALSE(module->is_loaded());
  module->AddSymbols({});
  EXPECT_TRUE(module->is_loaded());

  EXPECT_EQ(module_manager.GetMutableModuleByPathAndBuildId("wrong/path", build_id), nullptr);
  EXPECT_EQ(module_manager.GetMutableModuleByPathAndBuildId(file_path, "wrong build_id"), nullptr);
}

TEST(ModuleManager, AddOrUpdateModules) {
  const std::string name = "name of module";
  const std::string file_path = "path/of/module";
  const std::string build_id{};
  constexpr uint64_t kFileSize = 300;
  constexpr uint64_t kLoadBias = 0x400;

  ModuleInfo module_info;
  module_info.set_name(name);
  module_info.set_file_path(file_path);
  module_info.set_file_size(kFileSize);
  module_info.set_build_id(build_id);
  module_info.set_load_bias(kLoadBias);

  ModuleManager module_manager;
  std::vector<ModuleData*> unloaded_modules = module_manager.AddOrUpdateModules({module_info});
  EXPECT_TRUE(unloaded_modules.empty());

  ModuleData* module = module_manager.GetMutableModuleByPathAndBuildId(file_path, build_id);

  ASSERT_NE(module, nullptr);
  EXPECT_EQ(module->name(), name);

  // change name, this updates the module
  std::string changed_name = "changed name";
  module_info.set_name(changed_name);

  unloaded_modules = module_manager.AddOrUpdateModules({module_info});
  EXPECT_TRUE(unloaded_modules.empty());

  ASSERT_NE(module, nullptr);
  EXPECT_EQ(module->file_path(), file_path);
  EXPECT_EQ(module->name(), changed_name);

  // add symbols
  ModuleSymbols module_symbols;
  module->AddSymbols(module_symbols);
  ASSERT_TRUE(module->is_loaded());

  // change build id, this creates new module
  constexpr const char* kDifferentBuildId = "different build id";
  module_info.set_build_id(kDifferentBuildId);
  unloaded_modules = module_manager.AddOrUpdateModules({module_info});
  EXPECT_TRUE(unloaded_modules.empty());

  EXPECT_EQ(module->build_id(), build_id);
  EXPECT_TRUE(module->is_loaded());

  // change file path & file size and add again (should be added again, because it is now considered
  // a different module)
  std::string different_path = "different/path/of/module";
  module_info.set_file_path(different_path);
  uint64_t different_file_size = 301;
  module_info.set_file_size(different_file_size);
  unloaded_modules = module_manager.AddOrUpdateModules({module_info});
  EXPECT_TRUE(unloaded_modules.empty());

  ASSERT_NE(module, nullptr);
  EXPECT_EQ(module->file_path(), file_path);
  EXPECT_EQ(module->file_size(), kFileSize);

  const ModuleData* different_module =
      module_manager.GetModuleByPathAndBuildId(different_path, kDifferentBuildId);
  ASSERT_NE(different_module, nullptr);
  EXPECT_EQ(different_module->file_path(), different_path);
  EXPECT_EQ(different_module->file_size(), different_file_size);
}

TEST(ModuleManager, GetOrbitFunctionsOfProcess) {
  constexpr const char* kName = "name of module";
  constexpr const char* kFilePath = "path/of/module";
  constexpr const char* kBuildId = "build id example";
  constexpr const char* kBuildIdChanged = "another build id";
  constexpr uint64_t kFileSize = 300;
  constexpr uint64_t kLoadBias = 0x400;
  constexpr uint64_t kAddressStart = 100;
  constexpr uint64_t kAddressEnd = 1000;

  ModuleInfo module_info;
  module_info.set_name(kName);
  module_info.set_file_path(kFilePath);
  module_info.set_file_size(kFileSize);
  module_info.set_build_id(kBuildId);
  module_info.set_load_bias(kLoadBias);
  module_info.set_address_start(kAddressStart);
  module_info.set_address_end(kAddressEnd);

  ModuleManager module_manager;
  module_manager.AddOrUpdateModules({module_info});

  ProcessInfo process_info;
  ProcessData process{process_info};

  {
    // process does not have any modules loaded
    std::vector<FunctionInfo> functions = module_manager.GetOrbitFunctionsOfProcess(process);
    EXPECT_TRUE(functions.empty());
  }

  process.UpdateModuleInfos({module_info});
  {
    // process has module loaded, but module does not have symbols loaded
    std::vector<FunctionInfo> functions = module_manager.GetOrbitFunctionsOfProcess(process);
    EXPECT_TRUE(functions.empty());
  }

  ModuleSymbols module_symbols;
  {
    SymbolInfo* symbol_info = module_symbols.add_symbol_infos();
    symbol_info->set_name("mangled_not_an_orbit_function");
    symbol_info->set_demangled_name("not an orbit function");
  }

  const auto& orbit_name_to_orbit_type_map = function_utils::GetFunctionNameToOrbitTypeMap();
  const std::string orbit_name = orbit_name_to_orbit_type_map.begin()->first;
  const std::string orbit_name_mangled = absl::StrFormat("mangled_%s", orbit_name);

  {
    SymbolInfo* symbol_info = module_symbols.add_symbol_infos();
    symbol_info->set_name(orbit_name_mangled);
    symbol_info->set_demangled_name(orbit_name);
    symbol_info->set_address(500);
  }

  ModuleData* module = module_manager.GetMutableModuleByPathAndBuildId(kFilePath, kBuildId);
  module->AddSymbols(module_symbols);

  {
    // process has module loaded and module has functions loaded. One of the functions is an orbit
    // function
    std::vector<FunctionInfo> functions = module_manager.GetOrbitFunctionsOfProcess(process);
    ASSERT_EQ(functions.size(), 1);

    FunctionInfo& function{functions[0]};
    EXPECT_EQ(function.name(), orbit_name_mangled);
    EXPECT_EQ(function.pretty_name(), orbit_name);
    EXPECT_EQ(function.address(), 500);
  }

  module_info.set_build_id(kBuildIdChanged);
  // Add same module with another build_id
  module_manager.AddOrUpdateModules({module_info});
  {
    // We should still be able to lookup the function because old module did not go anywhere.
    std::vector<FunctionInfo> functions = module_manager.GetOrbitFunctionsOfProcess(process);
    ASSERT_EQ(functions.size(), 1);

    FunctionInfo& function{functions[0]};
    EXPECT_EQ(function.name(), orbit_name_mangled);
    EXPECT_EQ(function.pretty_name(), orbit_name);
    EXPECT_EQ(function.address(), 500);
  }

  // Now update the process modules in memory.
  process.UpdateModuleInfos({module_info});
  {
    // Now ModuleManager should not be able to find the functions because the module is no longer in
    // the memory.
    std::vector<FunctionInfo> functions = module_manager.GetOrbitFunctionsOfProcess(process);
    EXPECT_TRUE(functions.empty());
  }
}

}  // namespace orbit_client_data
