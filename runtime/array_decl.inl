// Compiler for PHP (aka KPHP)
// Copyright (c) 2020 LLC «V Kontakte»
// Distributed under the GPL v3 License, see LICENSE.notice.txt

#pragma once

#include "runtime/array_iterator.h"
#include "runtime/include.h"

#ifndef INCLUDED_FROM_KPHP_CORE
  #error "this file must be included only from kphp_core.h"
#endif

struct array_size {
  int64_t int_size = 0;
  int64_t string_size = 0;
  bool is_vector = false;

  array_size() = default;

  inline array_size(int64_t int_size, int64_t string_size, bool is_vector);

  inline array_size operator+(const array_size &other) const;

  inline array_size &cut(int64_t length);

  inline array_size &min(const array_size &other);
};

#ifdef __clang__
  // clang complains about 'flexible array member x of type T[] with non-trivial destruction'
  #define KPHP_ARRAY_TAIL_SIZE 0
#else
  // gcc10 complains about out-of-bounds access to an array of zero size
  #define KPHP_ARRAY_TAIL_SIZE
#endif

namespace dl {
template<class T, class TT, class T1>
void sort(TT *begin_init, TT *end_init, const T1 &compare);
}

enum class overwrite_element {
  YES,
  NO
};

template<class T>
class array {

public:
  using ValueType = T;

  typedef mixed key_type;
  typedef T value_type;

  inline static bool is_int_key(const key_type &key);

private:

  using entry_pointer_type = uint32_t;

  struct list_hash_entry {
    entry_pointer_type next;
    entry_pointer_type prev;
  };

  struct int_hash_entry : list_hash_entry {
    T value;

    int64_t int_key;

    inline key_type get_key() const;
  };

  struct string_hash_entry : list_hash_entry {
    T value;

    int64_t int_key;
    string string_key;

    inline key_type get_key() const;
  };

  // `max_key` and `string_size` could be also be there
  // but sometimes, for simplicity, we use them in vector too
  // to not add extra checks they are left in `array_inner`
  struct array_inner_fields_for_map {
    uint64_t modulo_helper_int_buf_size{0};
    uint64_t modulo_helper_string_buf_size{0};
  };

  struct array_inner {
    //if key is number, int_key contains this number, there is no string_key.
    //if key is string, int_key contains hash of this string, string_key contains this string.
    //empty hash_entry identified by (next == EMPTY_POINTER)
    //vector is_identified by string_buf_size == -1

    static constexpr uint32_t MAX_HASHTABLE_SIZE = (1 << 26);

    static constexpr entry_pointer_type EMPTY_POINTER = 0;

    // This stub field is needed to ensure the alignment during the const arrays generation
    // TODO: figure out something smarter
    int stub{0};
    int ref_cnt;
    int64_t max_key;
    list_hash_entry end_;
    uint32_t int_size;
    uint32_t int_buf_size;
    uint32_t string_size;
    uint32_t string_buf_size;
    int_hash_entry int_entries[KPHP_ARRAY_TAIL_SIZE];

    inline bool is_vector() const __attribute__ ((always_inline));

    inline list_hash_entry *get_entry(entry_pointer_type pointer) const __attribute__ ((always_inline));
    inline entry_pointer_type get_pointer(list_hash_entry *entry) const __attribute__ ((always_inline));

    inline const string_hash_entry *begin() const __attribute__ ((always_inline)) ubsan_supp("alignment");
    inline const string_hash_entry *next(const string_hash_entry *p) const __attribute__ ((always_inline)) ubsan_supp("alignment");
    inline const string_hash_entry *prev(const string_hash_entry *p) const __attribute__ ((always_inline)) ubsan_supp("alignment");
    inline const string_hash_entry *end() const __attribute__ ((always_inline)) ubsan_supp("alignment");

    inline string_hash_entry *begin() __attribute__ ((always_inline)) ubsan_supp("alignment");
    inline string_hash_entry *next(string_hash_entry *p) __attribute__ ((always_inline)) ubsan_supp("alignment");
    inline string_hash_entry *prev(string_hash_entry *p) __attribute__ ((always_inline)) ubsan_supp("alignment");
    inline string_hash_entry *end() __attribute__ ((always_inline)) ubsan_supp("alignment");

