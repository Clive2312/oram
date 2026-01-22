#ifndef PLAINTEXT_ORAM_HPP
#define PLAINTEXT_ORAM_HPP

#include "oram.hpp"

// PlaintextORAM: No obfuscation - directly reads/writes the requested position.
// This is the baseline non-oblivious implementation.
template <class T>
class PlaintextORAM final : public ORAM<T> {
public:
  explicit PlaintextORAM(size_t size) : size_(size) {}

  size_t size() const override { return size_; }
  size_t physical_size() const override { return size_; }

protected:
  typename ORAM<T>::AccessResult access_impl(size_t pos, T val) override {
    // Read the value at position
    auto read_res = STORE_READ((std::vector<size_t>{pos}));
    T old_value = read_res[0];

    // Write the new value
    STORE_WRITE((std::vector<size_t>{pos}), (std::vector<T>{val}));

    co_return old_value;
  }

private:
  size_t size_;
};

#endif // PLAINTEXT_ORAM_HPP
