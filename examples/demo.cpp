#include <oram/plaintext_oram.hpp>
#include <oram/naive_oram.hpp>
#include <oram/toy_oram.hpp>
#include <oram/memory_driver.hpp>
#include <oram/disk_driver.hpp>
#include <iostream>

int main() {
  constexpr size_t STORAGE_SIZE = 100;
  constexpr int DEFAULT_VALUE = 1000;

  // --- PlaintextORAM with MemoryDriver ---
  std::cout << "=== PlaintextORAM + MemoryDriver ===\n";
  {
    PlaintextORAM<int> oram(STORAGE_SIZE);
    MemoryDriver<int> driver(STORAGE_SIZE, DEFAULT_VALUE);

    auto op = oram.access(10, 42);
    int result = driver.do_access(std::move(op));
    std::cout << "access(10, 42) returned: " << result << "\n";
    std::cout << "Storage[10] is now: " << driver.storage()[10] << "\n";

    op = oram.access(10, 99);
    result = driver.do_access(std::move(op));
    std::cout << "access(10, 99) returned: " << result << "\n";
    std::cout << "Storage[10] is now: " << driver.storage()[10] << "\n";
  }

  // --- NaiveORAM with MemoryDriver ---
  std::cout << "\n=== NaiveORAM + MemoryDriver ===\n";
  {
    NaiveORAM<int> oram(STORAGE_SIZE);
    MemoryDriver<int> driver(STORAGE_SIZE, DEFAULT_VALUE);

    auto op = oram.access(10, 42);
    int result = driver.do_access(std::move(op));
    std::cout << "access(10, 42) returned: " << result << "\n";
    std::cout << "Storage[10] is now: " << driver.storage()[10] << "\n";

    op = oram.access(10, 99);
    result = driver.do_access(std::move(op));
    std::cout << "access(10, 99) returned: " << result << "\n";
    std::cout << "Storage[10] is now: " << driver.storage()[10] << "\n";
  }

  // --- PlaintextORAM with DiskDriver ---
  std::cout << "\n=== PlaintextORAM + DiskDriver ===\n";
  {
    PlaintextORAM<int> oram(STORAGE_SIZE);
    DiskDriver<int> driver("/tmp/oram_test", DEFAULT_VALUE);

    auto op = oram.access(5, 123);
    int result = driver.do_access(std::move(op));
    std::cout << "access(5, 123) returned: " << result << "\n";
    std::cout << "Files stored in: " << driver.root() << "\n";
  }

  // --- ToyORAM (original example) ---
  std::cout << "\n=== ToyORAM + MemoryDriver ===\n";
  {
    ToyORAM<int> oram(STORAGE_SIZE);
    MemoryDriver<int> driver(STORAGE_SIZE, DEFAULT_VALUE);

    auto op = oram.access(10, 7);
    int result = driver.do_access(std::move(op));
    std::cout << "access(10, 7) returned: " << result << "\n";
  }

  return 0;
}