    inline bool is_string_hash_entry(const string_hash_entry *p) const __attribute__ ((always_inline));
    inline const string_hash_entry *get_string_entries() const __attribute__ ((always_inline));
    inline string_hash_entry *get_string_entries() __attribute__ ((always_inline));

    inline array_inner_fields_for_map &fields_for_map() __attribute__((always_inline));
    inline const array_inner_fields_for_map &fields_for_map() const __attribute__((always_inline));

    inline uint32_t choose_bucket_int(int64_t key) const __attribute__ ((always_inline));
    inline uint32_t choose_bucket_string(int64_t key) const __attribute__ ((always_inline));
    inline static uint32_t choose_bucket(int64_t key, uint32_t buf_size, uint64_t modulo_helper) __attribute__ ((always_inline));

    inline static size_t sizeof_vector(uint32_t int_size) __attribute__((always_inline));
    inline static size_t sizeof_map(uint32_t int_size, uint32_t string_size) __attribute__((always_inline));
    inline static size_t estimate_size(int64_t &new_int_size, int64_t &new_string_size, bool is_vector);
    inline static array_inner *create(int64_t new_int_size, int64_t new_string_size, bool is_vector);

    inline static array_inner *empty_array() __attribute__ ((always_inline));

    inline void dispose();

    inline array_inner *ref_copy() __attribute__ ((always_inline));


    template<class ...Args>
    inline T &emplace_back_vector_value(Args &&... args) noexcept;
    inline T &push_back_vector_value(const T &v); //unsafe

    template<class ...Args>
    inline T &emplace_vector_value(int64_t int_key, Args &&... args) noexcept;
    inline T &set_vector_value(int64_t int_key, const T &v); //unsafe

    template<class ...Args>
    inline T &emplace_int_key_map_value(overwrite_element policy, int64_t int_key, Args &&... args) noexcept;
    inline T &set_map_value(overwrite_element policy, int64_t int_key, const T &v);

    template<class STRING, class ...Args>
    inline T &emplace_string_key_map_value(overwrite_element policy, int64_t int_key, STRING &&string_key, Args &&... args) noexcept;
    inline T &set_map_value(overwrite_element policy, int64_t int_key, const string &string_key, const T &v);

    inline void unset_vector_value();
    inline void unset_map_value(int64_t int_key);

    // to avoid the const_cast, declare these functions as static with a template self parameter (this)
    template<class S>
    static inline auto &find_map_entry(S &self, int64_t int_key) noexcept;
    template<class S>
    static inline auto &find_map_entry(S &self, const string &string_key, int64_t precomuted_hash) noexcept;

    template<class ...Key>
    inline const T *find_map_value(Key &&... key) const noexcept;
    inline const T *find_vector_value(int64_t int_key) const noexcept;
    inline T *find_vector_value(int64_t int_key) noexcept;

    inline const T &get_vector_value(int64_t int_key) const;//unsafe
    inline T &get_vector_value(int64_t int_key);//unsafe
    inline void unset_map_value(const string &string_key, int64_t precomuted_hash);

    size_t estimate_memory_usage() const;

    inline array_inner(int ref_cnt, int64_t max_key, list_hash_entry end_, uint32_t int_size, uint32_t int_buf_size, uint32_t string_size, uint32_t string_buf_size) :
      ref_cnt(ref_cnt),
      max_key(max_key),
      end_(end_),
      int_size(int_size),
      int_buf_size(int_buf_size),
      string_size(string_size),
      string_buf_size(string_buf_size) {
    }

    inline array_inner(const array_inner &other) = delete;
    inline array_inner &operator=(const array_inner &other) = delete;
  };

  inline bool mutate_if_vector_shared(uint32_t mul = 1);
  inline bool mutate_to_size_if_vector_shared(int64_t int_size);
  inline void mutate_to_size(int64_t int_size);
  inline bool mutate_if_map_shared(uint32_t mul = 1);
  inline void mutate_if_vector_needed_int();
  inline void mutate_if_map_needed_int();
  inline void mutate_if_map_needed_string();
  inline void mutate_to_map_if_vector_or_map_need_string();

