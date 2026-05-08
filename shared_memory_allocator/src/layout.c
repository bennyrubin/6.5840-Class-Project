#include "internal/layout.h"

size_t cm_align_up(size_t value, size_t alignment) {
  if (alignment == 0) {
    return value;
  }
  return (value + (alignment - 1u)) & ~(alignment - 1u);
}
