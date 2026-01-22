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
    int actual_old = driver.run(std::move(access_op));
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
  int old = driver.run(std::move(op));
  expect_equal(old, reference[0], name + ": size=1 first op");
  reference[0] = 1;
  expect_true(driver.storage() == reference, name + ": size=1 storage");
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
    int actual_old = driver.run(std::move(access_op));
    expect_equal(actual_old, expected_old, "PathORAM access result");
  }
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
    driver.run(std::move(access_op));
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
  run_tracker_oram_path_oram();

  if (fail_count == 0) {
    std::cout << "All functional ORAM tests passed.\n";
    return 0;
  }

  std::cerr << fail_count << " test(s) failed.\n";
  return 1;
}
