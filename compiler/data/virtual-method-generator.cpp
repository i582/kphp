// Compiler for PHP (aka KPHP)
// Copyright (c) 2020 LLC «V Kontakte»
// Distributed under the GPL v3 License, see LICENSE.notice.txt

#include "compiler/data/virtual-method-generator.h"

#include <queue>

#include "common/termformat/termformat.h"

#include "compiler/const-manipulations.h"
#include "compiler/data/class-data.h"
#include "compiler/data/function-data.h"
#include "compiler/data/src-file.h"
#include "compiler/type-hint.h"
#include "compiler/gentree.h"
#include "compiler/stage.h"

namespace {

VertexAdaptor<op_func_call> create_call_with_var_and_class_name_params(VertexAdaptor<op_var> instance_var, ClassPtr klass) {
  DefinePtr d = G->get_define("c#" + replace_backslashes(klass->name) + "$$class");
  auto class_name = d->val.clone().set_location_recursively(instance_var);

  return VertexAdaptor<op_func_call>::create(instance_var, class_name);
}

VertexAdaptor<op_func_call> create_instance_cast_to(VertexAdaptor<op_var> instance_var, ClassPtr derived) {
  auto cast_to_derived = create_call_with_var_and_class_name_params(instance_var, derived);
  cast_to_derived->set_string("instance_cast");
  return cast_to_derived;
}

// derived type can be more specific than the base type (covariance)
// see https://www.php.net/manual/en/language.oop5.variance.php
// note: the PHP docs lie about int<->float, int is not considered to be a more specific type
bool return_type_hints_compatible(const TypeHint* base, const TypeHint* derived) {
  // no base type hint -> anything is allowed
  // identical type hint -> OK
  if (!base || base == derived) {
    return true;
  }
  if (!derived) {
    return false;
  }

  // temporary kludge
  // REMOVE ME, DO NOT SUBMIT
  if (const auto *base_primitive = base->try_as<TypeHintPrimitive>()) {
    if (base_primitive->ptype == tp_void) {
      return true;
    }
  }

  // handle variadic arguments `T ...$param`
  if (const auto *base_array = base->try_as<TypeHintArray>()) {
    if (const auto *derived_array = derived->try_as<TypeHintArray>()) {
      return return_type_hints_compatible(base_array->inner, derived_array->inner);
    }
  }

  if (const auto *base_optional = base->try_as<TypeHintOptional>()) {
    if (const auto *derived_optional = derived->try_as<TypeHintOptional>()) {
      return base_optional->or_null && !base_optional->or_false &&
             derived_optional->or_null && !derived_optional->or_false &&
             return_type_hints_compatible(base_optional->inner, derived_optional->inner);
    }
    if (base_optional->or_null && !base_optional->or_false) {
      // covariance allows base `?T` type to be compared as `T`, but not the other way around
      return base_optional->or_null && !base_optional->or_false &&
             return_type_hints_compatible(base_optional->inner, derived);
    }
    // or-false types are handled below
  }

  // can't use Assumption::extract_instance_from_type_hint as it will
  // unwrap the TypeHintOptional here
  if (const auto *base_instance = base->try_as<TypeHintInstance>()) {
    if (const auto *derived_instance = derived->try_as<TypeHintInstance>()) {
      return base_instance->resolve()->is_parent_of(derived_instance->resolve());
    }
  }

  // this is not compliant to the PHP behavior as we would allow
  // a mixture of incompatible unions here, but this can be improved in future;
  //
  // we'll probably want to sort (by hash?) all TypeHintPipe items to make
  // their full comparison faster (without a map or N^2 complexity)
  if (base->is_typedata_constexpr() && derived->is_typedata_constexpr()) {
    const auto *base_td = base->to_type_data();
    const auto *derived_td = derived->to_type_data();
    if (base_td->ptype() == tp_float && derived_td->ptype() == tp_int) {
      return false;
    }
    return is_less_or_equal_type(derived_td, base_td);
  }

  return false;
}

// derived type can be less specific than the base type (contravariance)
// see https://www.php.net/manual/en/language.oop5.variance.php
bool param_type_hints_compatible(const TypeHint* base, const TypeHint* derived) {
  // doing a swap + same argument order to avoid "suspicious arguments order" inspection;
  // we need to call a return_type_hints_compatible with reversed arguments order
  std::swap(base, derived);
  return return_type_hints_compatible(base, derived);
}

bool check_signatures_variance(FunctionPtr interface_function, FunctionPtr derived_method) {
  auto derived_params = derived_method->get_params();
  auto interface_params = interface_function->get_params();

  bool type_hints_are_compatible = return_type_hints_compatible(interface_function->return_typehint, derived_method->return_typehint);
  bool error_in_return = !type_hints_are_compatible;
  for (int i = 1; i < interface_params.size(); i++) {
    if (!type_hints_are_compatible) {
      break;
    }
    auto interface_param = interface_params[i].as<op_func_param>();
    auto derived_param = derived_params[i].as<op_func_param>();
    type_hints_are_compatible = param_type_hints_compatible(interface_param->type_hint, derived_param->type_hint);
  }
  if (!type_hints_are_compatible) {
    if (error_in_return) {
      printf("%s\n", fmt_format("base return type ({}): {}",
                                interface_function->class_id->name,
                                interface_function->return_typehint->as_human_readable()).c_str());
      printf("%s\n", fmt_format("derived return type ({}): {}",
                                derived_method->class_id->name,
                                derived_method->return_typehint ? derived_method->return_typehint->as_human_readable() : "NONE").c_str());
    } else {
      for (int i = 1; i < interface_params.size(); i++) {
        auto interface_param = interface_params[i].as<op_func_param>();
        auto derived_param = derived_params[i].as<op_func_param>();
        printf("%s\n", fmt_format("base ({}) param[{}]: {}",
                                  interface_function->class_id->name,
                                  interface_param->var()->str_val,
                                  interface_param->type_hint ? interface_param->type_hint->as_human_readable() : "NONE").c_str());
        printf("%s\n", fmt_format("derived ({}) param[{}]: {}",
                                  derived_method->class_id->name,
                                  derived_param->var()->str_val,
                                  derived_param->type_hint ? derived_param->type_hint->as_human_readable() : "NONE").c_str());
      }
    }
  }
  kphp_error(type_hints_are_compatible, fmt_format("Declaration of {}() must be compatible with {}()",
                                                   derived_method->get_human_readable_name(),
                                                   interface_function->get_human_readable_name()));
  return type_hints_are_compatible;
}

template<class ClassMemberMethod>
bool check_that_signatures_are_same(FunctionPtr interface_function, ClassPtr context_class, ClassMemberMethod *interface_method_in_derived) {
  stage::set_line(interface_function->root->get_location().line);
  if (!interface_method_in_derived || (interface_method_in_derived->function->is_constructor() && !context_class->has_custom_constructor)) {
    kphp_error_act(context_class->modifiers.is_abstract(),
                   fmt_format("class: {} must be abstract, method: {} is not overridden",
                              TermStringFormat::paint_green(context_class->name),
                              TermStringFormat::paint_green(interface_function->get_human_readable_name())),
                   return true);
    for (auto derived : context_class->derived_classes) {
      const auto *method_in_derived = derived->members.find_by_local_name<ClassMemberMethod>(interface_function->local_name());
      if (!check_that_signatures_are_same(interface_function, derived, method_in_derived)) {
        return false;
      }
    }
    return true;
  }

  FunctionPtr derived_method = interface_method_in_derived->function;
  kphp_assert(derived_method);

  /**
   * Check that overridden method has count of parameters at least as in interface
   * and has appropriate count of default parameters e.g.:
   *
   * interface I { public function foo($x, $y); }
   * class A implements I { public function foo($x, $y = 10, $z = 20); }
   *
   * Interface I: i_argn = 2
   * Class A    : min_argn = 1, max_argn = 3, default_argn = 2
   * check that : i_argn <= max_argn && (default_argn >= max_argn - i_argn)
   */
  auto min_argn = derived_method->get_min_argn();
  auto derived_params = derived_method->get_params();
  auto max_argn = derived_params.size();
  auto default_argn = max_argn - min_argn;

  auto interface_params = interface_function->get_params();
  auto i_argn = interface_params.size();

  if (!(i_argn <= max_argn && (default_argn >= max_argn - i_argn))) {
    kphp_error(false, fmt_format("Count of arguments are different in interface method: `{}` and in class: `{}`",
                                 interface_function->get_human_readable_name(),
                                 context_class->name));

    return false;
  }

  // non-required methods may not have enough types information to be checked here;
  // we skip them to avoid false positives
  if (derived_method->is_required) {
    check_signatures_variance(interface_function, derived_method);
  }

  // do we want to return `typehints_are_compatible` here instead of `true`?
  return true;
}

bool check_that_signatures_are_same(ClassPtr context_class, FunctionPtr interface_function) {
  auto interface_method_in_derived = context_class->members.get_instance_method(interface_function->local_name());
  return check_that_signatures_are_same(interface_function, context_class, interface_method_in_derived);
}

void check_static_function(FunctionPtr interface_function, std::vector<ClassPtr> &derived_classes) {
  for (auto derived : derived_classes) {
    auto static_in_derived = derived->members.get_static_method(interface_function->local_name());
    check_that_signatures_are_same(interface_function, derived, static_in_derived);
  }
}

void check_constructor(FunctionPtr interface_constructor, std::vector<ClassPtr> &derived_classes) {
  for (auto derived : derived_classes) {
    auto derived_constructor = derived->members.get_instance_method(ClassData::NAME_OF_CONSTRUCT);
    check_that_signatures_are_same(interface_constructor, derived, derived_constructor);
  }
}

VertexAdaptor<op_case> gen_case_on_hash(ClassPtr derived, VertexAdaptor<op_seq> cmd) {
  auto hash_of_derived = GenTree::create_int_const(derived->get_hash());
  return VertexAdaptor<op_case>::create(hash_of_derived, cmd);
}

VertexAdaptor<op_case> gen_case_calling_methods_on_derived_class(ClassPtr derived, FunctionPtr virtual_function) {
  FunctionPtr concrete_method_of_derived;
  if (auto method_of_derived = derived->members.get_instance_method(virtual_function->local_name())) {
    concrete_method_of_derived = method_of_derived->function;
  } else {
    if (auto method_from_ancestor = derived->get_instance_method(virtual_function->local_name())) {
      concrete_method_of_derived = method_from_ancestor->function;
    }

    bool is_overridden_by_ancestors = concrete_method_of_derived && !concrete_method_of_derived->modifiers.is_abstract();
    if (virtual_function->modifiers.is_abstract() && !derived->modifiers.is_abstract() && !is_overridden_by_ancestors) {
      kphp_error(false, fmt_format("You should override abstract method: `{}` in class: `{}`",
                                   virtual_function->get_human_readable_name(),
                                   derived->name));
      return {};
    }
  }

  if (!concrete_method_of_derived || concrete_method_of_derived->modifiers.is_abstract()) {
    return {};
  }

  if (!concrete_method_of_derived->file_id->is_builtin() && concrete_method_of_derived->is_virtual_method) {
    auto &members_of_derived_class = concrete_method_of_derived->class_id->members;
    concrete_method_of_derived = members_of_derived_class.get_instance_method(virtual_function->get_name_of_self_method())->function;
  }

  if (!check_that_signatures_are_same(concrete_method_of_derived->class_id, virtual_function)) {
    return {};
  }

  VertexPtr this_var = create_instance_cast_to(ClassData::gen_vertex_this({}), derived);
  // generate concrete_method call, with arguments from virtual_functions, because of Derived can have extra default params:
  auto call_self_method_of_derived = GenTree::generate_call_on_instance_var(this_var, virtual_function, concrete_method_of_derived->local_name());
  auto call_derived_method = VertexAdaptor<op_seq>::create(VertexAdaptor<op_return>::create(call_self_method_of_derived));

  return gen_case_on_hash(derived, call_derived_method);
}

} // namespace


