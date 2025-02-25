// Compiler for PHP (aka KPHP)
// Copyright (c) 2020 LLC «V Kontakte»
// Distributed under the GPL v3 License, see LICENSE.notice.txt

#pragma once

#include <map>
#include <string>
#include <vector>

#include "common/wrappers/optional.h"

#include "compiler/data/data_ptr.h"
#include "compiler/data/vertex-adaptor.h"
#include "compiler/inferring/primitive-type.h"

struct php_doc_tag {
  enum doc_type {
    unknown,
    kphp_inline,
    kphp_infer,
    kphp_disable_warnings,
    kphp_extern_func_info,
    kphp_pure_function,
    param,
    returns,
    var,
    kphp_required,
    kphp_lib_export,
    kphp_sync,
    kphp_should_not_throw,
    kphp_throws,
    kphp_template,
    kphp_return,
    kphp_memcache_class,
    kphp_immutable_class,
    kphp_tl_class,
    kphp_const,
    kphp_noreturn,
    kphp_warn_unused_result,
    kphp_warn_performance,
    kphp_analyze_performance,
    kphp_flatten,
    kphp_serializable,
    kphp_reserved_fields,
    kphp_serialized_field,
    kphp_serialized_float32,
    kphp_profile,
    kphp_profile_allow_inline,
    kphp_strict_types_enable, // TODO: remove when strict_types=1 are enabled by default
  };

public:
  static doc_type get_doc_type(const std::string &str) {
    auto it = str2doc_type.find(str);
    bool found = (it != str2doc_type.end());

    return found ? it->second : unknown;
  }

public:
  std::string name;
  std::string value;
  doc_type type = unknown;
  int line_num = -1;

private:
  static const std::map<std::string, doc_type> str2doc_type;
};

class TypeHint;

struct PhpDocTagParseResult {
  const TypeHint *type_hint;
  std::string var_name;     // stored without leading "$"; could be empty if omitted in phpdoc

  operator bool() const { return static_cast<bool>(type_hint); }
};

class PhpDocTypeRuleParser {
public:
  explicit PhpDocTypeRuleParser(FunctionPtr current_function) :
    current_function(current_function) {}

  const TypeHint *parse_from_tokens(std::vector<Token>::const_iterator &tok_iter);
  const TypeHint *parse_from_tokens_silent(std::vector<Token>::const_iterator &tok_iter) noexcept;

private:
  FunctionPtr current_function;
  std::vector<Token>::const_iterator cur_tok;

  const TypeHint *parse_classname(const std::string &phpdoc_class_name);
  const TypeHint *parse_simple_type();
  const TypeHint *parse_arg_ref();
  const TypeHint *parse_type_array();
  std::vector<const TypeHint *> parse_nested_type_hints();
  const TypeHint *parse_shape_type();
  const TypeHint *parse_nested_one_type_hint();
  const TypeHint *parse_typed_callable();
  const TypeHint *parse_type_expression();
};

std::vector<php_doc_tag> parse_php_doc(vk::string_view phpdoc);
PhpDocTagParseResult phpdoc_parse_type_and_var_name(vk::string_view phpdoc_tag_str, FunctionPtr current_function);

PhpDocTagParseResult phpdoc_find_tag(vk::string_view phpdoc, php_doc_tag::doc_type tag_type, FunctionPtr current_function);
vk::optional<std::string> phpdoc_find_tag_as_string(vk::string_view phpdoc, php_doc_tag::doc_type tag_type);

std::vector<PhpDocTagParseResult> phpdoc_find_tag_multi(vk::string_view phpdoc, php_doc_tag::doc_type tag_type, FunctionPtr current_function);
std::vector<std::string> phpdoc_find_tag_as_string_multi(vk::string_view phpdoc, php_doc_tag::doc_type tag_type);

bool phpdoc_tag_exists(vk::string_view phpdoc, php_doc_tag::doc_type tag_type);

const TypeHint *phpdoc_finalize_type_hint_and_resolve(const TypeHint *type_hint, FunctionPtr resolve_context);
const TypeHint *phpdoc_convert_default_value_to_type_hint(VertexPtr init_val);
