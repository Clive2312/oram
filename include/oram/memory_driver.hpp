#ifndef MEMORY_DRIVER_HPP
#define MEMORY_DRIVER_HPP

#include "driver.hpp"
#include <vector>
#include <stdexcept>

template <class T, class U = T>
class MemoryDriver : public Driver<T, U> {
public:
  explicit MemoryDriver(size_t size, U default_value = U{})
    : storage_(size, default_value) {}

  U read_one(size_t pos) override {
    if (pos >= storage_.size()) {
      throw std::out_of_range("MemoryDriver: position out of range");
    }
    return storage_[pos];
  }

  U exchange_one(size_t pos, const U& value) override {
    if (pos >= storage_.size()) {
      throw std::out_of_range("MemoryDriver: position out of range");
    }
    U old = storage_[pos];
    storage_[pos] = value;
    return old;
  }

  // Direct access for testing/debugging
  const std::vector<U>& storage() const { return storage_; }
  std::vector<U>& storage() { return storage_; }

private:
  std::vector<U> storage_;
};

#endif // MEMORY_DRIVER_HPP
