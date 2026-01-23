#ifndef TRACKER_ORAM_HPP
#define TRACKER_ORAM_HPP

#include "oram.hpp"
#include <concepts>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

template <class U>
concept TrackableBlock = requires(const U& value) {
  { value.id() } -> std::same_as<std::optional<uint32_t>>;
};

// TrackerORAM: Wraps an inner ORAM and tracks block IDs by observing writes/exchanges.
// The inner ORAM and driver see the same request/response types; tracking is side-band.
template <class T, class U>
requires TrackableBlock<U>
class TrackerORAM : public ORAM<T, U> {
public:
  using Base = ORAM<T, U>;
  using Inner = ORAM<T, U>;

  explicit TrackerORAM(Inner& inner,
                       std::vector<std::optional<uint32_t>> initial_ids = {})
      : inner_(inner) {
    const size_t physical = inner_.physical_size();
    if (initial_ids.empty()) {
      physical_ids_.assign(physical, std::nullopt);
    } else if (initial_ids.size() == physical) {
      physical_ids_ = std::move(initial_ids);
    } else {
      throw std::invalid_argument("TrackerORAM: initial_ids size mismatch");
    }
  }

  size_t size() const override { return inner_.size(); }
  size_t physical_size() const override { return inner_.physical_size(); }

  // Returns vector of IDs at each physical position; -1 for dummy/invalid.
  std::vector<int> extract_permutation() const {
    std::vector<int> out;
    out.reserve(physical_ids_.size());
    for (const auto& id : physical_ids_) {
      out.push_back(id ? static_cast<int>(*id) : -1);
    }
    return out;
  }

protected:
  typename Base::AccessResult access_impl(size_t pos, T val) override {
    auto inner_coro = inner_.access(pos, std::move(val));

    inner_coro.resume();
    while (!inner_coro.done()) {
      if (!inner_coro.has_yield()) {
        inner_coro.resume();
        continue;
      }

      auto yielded = inner_coro.take_yield();
      if (auto* inner_req = std::get_if<typename Base::AccessReq>(&yielded)) {
        track_request(*inner_req);

        auto results = co_yield std::move(*inner_req);
        inner_coro.provide_results(std::move(results));
      } else if (auto* lock_req = std::get_if<LockReq>(&yielded)) {
        co_yield *lock_req;
        inner_coro.resume();
      } else if (auto* unlock_req = std::get_if<UnlockReq>(&yielded)) {
        co_yield *unlock_req;
        inner_coro.resume();
      }
    }

    co_return inner_coro.result();
  }

private:
  static void expand_positions(const typename Base::Positions& positions,
                               std::vector<size_t>& out) {
    if (auto* vec = std::get_if<std::vector<size_t>>(&positions)) {
      out.insert(out.end(), vec->begin(), vec->end());
      return;
    }

    auto range = std::get<typename Base::Range>(positions);
    if (range.count == 0) return;
    if (range.stride == 0) throw std::invalid_argument("TrackerORAM: range stride cannot be zero");

    out.reserve(out.size() + range.count);
    size_t pos = range.start;
    for (size_t i = 0; i < range.count; ++i) {
      out.push_back(pos);
      pos += range.stride;
    }
  }

  static void validate_values(const std::vector<size_t>& positions,
                              const std::vector<U>& values) {
    if (positions.size() != values.size()) {
      throw std::invalid_argument("TrackerORAM: positions and values size mismatch");
    }
  }

  void track_request(const typename Base::AccessReq& req) {
    for (const auto& op : req.ops) {
      if (op.kind == Base::Op::Kind::Read) {
        continue;
      }

      std::vector<size_t> positions;
      expand_positions(op.positions, positions);
      validate_values(positions, op.values);

      for (size_t i = 0; i < positions.size(); ++i) {
        if (positions[i] >= physical_ids_.size()) {
          throw std::out_of_range("TrackerORAM: physical position out of range");
        }
        physical_ids_[positions[i]] = op.values[i].id();
      }
    }
  }

  Inner& inner_;
  std::vector<std::optional<uint32_t>> physical_ids_;
};

#endif // TRACKER_ORAM_HPP