  inline void convert_to_map();

  template<class T1>
  inline void copy_from(const array<T1> &other);

  template<class T1>
  inline void move_from(array<T1> &&other) noexcept;

  inline void destroy() __attribute__ ((always_inline));

public:
  friend class array_iterator<T>;
  friend class array_iterator<const T>;

  using iterator = array_iterator<T>;
  using const_iterator = array_iterator<const T>;

  inline array() __attribute__ ((always_inline));

  inline explicit array(const array_size &s) __attribute__ ((always_inline));

  template<class KeyT>
  inline array(const std::initializer_list<std::pair<KeyT, T>> &list) __attribute__ ((always_inline));

  inline array(const array &other) noexcept __attribute__ ((always_inline));

  inline array(array &&other) noexcept __attribute__ ((always_inline));

  template<class T1, class = enable_if_constructible_or_unknown<T, T1>>
  inline array(const array<T1> &other) noexcept __attribute__ ((always_inline));

  template<class T1, class = enable_if_constructible_or_unknown<T, T1>>
  inline array(array<T1> &&other) noexcept __attribute__ ((always_inline));

  template<class... Args>
  inline static array create(Args &&... args) __attribute__ ((always_inline));

  inline array &operator=(const array &other) noexcept __attribute__ ((always_inline));

  inline array &operator=(array &&other) noexcept __attribute__ ((always_inline));

  template<class T1, class = enable_if_constructible_or_unknown<T, T1>>
  inline array &operator=(const array<T1> &other) noexcept;

  template<class T1, class = enable_if_constructible_or_unknown<T, T1>>
  inline array &operator=(array<T1> &&other) noexcept;

  inline ~array();

  inline void clear() __attribute__ ((always_inline));

  inline bool is_vector() const __attribute__ ((always_inline));
  inline bool is_pseudo_vector() const __attribute__ ((always_inline));

  T &operator[](int64_t int_key);
  T &operator[](int32_t key) { return (*this)[int64_t{key}]; }
  T &operator[](const string &s);
  T &operator[](const mixed &v);
  T &operator[](double double_key);
  T &operator[](const const_iterator &it);
  T &operator[](const iterator &it);

  template<class ...Args>
  void emplace_value(int64_t int_key, Args &&... args) noexcept;
  void set_value(int64_t int_key, T &&v) noexcept;
  void set_value(int64_t int_key, const T &v) noexcept;
  void set_value(int32_t key, T &&v) noexcept { set_value(int64_t{key}, std::move(v)); }
  void set_value(int32_t key, const T &v) noexcept { set_value(int64_t{key}, v); }
  void set_value(double double_key, T &&v) noexcept;
  void set_value(double double_key, const T &v) noexcept;

  template<class ...Args>
  void emplace_value(const string &string_key, Args &&... args) noexcept;
  void set_value(const string &string_key, T &&v) noexcept;
  void set_value(const string &string_key, const T &v) noexcept;

  void set_value(const string &string_key, T &&v, int64_t precomuted_hash) noexcept;
  void set_value(const string &string_key, const T &v, int64_t precomuted_hash) noexcept;

  template<class ...Args>
  void emplace_value(const mixed &var_key, Args &&... args) noexcept;
  void set_value(const mixed &v, T &&value) noexcept;
  void set_value(const mixed &v, const T &value) noexcept;


  template<class OptionalT, class ...Args>
  void emplace_value(const Optional<OptionalT> &key, Args &&... args) noexcept;
  template<class OptionalT>
  void set_value(const Optional<OptionalT> &key, T &&value) noexcept;
  template<class OptionalT>
  void set_value(const Optional<OptionalT> &key, const T &value) noexcept;

  void set_value(const const_iterator &it);
  void set_value(const iterator &it);

  // assign binary array_inner representation
  // can be used only on empty arrays to receive logically const array
  void assign_raw(const char *s);

