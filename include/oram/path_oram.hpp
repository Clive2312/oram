#ifndef PATH_ORAM_HPP
#define PATH_ORAM_HPP

#include "oram.hpp"
#include <cstdint>
#include <cstring>
#include <cmath>
#include <optional>
#include <random>
#include <vector>

// PathORAM: An Extremely Simple Oblivious RAM Protocol by Stefanov et al.
// O(log(N)) per access
template <class T>
struct PathORAMBlock {
  size_t block_id;
  T data;
  bool valid;

  std::optional<uint32_t> id() const {
    if (!valid) return std::nullopt;
    return static_cast<uint32_t>(block_id);
  }

  // Serialization overhead: 8-byte block_id (LE) + 1-byte valid flag.
  static constexpr size_t kSerializedOverhead = sizeof(uint64_t) + 1;

  // Serialize to bytes: [block_id (8 LE bytes)][valid (1 byte)][data]
  // Only meaningful for T with .size() and iterators (e.g. std::vector<uint8_t>).
  std::vector<uint8_t> serialize() const
      requires requires(const T& d) { d.size(); d.begin(); d.end(); } {
    std::vector<uint8_t> out;
    out.reserve(kSerializedOverhead + data.size());
    uint64_t id_le = static_cast<uint64_t>(block_id);
    for (size_t i = 0; i < sizeof(uint64_t); ++i) {
      out.push_back(static_cast<uint8_t>(id_le & 0xFF));
      id_le >>= 8;
    }
    out.push_back(valid ? uint8_t{1} : uint8_t{0});
    out.insert(out.end(), data.begin(), data.end());
    return out;
  }

  // Deserialize from bytes: [block_id (8 LE bytes)][valid (1 byte)][data]
  static PathORAMBlock deserialize(const std::vector<uint8_t>& bytes)
      requires requires { T().assign(bytes.begin(), bytes.end()); } {
    if (bytes.size() < kSerializedOverhead) {
      throw std::invalid_argument("PathORAMBlock::deserialize: too small");
    }
    PathORAMBlock block;
    uint64_t id_le = 0;
    for (size_t i = 0; i < sizeof(uint64_t); ++i) {
      id_le |= static_cast<uint64_t>(bytes[i]) << (i * 8);
    }
    block.block_id = static_cast<size_t>(id_le);
    block.valid = (bytes[sizeof(uint64_t)] != 0);
    block.data.assign(bytes.begin() + kSerializedOverhead, bytes.end());
    return block;
  }
};

template <class T>
class PathORAM final : public ORAM<T, PathORAMBlock<T>> {
public:
  using Block = PathORAMBlock<T>;
  using Base = ORAM<T, Block>;
  using Range = typename Base::Range;

  explicit PathORAM(size_t size, size_t Z,
                    std::optional<uint64_t> seed = std::nullopt,
                    size_t stash_max = 0)
      : size_(size),
        Z_(Z),
        height_(compute_height(size)),
        max_path_(compute_max_path(height_)),
        physical_size_(compute_physical_size(max_path_, Z)),
        stash_max_(stash_max),
        rng_(seed ? *seed : std::random_device{}()),
        dist_(0, static_cast<uint32_t>(max_path_)) {

    // Initialize position map
    position_map_.resize(size);
    for (size_t i = 0; i < size; i++) {
      position_map_[i] = random_path();
    }
  }

  size_t size() const override { return size_; }
  size_t physical_size() const override { return physical_size_; }
  size_t max_stash_size() const override { return stash_max_; }

  // Returns block IDs currently in the stash (blocks not placed in the tree).
  std::vector<uint32_t> leak_stash() const {
    std::vector<uint32_t> ids;
    for (const auto& block : stash_) {
      if (block.valid) {
        ids.push_back(static_cast<uint32_t>(block.block_id));
      }
    }
    return ids;
  }

  // Replace stash contents wholesale (used when restoring runtime state).
  void set_stash(std::vector<Block> entries) {
    stash_ = std::move(entries);
  }

  // Replace position map wholesale (used when restoring runtime state).
  void set_position_map(std::vector<uint32_t> pm) {
    position_map_ = std::move(pm);
  }

  // Set the data size for dummy blocks (needed for container types like Bytes).
  void set_data_size(size_t n) { data_size_ = n; }

  // Return a copy of the current position map.
  std::vector<uint32_t> leak_position_map() const {
    return position_map_;
  }

  static size_t flat_index(size_t level, size_t offset) {
    return (static_cast<size_t>(1) << level) - 1 + offset;
  }

  std::vector<size_t> positions_on_path(uint32_t pid) const {
    std::vector<size_t> positions;
    positions.reserve(height_ + 1);
    for (size_t level = 0; level <= height_; level++) {
      size_t shift = height_ - level;
      size_t offset = (shift >= 32) ? 0 : (pid >> shift);
      positions.push_back(flat_index(level, offset));
    }
    return positions;
  }

