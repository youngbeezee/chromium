// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_RESOURCE_COORDINATOR_PUBLIC_CPP_MEMORY_MEMORY_INSTRUMENTATION_STRUCT_TRAITS_H_
#define SERVICES_RESOURCE_COORDINATOR_PUBLIC_CPP_MEMORY_MEMORY_INSTRUMENTATION_STRUCT_TRAITS_H_

#include "base/process/process_handle.h"
#include "base/trace_event/memory_dump_request_args.h"
#include "mojo/common/common_custom_types_struct_traits.h"
#include "services/resource_coordinator/public/cpp/memory/memory_export.h"
#include "services/resource_coordinator/public/interfaces/memory/memory_instrumentation.mojom.h"

namespace mojo {

template <>
struct SERVICES_RESOURCE_COORDINATOR_PUBLIC_CPP_MEMORY_EXPORT
    EnumTraits<memory_instrumentation::mojom::DumpType,
               base::trace_event::MemoryDumpType> {
  static memory_instrumentation::mojom::DumpType ToMojom(
      base::trace_event::MemoryDumpType type);
  static bool FromMojom(memory_instrumentation::mojom::DumpType input,
                        base::trace_event::MemoryDumpType* out);
};

template <>
struct SERVICES_RESOURCE_COORDINATOR_PUBLIC_CPP_MEMORY_EXPORT
    EnumTraits<memory_instrumentation::mojom::LevelOfDetail,
               base::trace_event::MemoryDumpLevelOfDetail> {
  static memory_instrumentation::mojom::LevelOfDetail ToMojom(
      base::trace_event::MemoryDumpLevelOfDetail level_of_detail);
  static bool FromMojom(memory_instrumentation::mojom::LevelOfDetail input,
                        base::trace_event::MemoryDumpLevelOfDetail* out);
};

template <>
struct SERVICES_RESOURCE_COORDINATOR_PUBLIC_CPP_MEMORY_EXPORT
    StructTraits<memory_instrumentation::mojom::RequestArgsDataView,
                 base::trace_event::MemoryDumpRequestArgs> {
  static uint64_t dump_guid(
      const base::trace_event::MemoryDumpRequestArgs& args) {
    return args.dump_guid;
  }
  static base::trace_event::MemoryDumpType dump_type(
      const base::trace_event::MemoryDumpRequestArgs& args) {
    return args.dump_type;
  }
  static base::trace_event::MemoryDumpLevelOfDetail level_of_detail(
      const base::trace_event::MemoryDumpRequestArgs& args) {
    return args.level_of_detail;
  }
  static bool Read(memory_instrumentation::mojom::RequestArgsDataView input,
                   base::trace_event::MemoryDumpRequestArgs* out);
};

template <>
struct SERVICES_RESOURCE_COORDINATOR_PUBLIC_CPP_MEMORY_EXPORT
    StructTraits<memory_instrumentation::mojom::ChromeMemDumpDataView,
                 base::trace_event::MemoryDumpCallbackResult::ChromeMemDump> {
  static uint32_t malloc_total_kb(
      const base::trace_event::MemoryDumpCallbackResult::ChromeMemDump& args) {
    return args.malloc_total_kb;
  }
  static uint32_t partition_alloc_total_kb(
      const base::trace_event::MemoryDumpCallbackResult::ChromeMemDump& args) {
    return args.partition_alloc_total_kb;
  }
  static uint32_t blink_gc_total_kb(
      const base::trace_event::MemoryDumpCallbackResult::ChromeMemDump& args) {
    return args.blink_gc_total_kb;
  }
  static uint32_t v8_total_kb(
      const base::trace_event::MemoryDumpCallbackResult::ChromeMemDump& args) {
    return args.v8_total_kb;
  }
  static bool Read(
      memory_instrumentation::mojom::ChromeMemDumpDataView input,
      base::trace_event::MemoryDumpCallbackResult::ChromeMemDump* out);
};

template <>
struct SERVICES_RESOURCE_COORDINATOR_PUBLIC_CPP_MEMORY_EXPORT
    StructTraits<memory_instrumentation::mojom::OSMemDumpDataView,
                 base::trace_event::MemoryDumpCallbackResult::OSMemDump> {
  static uint32_t resident_set_kb(
      const base::trace_event::MemoryDumpCallbackResult::OSMemDump& args) {
    return args.resident_set_kb;
  }
  static bool Read(memory_instrumentation::mojom::OSMemDumpDataView input,
                   base::trace_event::MemoryDumpCallbackResult::OSMemDump* out);
};

template <>
struct SERVICES_RESOURCE_COORDINATOR_PUBLIC_CPP_MEMORY_EXPORT StructTraits<
    memory_instrumentation::mojom::MemoryDumpCallbackResultDataView,
    base::trace_event::MemoryDumpCallbackResult> {
  static base::trace_event::MemoryDumpCallbackResult::OSMemDump os_dump(
      const base::trace_event::MemoryDumpCallbackResult& args) {
    return args.os_dump;
  }
  static base::trace_event::MemoryDumpCallbackResult::ChromeMemDump chrome_dump(
      const base::trace_event::MemoryDumpCallbackResult& args) {
    return args.chrome_dump;
  }
  static const std::map<base::ProcessId,
                        base::trace_event::MemoryDumpCallbackResult::OSMemDump>&
  extra_processes_dump(
      const base::trace_event::MemoryDumpCallbackResult& args) {
    return args.extra_processes_dump;
  }
  static bool Read(
      memory_instrumentation::mojom::MemoryDumpCallbackResultDataView input,
      base::trace_event::MemoryDumpCallbackResult* out);
};

}  // namespace mojo

#endif  // SERVICES_RESOURCE_COORDINATOR_PUBLIC_CPP_MEMORY_MEMORY_INSTRUMENTATION_STRUCT_TRAITS_H_
