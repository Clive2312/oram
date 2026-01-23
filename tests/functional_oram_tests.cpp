#include <oram/plaintext_oram.hpp>
#include <oram/naive_oram.hpp>
#include <oram/memory_driver.hpp>
#include <oram/path_oram.hpp>
#include <oram/tracker_oram.hpp>

#include <cstddef>
#include <iostream>
#include <string>
#include <vector>
#include <variant>

namespace {

int fail_count = 0;

void expect_true(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << "\n";
    ++fail_count;
  }
}

template <class T>
void expect_equal(const T& actual, const T& expected, const std::string& message) {
  if (!(actual == expected)) {
    std::cerr << "FAIL: " << message << " (actual=" << actual
              << ", expected=" << expected << ")\n";
    ++fail_count;
  }
}

template <class Oram>
void run_access_sequence(const std::string& name, size_t size) {
  std::cout << "[TEST] " << name << " access sequence (size=" << size << ")\n";
  constexpr int kDefaultValue = 7;
  MemoryDriver<int> driver(size, kDefaultValue);
  Oram oram(size);
  std::vector<int> reference(size, kDefaultValue);

  expect_equal(oram.size(), size, name + ": size()");
  expect_equal(oram.physical_size(), size, name + ": physical_size()");

  struct Op {
    size_t pos;
    int val;
  };
  const std::vector<Op> ops = {
      {0, 10}, {size / 2, 11}, {size - 1, 12}, {0, 13},
      {size / 2, 14}, {1, 15}, {size - 1, 16}, {2, 17},
  };

  for (const auto& op : ops) {
    std::cout << "  access(" << op.pos << ", " << op.val << ")\n";
    int expected_old = reference[op.pos];
    reference[op.pos] = op.val;

    auto access_op = oram.access(op.pos, op.val);
    int actual_old = driver.do_access(std::move(access_op));
    expect_equal(actual_old, expected_old, name + ": access result");
    expect_true(driver.storage() == reference, name + ": storage mirror");
  }
}

template <class Oram>
void run_small_size(const std::string& name) {
  std::cout << "[TEST] " << name << " size=1 edge case\n";
  constexpr size_t kSize = 1;
  constexpr int kDefaultValue = 0;
  MemoryDriver<int> driver(kSize, kDefaultValue);
  Oram oram(kSize);
  std::vector<int> reference(kSize, kDefaultValue);

  auto op = oram.access(0, 1);
  int old = driver.do_access(std::move(op));
  expect_equal(old, reference[0], name + ": size=1 first op");
  reference[0] = 1;
  expect_true(driver.storage() == reference, name + ": size=1 storage");
}

struct ActiveStats {
  int active = 0;
  int max_active = 0;
};

class LockedCounterOram final : public ORAM<int> {
public:
  LockedCounterOram(size_t size, ActiveStats* stats)
      : size_(size), stats_(stats) {
    if (!stats_) throw std::invalid_argument("LockedCounterOram: stats is null");
  }

  size_t size() const override { return size_; }
  size_t physical_size() const override { return size_; }

protected:
  AccessResult access_impl(size_t pos, int val) override {
    LOCK_EXCLUSIVE(lock_);
    stats_->active++;
    if (stats_->active > stats_->max_active) {
      stats_->max_active = stats_->active;
    }

    auto read_res = STORE_READ((std::vector<size_t>{pos}));
    STORE_WRITE((std::vector<size_t>{pos}), (std::vector<int>{val}));

    stats_->active--;
    UNLOCK(lock_);

    co_return read_res[0];
  }

private:
  size_t size_;
  ActiveStats* stats_;
  OramLock lock_;
};

struct LockStats {
  int readers = 0;
  int writers = 0;
  int max_readers = 0;
  int max_writers = 0;
  int overlap_errors = 0;
};

class MixedLockOram final : public ORAM<int> {
public:
  MixedLockOram(size_t size, LockStats* stats)
      : size_(size), stats_(stats) {
    if (!stats_) throw std::invalid_argument("MixedLockOram: stats is null");
  }

  size_t size() const override { return size_; }
  size_t physical_size() const override { return size_; }

protected:
  AccessResult access_impl(size_t pos, int val) override {
    if (val < 0) {
      LOCK_READ(lock_);
      stats_->readers++;
      if (stats_->writers > 0) {
        stats_->overlap_errors++;
      }
      if (stats_->readers > stats_->max_readers) {
        stats_->max_readers = stats_->readers;
      }

      auto read_res = STORE_READ((std::vector<size_t>{pos}));
      stats_->readers--;
      UNLOCK(lock_);
      co_return read_res[0];
    }

    LOCK_EXCLUSIVE(lock_);
    stats_->writers++;
    if (stats_->writers > 1 || stats_->readers > 0) {
      stats_->overlap_errors++;
    }
    if (stats_->writers > stats_->max_writers) {
      stats_->max_writers = stats_->writers;
    }

    auto read_res = STORE_READ((std::vector<size_t>{pos}));
    STORE_WRITE((std::vector<size_t>{pos}), (std::vector<int>{val}));

    stats_->writers--;
    UNLOCK(lock_);

    co_return read_res[0];
  }

private:
  size_t size_;
  LockStats* stats_;
  OramLock lock_;
};

