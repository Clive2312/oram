#ifndef TOY_ORAM_HPP
#define TOY_ORAM_HPP

#include "oram.hpp"

template <class T>
class ToyORAM final : public ORAM<T> {
public:
  explicit ToyORAM(size_t size) : size_(size) {}

  size_t size() const override { return size_; }
  size_t physical_size() const override { return size_; }

protected:
  typename ORAM<T>::AccessResult access_impl(size_t pos, T val) override {
    // Read
    auto r1 = STORE_READ((std::vector<size_t>{pos, pos + 1}));
    T old = r1[0];
    T combined = r1[0] + r1[1] + val;

    // Write
    STORE_WRITE((std::vector<size_t>{pos}), (std::vector<T>{combined}));

    // Read again
    auto r2 = STORE_READ((std::vector<size_t>{pos + 2}));
    co_return old + r2[0];
  }

private:
  size_t size_;
};

#endif // TOY_ORAM_HPP
