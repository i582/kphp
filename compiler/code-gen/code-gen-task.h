// Compiler for PHP (aka KPHP)
// Copyright (c) 2020 LLC «V Kontakte»
// Distributed under the GPL v3 License, see LICENSE.notice.txt

#pragma once

#include "compiler/code-gen/code-generator.h"
#include "compiler/scheduler/task.h"
#include "compiler/stage.h"
#include "compiler/threading/profiler.h"

ProfilerRaw &get_code_gen_profiler();

template<class T>
class CodeGenSchedulerTask : public Task {
private:
  CodeGenerator W;
  T cmd;
public:
  CodeGenSchedulerTask(DataStream<WriterData *> &os, const T &cmd) :
    W(os),
    cmd(cmd) {
  }

  void execute() {
    AutoProfiler profler{get_code_gen_profiler()};
    stage::set_name("Code generation");
    // uncomment this to launch codegen twice and ensure there is no diff (codegeneration is idempotent)
    // cmd.compile(W);
    cmd.compile(W);
  }
};

template<class T>
void code_gen_start_root_task(DataStream<WriterData *> &os, const T &cmd) {
  register_async_task(new CodeGenSchedulerTask<T>(os, cmd));
}