void run_driver_lock_test() {
  std::cout << "[TEST] Driver cooperative locking\n";
  constexpr size_t kSize = 4;
  ActiveStats stats;
  LockedCounterOram oram(kSize, &stats);
  MemoryDriver<int> driver(kSize, 0);

  auto token1 = driver.submit(oram.access(0, 10));
  auto token2 = driver.submit(oram.access(0, 11));
  auto token3 = driver.submit(oram.access(0, 12));

  driver.run();

  int result1 = driver.wait(token1);
  int result2 = driver.wait(token2);
  int result3 = driver.wait(token3);

  (void)result1;
  (void)result2;
  (void)result3;

  expect_equal(stats.max_active, 1,
               "Driver exclusive lock serializes critical sections");
}

void run_driver_lock_stress_test() {
  std::cout << "[TEST] Driver mixed read/write lock stress\n";
  constexpr size_t kSize = 8;
  LockStats stats;
  MixedLockOram oram(kSize, &stats);
  MemoryDriver<int> driver(kSize, 0);

  std::vector<Driver<int>::Token> tokens;
  tokens.reserve(20);

  for (int i = 0; i < 10; ++i) {
    tokens.push_back(driver.submit(oram.access(static_cast<size_t>(i % kSize), -1)));
  }
  for (int i = 0; i < 10; ++i) {
    tokens.push_back(driver.submit(oram.access(static_cast<size_t>(i % kSize), i)));
  }

  driver.run();
  for (auto token : tokens) {
    (void)driver.wait(token);
  }

  expect_equal(stats.overlap_errors, 0,
               "Driver prevents read/write overlap in critical section");
  expect_equal(stats.max_writers, 1,
               "Driver enforces single writer in critical section");
}

void run_path_oram_tree_tests() {
  std::cout << "[TEST] PathORAM tree helpers\n";
  using Oram = PathORAM<int>;
  constexpr size_t kSize = 8;
  constexpr size_t kZ = 2;
  Oram oram(kSize, kZ, 123);

  expect_equal(Oram::flat_index(0, 0), static_cast<size_t>(0),
               "PathORAM flat_index root");
  expect_equal(Oram::flat_index(1, 0), static_cast<size_t>(1),
               "PathORAM flat_index level1 left");
  expect_equal(Oram::flat_index(1, 1), static_cast<size_t>(2),
               "PathORAM flat_index level1 right");
  expect_equal(Oram::flat_index(2, 3), static_cast<size_t>(6),
               "PathORAM flat_index level2 offset3");
  expect_equal(Oram::flat_index(3, 7), static_cast<size_t>(14),
               "PathORAM flat_index level3 offset7");

  std::vector<size_t> expected_nodes = {0, 2, 5, 12};
  auto nodes = oram.positions_on_path(5);
  expect_true(nodes == expected_nodes, "PathORAM positions_on_path pid=5");

  std::vector<Oram::Range> expected_ranges = {
      {0, 2, 1}, {4, 2, 1}, {10, 2, 1}, {24, 2, 1}};
  auto ops = oram.bucket_ranges_on_path(5);
  expect_equal(ops.size(), expected_ranges.size(),
               "PathORAM bucket_ranges_on_path pid=5 size");
  bool ops_match = ops.size() == expected_ranges.size();
  for (size_t i = 0; i < ops.size() && ops_match; i++) {
    const auto& actual = ops[i];
    const auto& expected = expected_ranges[i];
    ops_match = actual.kind == Oram::Op::Kind::Read &&
                std::holds_alternative<std::vector<size_t>>(actual.positions) == false &&
                std::get<Oram::Range>(actual.positions).start == expected.start &&
                std::get<Oram::Range>(actual.positions).count == expected.count &&
                std::get<Oram::Range>(actual.positions).stride == expected.stride;
  }
  expect_true(ops_match, "PathORAM bucket_ranges_on_path pid=5 values");
}

