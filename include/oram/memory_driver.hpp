#ifndef MEMORY_DRIVER_HPP
#define MEMORY_DRIVER_HPP

#include "driver.hpp"
#include <vector>
#include <stdexcept>

template <class T>
class MemoryDriver : public Driver<T> {
public:
  explicit MemoryDriver(size_t size, T default_value = T{})
    : storage_(size, default_value) {}

  T read_one(size_t pos) override {
    if (pos >= storage_.size()) {
      throw std::out_of_range("MemoryDriver: position out of range");
    }
    return storage_[pos];
  }

  T exchange_one(size_t pos, const T& value) override {
    if (pos >= storage_.size()) {
      throw std::out_of_range("MemoryDriver: position out of range");
    }
    T old = storage_[pos];
    storage_[pos] = value;
    return old;
  }

  // Direct access for testing/debugging
  const std::vector<T>& storage() const { return storage_; }
  std::vector<T>& storage() { return storage_; }

private:
  std::vector<T> storage_;
};

#endif // MEMORY_DRIVER_HPP
