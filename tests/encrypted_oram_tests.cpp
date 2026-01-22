#include <oram/plaintext_oram.hpp>
#include <oram/naive_oram.hpp>
#include <oram/memory_driver.hpp>
#include <oram/encryption/encrypted_oram.hpp>
#include <oram/encryption/dummy_encryptor.hpp>

#include <cstddef>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

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

// Helper to convert int to bytes (matching EncryptedORAM's serialization)
std::vector<unsigned char> int_to_bytes(int val) {
  std::vector<unsigned char> bytes(sizeof(int));
  std::memcpy(bytes.data(), &val, sizeof(int));
  return bytes;
}

// Helper to convert bytes to int
int bytes_to_int(const std::vector<unsigned char>& bytes) {
  int val;
  std::memcpy(&val, bytes.data(), sizeof(int));
  return val;
}

void run_encrypted_oram_basic() {
  std::cout << "[TEST] EncryptedORAM basic with DummyEncryptor\n";

  using Bytes = std::vector<unsigned char>;
  constexpr size_t kSize = 8;
  constexpr int kDefaultValue = 42;

  // Inner ORAM works with plaintext ints
  PlaintextORAM<int> inner(kSize);

  // Wrap with encryption layer
  DummyEncryptor encryptor;
  DummyEncryptor::secret_key_type key{};
  EncryptedORAM<int, int, DummyEncryptor> encrypted(inner, encryptor, key);

  // Driver stores encrypted bytes (with DummyEncryptor, bytes are just serialized ints)
  MemoryDriver<int, Bytes> driver(kSize, int_to_bytes(kDefaultValue));

  // Reference for expected values
  std::vector<int> reference(kSize, kDefaultValue);

  expect_equal(encrypted.size(), kSize, "EncryptedORAM size()");
  expect_equal(encrypted.physical_size(), kSize, "EncryptedORAM physical_size()");

  struct Op {
    size_t pos;
    int val;
  };
  const std::vector<Op> ops = {
      {0, 10}, {4, 11}, {7, 12}, {0, 13}, {3, 14}, {7, 15},
  };

  for (const auto& op : ops) {
    std::cout << "  access(" << op.pos << ", " << op.val << ")\n";
    int expected_old = reference[op.pos];
    reference[op.pos] = op.val;

    auto access_op = encrypted.access(op.pos, op.val);
    int actual_old = driver.run(std::move(access_op));

    expect_equal(actual_old, expected_old, "EncryptedORAM access result");

    // Verify storage contains correct (encrypted) values
    // With DummyEncryptor, we can decode and check
    for (size_t i = 0; i < kSize; i++) {
      int stored = bytes_to_int(driver.storage()[i]);
      expect_equal(stored, reference[i],
                   "EncryptedORAM storage[" + std::to_string(i) + "]");
    }
  }
}

void run_encrypted_oram_with_naive() {
  std::cout << "[TEST] EncryptedORAM with NaiveORAM inner\n";

  using Bytes = std::vector<unsigned char>;
  constexpr size_t kSize = 8;
  constexpr int kDefaultValue = 0;

  // Inner is NaiveORAM (reads/writes all positions)
  NaiveORAM<int> inner(kSize);

  DummyEncryptor encryptor;
  DummyEncryptor::secret_key_type key{};
  EncryptedORAM<int, int, DummyEncryptor> encrypted(inner, encryptor, key);

  // NaiveORAM has physical_size = size
  MemoryDriver<int, Bytes> driver(inner.physical_size(), int_to_bytes(kDefaultValue));

  std::vector<int> reference(kSize, kDefaultValue);

  struct Op {
    size_t pos;
    int val;
  };
  const std::vector<Op> ops = {
      {0, 100}, {7, 200}, {3, 300}, {0, 400},
  };

  for (const auto& op : ops) {
    std::cout << "  access(" << op.pos << ", " << op.val << ")\n";
    int expected_old = reference[op.pos];
    reference[op.pos] = op.val;

    auto access_op = encrypted.access(op.pos, op.val);
    int actual_old = driver.run(std::move(access_op));

    expect_equal(actual_old, expected_old, "EncryptedORAM+NaiveORAM access result");
  }

  // Final storage check
  bool storage_matches = true;
  for (size_t i = 0; i < kSize; i++) {
    int stored = bytes_to_int(driver.storage()[i]);
    if (stored != reference[i]) {
      storage_matches = false;
      std::cerr << "  storage[" << i << "]=" << stored
                << " expected=" << reference[i] << "\n";
    }
  }
  expect_true(storage_matches, "EncryptedORAM+NaiveORAM final storage");
}

void run_encrypted_oram_size_one() {
  std::cout << "[TEST] EncryptedORAM size=1 edge case\n";

  using Bytes = std::vector<unsigned char>;
  constexpr size_t kSize = 1;
  constexpr int kDefaultValue = 99;

  PlaintextORAM<int> inner(kSize);
  DummyEncryptor encryptor;
  DummyEncryptor::secret_key_type key{};
  EncryptedORAM<int, int, DummyEncryptor> encrypted(inner, encryptor, key);
  MemoryDriver<int, Bytes> driver(kSize, int_to_bytes(kDefaultValue));

  auto op1 = encrypted.access(0, 1);
  int old1 = driver.run(std::move(op1));
  expect_equal(old1, kDefaultValue, "EncryptedORAM size=1 first access");

  auto op2 = encrypted.access(0, 2);
  int old2 = driver.run(std::move(op2));
  expect_equal(old2, 1, "EncryptedORAM size=1 second access");

  expect_equal(bytes_to_int(driver.storage()[0]), 2, "EncryptedORAM size=1 final storage");
}

} // namespace

int main() {
  run_encrypted_oram_basic();
  run_encrypted_oram_with_naive();
  run_encrypted_oram_size_one();

  if (fail_count == 0) {
    std::cout << "All EncryptedORAM tests passed.\n";
    return 0;
  }

  std::cerr << fail_count << " test(s) failed.\n";
  return 1;
}