void run_path_oram_correctness() {
  std::cout << "[TEST] PathORAM correctness sequence\n";
  using Oram = PathORAM<int>;
  using Block = PathORAMBlock<int>;
  constexpr size_t kSize = 16;
  constexpr size_t kZ = 3;
  Oram oram(kSize, kZ, 123);
  Block default_block{0, 0, false};
  MemoryDriver<int, Block> driver(oram.physical_size(), default_block);
  std::vector<int> reference(kSize, 0);

  struct Op {
    size_t pos;
    int val;
  };
  const std::vector<Op> ops = {
      {0, 10}, {5, 11}, {15, 12}, {5, 13},
      {1, 14}, {8, 15}, {0, 16}, {15, 17},
  };

  for (const auto& op : ops) {
    std::cout << "  access(" << op.pos << ", " << op.val << ")\n";
    int expected_old = reference[op.pos];
    reference[op.pos] = op.val;
    auto access_op = oram.access(op.pos, op.val);
    int actual_old = driver.do_access(std::move(access_op));
    expect_equal(actual_old, expected_old, "PathORAM access result");
  }
}

void run_path_oram_lock_serialization() {
  std::cout << "[TEST] PathORAM lock serialization\n";
  using Oram = PathORAM<int>;
  using Block = PathORAMBlock<int>;
  constexpr size_t kSize = 8;
  constexpr size_t kZ = 2;
  Oram oram(kSize, kZ, 123);
  Block default_block{0, 0, false};
  MemoryDriver<int, Block> driver(oram.physical_size(), default_block);

  auto token1 = driver.submit(oram.access(3, 10));
  auto token2 = driver.submit(oram.access(3, 20));

  driver.run();

  int old1 = driver.wait(token1);
  int old2 = driver.wait(token2);

  expect_equal(old1, 0, "PathORAM lock first access");
  expect_equal(old2, 10, "PathORAM lock second access");

  int old3 = driver.do_access(oram.access(3, 30));
  expect_equal(old3, 20, "PathORAM lock final state");
}

void run_tracker_oram_path_oram() {
  std::cout << "[TEST] TrackerORAM with PathORAM\n";
  using Oram = PathORAM<int>;
  using Block = PathORAMBlock<int>;
  constexpr size_t kSize = 8;
  constexpr size_t kZ = 2;

  Oram inner(kSize, kZ, 123);
  TrackerORAM<int, Block> tracked(inner);
  Block default_block{0, 0, false};
  MemoryDriver<int, Block> driver(tracked.physical_size(), default_block);

  for (size_t pos = 0; pos < inner.size(); ++pos) {
    auto access_op = tracked.access(pos, static_cast<int>(pos * 10));
    driver.do_access(std::move(access_op));
  }

  auto perm = tracked.extract_permutation();
  expect_equal(perm.size(), tracked.physical_size(),
               "TrackerORAM perm size matches physical_size");

  std::cout << "  tracker perm ids: ";
  for (size_t i = 0; i < perm.size(); ++i) {
    std::cout << perm[i] << (i + 1 == perm.size() ? "" : " ");
  }
  std::cout << "\n";

  std::vector<int> initial(tracked.physical_size(), -1);
  for (size_t i = 0; i < inner.size(); ++i) {
    initial[i] = static_cast<int>(i);
  }

  std::vector<int> permuted(tracked.physical_size(), -1);
  for (size_t i = 0; i < perm.size(); ++i) {
    if (perm[i] >= 0) {
      permuted[i] = initial[static_cast<size_t>(perm[i])];
    }
  }

  std::cout << "  permuted ids: ";
  for (size_t i = 0; i < permuted.size(); ++i) {
    std::cout << permuted[i] << (i + 1 == permuted.size() ? "" : " ");
  }
  std::cout << "\n";

  const auto& storage = driver.storage();
  std::vector<int> storage_ids;
  storage_ids.reserve(storage.size());
  for (const auto& block : storage) {
    storage_ids.push_back(block.valid ? static_cast<int>(block.block_id) : -1);
  }

  std::cout << "  storage ids:  ";
  for (size_t i = 0; i < storage_ids.size(); ++i) {
    std::cout << storage_ids[i] << (i + 1 == storage_ids.size() ? "" : " ");
  }
  std::cout << "\n";

  expect_true(permuted == storage_ids,
              "TrackerORAM permutation mirrors storage ids");
}

} // namespace

int main() {
  run_access_sequence<PlaintextORAM<int>>("PlaintextORAM", 16);
  run_access_sequence<NaiveORAM<int>>("NaiveORAM", 16);
  run_small_size<PlaintextORAM<int>>("PlaintextORAM");
  run_small_size<NaiveORAM<int>>("NaiveORAM");
  run_path_oram_tree_tests();
  run_path_oram_correctness();
  run_path_oram_lock_serialization();
  run_tracker_oram_path_oram();
  run_driver_lock_test();
  run_driver_lock_stress_test();

  if (fail_count == 0) {
    std::cout << "All functional ORAM tests passed.\n";
    return 0;
  }

  std::cerr << fail_count << " test(s) failed.\n";
  return 1;
}