  const T *find_value(int64_t int_key) const;
  const T *find_value(int32_t key) const { return find_value(int64_t{key}); }
  const T *find_value(const string &s) const;
  const T *find_value(const string &s, int64_t precomuted_hash) const;
  const T *find_value(const mixed &v) const;
  const T *find_value(double double_key) const;
  const T *find_value(const const_iterator &it) const;
  const T *find_value(const iterator &it) const;

  // All non-const methods find_no_mutate() do not lead to a copy
  iterator find_no_mutate(int64_t int_key) noexcept;
  iterator find_no_mutate(const string &string_key) noexcept;
  iterator find_no_mutate(const mixed &v) noexcept;

  template<class K>
  const mixed get_var(const K &key) const;

  template<class K>
  const T get_value(const K &key) const;
  const T get_value(const string &string_key, int64_t precomuted_hash) const;

  template<class ...Args>
  T &emplace_back(Args &&... args) noexcept;
  void push_back(T &&v) noexcept;
  void push_back(const T &v) noexcept;

  void push_back(const const_iterator &it);
  void push_back(const iterator &it);

  const T push_back_return(const T &v);
  const T push_back_return(T &&v);

  void swap_int_keys(int64_t idx1, int64_t idx2) noexcept;

  inline void fill_vector(int64_t num, const T &value);
  inline void memcpy_vector(int64_t num, const void *src_buf);

  inline int64_t get_next_key() const __attribute__ ((always_inline));

  template<class K>
  bool has_key(const K &key) const;

  template<class K>
  bool isset(const K &key) const;

  void unset(int64_t int_key);
  void unset(int32_t key) { unset(int64_t{key}); }
  void unset(const string &string_key);
  void unset(const mixed &var_key);
  void unset(double double_key);

  inline bool empty() const __attribute__ ((always_inline));
  inline int64_t count() const __attribute__ ((always_inline));

  inline array_size size() const __attribute__ ((always_inline));

  template<class T1, class = enable_if_constructible_or_unknown<T, T1>>
  void merge_with(const array<T1> &other);

  const array operator+(const array &other) const;
  array &operator+=(const array &other);

  inline const_iterator begin() const __attribute__ ((always_inline));
  inline const_iterator cbegin() const __attribute__ ((always_inline));
  inline const_iterator middle(int64_t n) const __attribute__ ((always_inline));
  inline const_iterator end() const __attribute__ ((always_inline));
  inline const_iterator cend() const __attribute__ ((always_inline));

  inline iterator begin() __attribute__ ((always_inline));
  inline iterator middle(int64_t n) __attribute__ ((always_inline));
  inline iterator end() __attribute__ ((always_inline));

  template<class T1>
  void sort(const T1 &compare, bool renumber);

  template<class T1>
  void ksort(const T1 &compare);

  inline void swap(array &other) __attribute__ ((always_inline));


  T pop();

  T shift();

  int64_t unshift(const T &val);


  inline bool to_bool() const __attribute__ ((always_inline));
  inline int64_t to_int() const __attribute__ ((always_inline));
  inline double to_float() const __attribute__ ((always_inline));


  int64_t get_reference_counter() const;
  bool is_reference_counter(ExtraRefCnt ref_cnt_value) const noexcept;
  void set_reference_counter_to(ExtraRefCnt ref_cnt_value) noexcept;
  void force_destroy(ExtraRefCnt expected_ref_cnt) noexcept;

  iterator begin_no_mutate();
  iterator end_no_mutate();

  void mutate_if_shared() noexcept;

  const T *get_const_vector_pointer() const; // unsafe

  bool is_equal_inner_pointer(const array &other) const noexcept;

  void reserve(int64_t int_size, int64_t string_size, bool make_vector_if_possible);

  size_t estimate_memory_usage() const;

  template<typename U>
  static array<T> convert_from(const array<U> &);

private:
  template<class ...Key>
  iterator find_iterator_in_map_no_mutate(const Key &... key) noexcept;

  void push_back_values() {}

  template<class Arg, class...Args>
  void push_back_values(Arg &&arg, Args &&... args);

private:
  array_inner *p;

  template<class T1>
  friend class array;
};

template<class T>
inline void swap(array<T> &lhs, array<T> &rhs);

template<class T>
inline const array<T> array_add(array<T> a1, const array<T> &a2);

