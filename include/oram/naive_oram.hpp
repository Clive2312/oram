#ifndef NAIVE_ORAM_HPP
#define NAIVE_ORAM_HPP

#include "oram.hpp"

// NaiveORAM: Scans all N positions on every access for obliviousness.
// Reads everything, updates the target position, writes everything back.
// O(N) per access - simple but inefficient.
template <class T>
class NaiveORAM final : public ORAM<T> {
public:
  explicit NaiveORAM(size_t size) : size_(size) {}

  size_t size() const override { return size_; }
  size_t physical_size() const override { return size_; }

protected:
  typename ORAM<T>::AccessResult access_impl(size_t pos, T val) override {
    // Read all positions
    typename ORAM<T>::Range range{0, size_, 1};
    auto all_values = STORE_READ(range);

    // Save the old value at the target position
    T old_value = all_values[pos];

    // Update the target position
    all_values[pos] = val;

    // Write all positions back
    STORE_WRITE(range, std::move(all_values));

    co_return old_value;
  }

private:
  size_t size_;
};

#endif // NAIVE_ORAM_HPP
