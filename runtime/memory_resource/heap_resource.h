#pragma once

#include "runtime/memory_resource/memory_resource.h"

namespace memory_resource {

class heap_resource {
public:
  void *allocate(size_type size) noexcept;
  void *allocate0(size_type size) noexcept;
  void *reallocate(void *mem, size_type new_size, size_type old_size) noexcept;
  void deallocate(void *mem, size_type size) noexcept;

  size_type memory_used() const noexcept { return memory_used_; }

private:
  size_type memory_used_{0};
};

} // namespace memory_resource