  Range bucket_range(size_t node_index) const {
    Range range;
    range.start = node_index * Z_;
    range.count = Z_;
    range.stride = 1;
    return range;
  }

  std::vector<typename Base::Op> bucket_ranges_on_path(uint32_t pid) const {
    auto nodes = positions_on_path(pid);
    std::vector<typename Base::Op> ops;
    ops.reserve(nodes.size());
    for (size_t node_index : nodes) {
      ops.push_back(Base::read(bucket_range(node_index)));
    }
    return ops;
  }

protected:

  typename Base::AccessResult access_impl(size_t pos, T val) override {
    LOCK_EXCLUSIVE(lock_);
    // Path ID
    uint32_t pathid = position_map_[pos];
    position_map_[pos] = random_path();

    // Read buckets on the path.
    auto ops = bucket_ranges_on_path(pathid);
    auto read_results = STORE_ACCESS(ops);
    absorb_read_results(read_results);

    T old_value = read_from_stash(pos);
    write_to_stash(pos, val);

    std::vector<typename Base::Op> write_ops;
    write_ops.reserve(height_ + 1);
    for (size_t level = height_ + 1; level-- > 0; ) {
      size_t node_index = node_on_path(pathid, level);
      auto selected = take_blocks_for_bucket(pathid, level);
      auto bucket = fill_bucket(std::move(selected));
      write_ops.push_back(Base::write(bucket_range(node_index), std::move(bucket)));
    }
    (void)STORE_ACCESS(write_ops);

    UNLOCK(lock_);
    co_return old_value;
  }

private:

  static size_t compute_height(size_t size) {
    if (size == 0) {
      return 0;
    }
    int levels = static_cast<int>(
        std::ceil(std::log2(static_cast<double>(size))));
    return static_cast<size_t>(levels);
  }

  static size_t compute_max_path(size_t height) {
    return (static_cast<size_t>(1) << height) - 1;
  }

  static size_t compute_physical_size(size_t max_path, size_t Z) {
    size_t number_of_leaves = max_path + 1;
    return 2 * number_of_leaves * Z;
  }

  uint32_t random_path() {
    return dist_(rng_);
  }

  size_t node_on_path(uint32_t pid, size_t level) const {
    size_t shift = height_ - level;
    size_t offset = (shift >= 32) ? 0 : (pid >> shift);
    return flat_index(level, offset);
  }

  void absorb_read_results(const typename Base::AccessResults& results) {
    for (const auto& maybe_values : results.results) {
      if (!maybe_values) continue;
      for (const auto& block : *maybe_values) {
        if (block.valid) {
          stash_.push_back(block);
        }
      }
    }
  }

  T read_from_stash(size_t pos) const {
    for (const auto& block : stash_) {
      if (block.valid && block.block_id == pos) {
        return block.data;
      }
    }
    return T{};
  }

  void write_to_stash(size_t pos, const T& val) {
    std::vector<Block> remaining;
    remaining.reserve(stash_.size());
    for (const auto& block : stash_) {
      if (!block.valid || block.block_id != pos) {
        remaining.push_back(block);
      }
    }
    remaining.push_back(Block{pos, val, true});
    stash_.swap(remaining);
  }

  std::vector<Block> take_blocks_for_bucket(uint32_t pathid, size_t level) {
    std::vector<Block> selected;
    selected.reserve(Z_);
    std::vector<Block> remaining;
    remaining.reserve(stash_.size());

    size_t target_node = node_on_path(pathid, level);
    for (const auto& block : stash_) {
      if (selected.size() < Z_ && block.valid) {
        size_t block_node = node_on_path(position_map_[block.block_id], level);
        if (block_node == target_node) {
          selected.push_back(block);
          continue;
        }
      }
      remaining.push_back(block);
    }

    stash_.swap(remaining);
    return selected;
  }

  std::vector<Block> fill_bucket(std::vector<Block> selected) const {
    while (selected.size() < Z_) {
      selected.push_back(Block{size_, make_dummy_data(), false});
    }
    return selected;
  }

  T make_dummy_data() const {
    if constexpr (requires(size_t n) { T(n, typename T::value_type{}); }) {
      return T(data_size_, typename T::value_type{});
    } else {
      return T{};
    }
  }

  const size_t size_;
  const size_t Z_;
  const size_t height_;
  const size_t max_path_;
  const size_t physical_size_;
  const size_t stash_max_;
  size_t data_size_ = 0;

  std::mt19937_64 rng_;
  std::uniform_int_distribution<uint32_t> dist_;
  std::vector<Block> stash_;
  std::vector<uint32_t> position_map_;
  OramLock lock_;
};

#endif // PATH_ORAM_HPP
