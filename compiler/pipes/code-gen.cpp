// Compiler for PHP (aka KPHP)
// Copyright (c) 2020 LLC «V Kontakte»
// Distributed under the GPL v3 License, see LICENSE.notice.txt

#include "compiler/pipes/code-gen.h"

#include "compiler/cpp-dest-dir-initializer.h"
#include "compiler/code-gen/code-gen-task.h"
#include "compiler/code-gen/code-generator.h"
#include "compiler/code-gen/common.h"
#include "compiler/code-gen/declarations.h"
#include "compiler/code-gen/files/function-header.h"
#include "compiler/code-gen/files/function-source.h"
#include "compiler/code-gen/files/global_vars_memory_stats.h"
#include "compiler/code-gen/files/init-scripts.h"
#include "compiler/code-gen/files/lib-header.h"
#include "compiler/code-gen/files/tl2cpp/tl2cpp.h"
#include "compiler/code-gen/files/type-tagger.h"
#include "compiler/code-gen/files/vars-cpp.h"
#include "compiler/code-gen/files/vars-reset.h"
#include "compiler/code-gen/raw-data.h"
#include "compiler/compiler-core.h"
#include "compiler/data/class-data.h"
#include "compiler/data/function-data.h"
#include "compiler/data/lib-data.h"
#include "compiler/data/src-file.h"
#include "compiler/function-pass.h"
#include "compiler/inferring/public.h"
#include "compiler/pipes/collect-forkable-types.h"

size_t CodeGenF::calc_count_of_parts(size_t cnt_global_vars) {
  return 1u + cnt_global_vars / G->settings().globals_split_count.get();
}

void CodeGenF::on_finish(DataStream<std::unique_ptr<CodeGenRootCmd>> &os) {
  vk::singleton<CppDestDirInitializer>::get().wait();

  stage::set_name("GenerateCode");
  stage::set_file(SrcFilePtr());
  stage::die_if_global_errors();

  auto xall = tmp_stream.flush();
  xall.sort();
  const vector<ClassPtr> &all_classes = G->get_classes();

  FunctionPtr main_function = G->get_main_file()->main_function;

  auto should_gen_function = [&](FunctionPtr fun) {
    return fun->type != FunctionData::func_class_holder &&
           (fun->body_seq != FunctionData::body_value::empty || main_function == fun);
  };

  //TODO: parallelize;
  for (const auto &fun : xall) {
    if (should_gen_function(fun)) {
      G->stats.on_function_processed(fun);
      prepare_generate_function(fun);
    }
  }
  for (const auto &c : all_classes) {
    if (ClassData::does_need_codegen(c)) {
      prepare_generate_class(c);
    }
  }

  vector<FunctionPtr> all_functions;
  vector<FunctionPtr> exported_functions;

  for (const auto &function : xall) {
    if (!should_gen_function(function)) {
      continue;
    }
    if (function->is_extern()) {
      continue;
    }
    all_functions.push_back(function);
    code_gen_start_root_task(os, std::make_unique<FunctionH>(function));
    code_gen_start_root_task(os, std::make_unique<FunctionCpp>(function));

    if (function->kphp_lib_export && G->settings().is_static_lib_mode()) {
      exported_functions.emplace_back(function);
    }
  }

  for (const auto &c : all_classes) {
    if (!ClassData::does_need_codegen(c)) {
      continue;
    }

    switch (c->class_type) {
      case ClassType::klass:
        code_gen_start_root_task(os, std::make_unique<ClassDeclaration>(c));
        break;
      case ClassType::interface:
        code_gen_start_root_task(os, std::make_unique<InterfaceDeclaration>(c));
        break;

      case ClassType::trait:
        /* fallthrough */
      default:
        kphp_assert(false);
    }
  }

  code_gen_start_root_task(os, std::make_unique<GlobalVarsReset>(G->get_main_file()));
  if (G->settings().enable_global_vars_memory_stats.get()) {
    code_gen_start_root_task(os, std::make_unique<GlobalVarsMemoryStats>(G->get_main_file()));
  }
  code_gen_start_root_task(os, std::make_unique<InitScriptsCpp>(G->get_main_file(), std::move(all_functions)));

  std::vector<VarPtr> vars = G->get_global_vars();
  for (FunctionPtr fun: xall) {
    vars.insert(vars.end(), fun->static_var_ids.begin(), fun->static_var_ids.end());
  }
  size_t parts_cnt = calc_count_of_parts(vars.size());
  code_gen_start_root_task(os, std::make_unique<VarsCpp>(std::move(vars), parts_cnt));

  if (G->settings().is_static_lib_mode()) {
    for (FunctionPtr exported_function: exported_functions) {
      code_gen_start_root_task(os, std::make_unique<LibHeaderH>(exported_function));
    }
    code_gen_start_root_task(os, std::make_unique<LibHeaderTxt>(std::move(exported_functions)));
    code_gen_start_root_task(os, std::make_unique<StaticLibraryRunGlobalHeaderH>());
  } else {
    // TODO: should be done in lib mode, but by some other way
    code_gen_start_root_task(os, std::make_unique<TypeTagger>(vk::singleton<ForkableTypeStorage>::get().flush_forkable_types(), vk::singleton<ForkableTypeStorage>::get().flush_waitable_types()));
  }

  code_gen_start_root_task(os, std::make_unique<TlSchemaToCpp>());
  code_gen_start_root_task(os, std::make_unique<LibVersionHFile>());
  if (!G->settings().is_static_lib_mode()) {
    code_gen_start_root_task(os, std::make_unique<CppMainFile>());
  }
}

void CodeGenF::prepare_generate_function(FunctionPtr func) {
  string file_name = func->name;
  std::replace(file_name.begin(), file_name.end(), '$', '@');

  func->header_name = file_name + ".h";
  func->subdir = get_subdir(func->file_id->short_file_name);

  if (!func->is_inline) {
    func->src_name = file_name + ".cpp";
  }

  func->header_full_name =
    func->is_imported_from_static_lib()
    ? func->file_id->owner_lib->headers_dir() + func->header_name
    : func->subdir + "/" + func->header_name;

  my_unique(&func->static_var_ids);
  my_unique(&func->global_var_ids);
  my_unique(&func->local_var_ids);
}

string CodeGenF::get_subdir(const string &base) {
  int bucket = vk::std_hash(base) % 100;
  return "o_" + std::to_string(bucket);
}

void CodeGenF::prepare_generate_class(ClassPtr) {
}

void CodeGenForDiffF::execute(std::unique_ptr<CodeGenRootCmd> cmd, DataStream<WriterData *> &os) {
  stage::set_name("Code generation for diff");

  // re-launch cmd not with "calc hashes", but with "store cpp/h contents and php comments" mode
  // all generated files will be passed to os (to WriteFilesF)
  CodeGenerator W(false, os);
  cmd->compile(W);
  // now cmd is destroyed
}