/**
 * generated AST for virtual methods will be like this:
 *
 * function virtual_function($param1, ...) {
 *   switch ($this->get_virt_hash()) {
 *   case 0xDEADBEAF: { // hash of Derived1
 *     $tmp = instance_cast<Derived1>($this);
 *     $tmp->virtual_function($param1, ...);
 *     break;
 *   }
 *   case 0x02280228: { // hash of Derived 2
 *     $tmp = instance_cast<Derived2>($this);
 *     $tmp->virtual_function($param1, ...);
 *     break;
 *   }
 *   default: {
 *     php_warning("call method(Interface::virtual_function) on empty class
 *     exit(0);
 *   }
 */
void generate_body_of_virtual_method(FunctionPtr virtual_function) {
  auto klass = virtual_function->class_id;
  kphp_assert(klass);

  auto &derived_classes = klass->derived_classes;

  if (derived_classes.empty()) {
    return;
  }

  if (virtual_function->modifiers.is_abstract()) {
    auto check_parents_dont_have_same_non_abstract_method = [&](auto member) {
      for (auto parent = klass->parent_class; parent; parent = parent->parent_class) {
        const auto *method = parent->members.find_by_local_name<decltype(member)>(virtual_function->local_name());
        kphp_error(!method || method->function->modifiers.is_abstract(),
                   fmt_format("Cannot make non abstract method {} abstract in class {}", virtual_function->get_human_readable_name(), parent->name));
      }
    };
    if (virtual_function->modifiers.is_static()) {
      check_parents_dont_have_same_non_abstract_method(ClassMemberStaticMethod{virtual_function});
    } else {
      check_parents_dont_have_same_non_abstract_method(ClassMemberInstanceMethod{virtual_function});
    }
  }

  if (virtual_function->modifiers.is_static()) {
    kphp_assert(klass->modifiers.is_abstract());
    return check_static_function(virtual_function, derived_classes);
  }

  if (virtual_function == klass->construct_function) {
    kphp_assert(klass->modifiers.is_abstract());
    return check_constructor(virtual_function, derived_classes);
  }

  if (!virtual_function->modifiers.is_abstract()) {
    kphp_assert(klass->members.has_instance_method(virtual_function->get_name_of_self_method()));
    kphp_assert(virtual_function->root->cmd()->empty());
  }

  std::vector<VertexPtr> cases;
  std::unordered_set<ClassPtr> unique_inheritors;
  for (auto inheritor : klass->get_all_inheritors()) {
    if (auto case_for_cur_class = gen_case_calling_methods_on_derived_class(inheritor, virtual_function)) {
      cases.emplace_back(case_for_cur_class);

      if (!unique_inheritors.insert(inheritor).second && !stage::has_global_error()) {
        kphp_error(false, fmt_format("duplicated class: {} in hierarchy from class: {}", klass->name, inheritor->name));
      }
    }
  }

  if (!cases.empty()) {
    auto warn_on_default = GenTree::generate_critical_error(fmt_format("call method({}) on null object", virtual_function->get_human_readable_name()));

    auto call_of_exit = VertexAdaptor<op_seq>::create(warn_on_default);
    cases.emplace_back(VertexAdaptor<op_default>::create(call_of_exit));
  } else {
    // just keep empty body, when there is no inheritors for interface method
    return;
  }

  auto get_hash_of_this = VertexAdaptor<op_func_call>::create(ClassData::gen_vertex_this({}));
  get_hash_of_this->set_string("get_hash_of_class");

  auto body_of_virtual_method = VertexAdaptor<op_seq>::create(GenTree::create_switch_vertex(virtual_function, get_hash_of_this, std::move(cases)));

  auto &root = virtual_function->root;
  auto declaration_location = root->get_location();
  root = VertexAdaptor<op_function>::create(root->param_list(), body_of_virtual_method);
  root->func_id = virtual_function;
  virtual_function->type = FunctionData::func_local;

  std::function<void(VertexPtr)> update_location = [&](VertexPtr v) {
    v.set_location(declaration_location);
    std::for_each(v->begin(), v->end(), update_location);
  };
  update_location(root);
}
