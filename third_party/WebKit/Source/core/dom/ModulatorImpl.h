// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ModulatorImpl_h
#define ModulatorImpl_h

#include "bindings/core/v8/ScriptModule.h"
#include "bindings/core/v8/ScriptWrappable.h"
#include "bindings/core/v8/TraceWrapperMember.h"
#include "bindings/core/v8/V8PerIsolateData.h"
#include "core/dom/Modulator.h"
#include "platform/heap/Handle.h"

namespace blink {

class Document;
class ExecutionContext;
class ModuleMap;
class ModuleScriptLoaderRegistry;
class ModuleTreeLinkerRegistry;
class ResourceFetcher;
class ScriptState;
class WebTaskRunner;

// ModulatorImpl is the implementation of Modulator interface, which represents
// "environment settings object" concept for module scripts.
// ModulatorImpl serves as the backplane for tieing all ES6 module algorithm
// components together.
class ModulatorImpl final : public Modulator {
 public:
  static ModulatorImpl* Create(RefPtr<ScriptState>, Document&);

  virtual ~ModulatorImpl();
  DECLARE_TRACE();
  DECLARE_VIRTUAL_TRACE_WRAPPERS();

 private:
  // Implements Modulator

  ScriptModuleResolver* GetScriptModuleResolver() override {
    return script_module_resolver_.Get();
  }
  WebTaskRunner* TaskRunner() override { return task_runner_.Get(); }
  ReferrerPolicy GetReferrerPolicy() override;
  SecurityOrigin* GetSecurityOrigin() override;

  void FetchTree(const ModuleScriptFetchRequest&, ModuleTreeClient*) override;
  void FetchDescendantsForInlineScript(ModuleScript*,
                                       ModuleTreeClient*) override;
  void FetchTreeInternal(const ModuleScriptFetchRequest&,
                         const AncestorList&,
                         ModuleGraphLevel,
                         ModuleTreeClient*) override;
  void FetchSingle(const ModuleScriptFetchRequest&,
                   ModuleGraphLevel,
                   SingleModuleClient*) override;
  ModuleScript* GetFetchedModuleScript(const KURL&) override;
  void FetchNewSingleModule(const ModuleScriptFetchRequest&,
                            ModuleGraphLevel,
                            ModuleScriptLoaderClient*) override;
  ScriptModule CompileModule(const String& script,
                             const String& url_str,
                             AccessControlStatus) override;
  ScriptValue InstantiateModule(ScriptModule) override;
  ScriptValue GetInstantiationError(const ModuleScript*) override;
  Vector<String> ModuleRequestsFromScriptModule(ScriptModule) override;
  void ExecuteModule(const ModuleScript*) override;

  ModulatorImpl(RefPtr<ScriptState>, RefPtr<WebTaskRunner>, ResourceFetcher*);

  ExecutionContext* GetExecutionContext() const;

  RefPtr<ScriptState> script_state_;
  RefPtr<WebTaskRunner> task_runner_;
  Member<ResourceFetcher> fetcher_;
  TraceWrapperMember<ModuleMap> map_;
  Member<ModuleScriptLoaderRegistry> loader_registry_;
  Member<ModuleTreeLinkerRegistry> tree_linker_registry_;
  Member<ScriptModuleResolver> script_module_resolver_;
};

}  // namespace blink

#endif
