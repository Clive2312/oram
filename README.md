# ORAM Coroutine Framework

A C++20 header-only library for implementing Oblivious RAM (ORAM) algorithms using coroutines.
The framework cleanly separates ORAM access patterns from storage backends through a coroutine-based interface.

## Overview

ORAM (Oblivious RAM) hides memory access patterns from an adversary who can observe all memory operations. This library provides:

- **ORAM base class** with coroutine infrastructure for expressing access patterns
- **Driver interface** for pluggable storage backends
- **Example algorithms**: PlaintextORAM (baseline), NaiveORAM (O(N) scan), ToyORAM (demo)
- **Example drivers**: MemoryDriver (in-memory), DiskDriver (file-based)

## Architecture

```
┌─────────────────────┐
│   ORAM Algorithm    │  (PlaintextORAM, NaiveORAM, etc.)
│   - access_impl()   │  co_yield storage requests
└─────────┬───────────┘
          │ AccessReq / AccessResults
          ▼
┌─────────────────────┐
│      Driver         │  (MemoryDriver, DiskDriver, etc.)
│   - read_one()      │  executes actual I/O
│   - exchange_one()  │
│   - run()           │  schedules submitted coroutines
└─────────────────────┘
```

ORAM algorithms express **what** storage operations they need via `co_yield`, while drivers handle **how** those operations are executed. This allows:

- Testing ORAM algorithms with different storage backends
- Simulating various I/O characteristics
- Adding encryption, network transport, etc. in the driver layer

## Quick Start

```cpp
#include <oram/plaintext_oram.hpp>
#include <oram/memory_driver.hpp>

int main() {
    PlaintextORAM<int> oram(100);
    MemoryDriver<int> driver(100, 0);  // 100 slots, default value 0

    // access(pos, new_value) returns old value and writes new_value
    auto op = oram.access(10, 42);
    int old_value = driver.do_access(std::move(op));
    // old_value == 0 (the default), slot 10 now contains 42
}
```

Or use the convenience header to include everything:

```cpp
#include <oram/all.hpp>
```

## API Reference

### ORAM Base Class (`oram.hpp`)

All ORAM algorithms inherit from `ORAM<T>` and implement `access_impl()`:

```cpp
template <class T>
class ORAM {
public:
    // Main entry point: access position `pos`, write `val`, return old value
    AccessResult access(size_t pos, T val);

    // Logical and physical sizes
    size_t size() const;
    size_t physical_size() const;

protected:
    // Subclasses implement this using coroutines
    virtual AccessResult access_impl(size_t pos, T val) = 0;

    // Helpers for building operations
    static Op read(Positions positions);
    static Op write(Positions positions, std::vector<T> values);
    static Op exchange(Positions positions, std::vector<T> values);

    class AccessBuilder;
};
```

### Writing an ORAM Algorithm

Use `STORE_READ`/`STORE_WRITE`/`STORE_EXCHANGE` for the common single-op patterns:

```cpp
template <class T>
class MyORAM : public ORAM<T> {
public:
    explicit MyORAM(size_t size) : size_(size) {}

    size_t size() const override { return size_; }
    size_t physical_size() const override { return size_; }

protected:
    typename ORAM<T>::AccessResult access_impl(size_t pos, T val) override {
        // Read positions 0 and 1
        auto values = STORE_READ((std::vector<size_t>{0, 1}));

        // Write to position 0
        STORE_WRITE((std::vector<size_t>{0}), (std::vector<T>{val}));

        co_return values[0];
    }

private:
    size_t size_;
};
```

For mixed/batched requests, use `STORE_ACCESS` with operator chaining:

```cpp
auto results = STORE_ACCESS(
    ORAM<T>::read(std::vector<size_t>{0}) +
    ORAM<T>::exchange(std::vector<size_t>{1}, std::vector<T>{val})
);
```

### Positions

Operations can specify positions as either:
- `std::vector<size_t>` - explicit list of positions
- `ORAM<T>::Range{start, count, stride}` - arithmetic sequence

### Driver Base Class (`driver.hpp`)

Drivers execute storage operations and coordinate coroutines:

```cpp
template <class T>
class Driver {
public:
    // Subclasses implement these
    virtual T read_one(size_t pos) = 0;
    virtual T exchange_one(size_t pos, const T& value) = 0;

    // Execute a batch of operations
    virtual AccessResults execute(const AccessReq& req);

    // Schedule or run coroutines
    Token submit(AccessResult op);
    void run();
    T wait(Token token);
    T do_access(AccessResult op);  // Convenience helper (C++ reserves `do`)
};
```

### Included ORAM Algorithms

| Algorithm | File | Description | Complexity |
|-----------|------|-------------|------------|
| PlaintextORAM | `plaintext_oram.hpp` | Direct read/write, no obfuscation (baseline) | O(1) |
| NaiveORAM | `naive_oram.hpp` | Reads and writes all N positions every access | O(N) |
| ToyORAM | `toy_oram.hpp` | Demo showing multi-step coroutine pattern (not an ORAM) | - |

### Included Drivers

| Driver | File | Description |
|--------|------|-------------|
| MemoryDriver | `memory_driver.hpp` | In-memory `std::vector<T>` storage |
| DiskDriver | `disk_driver.hpp` | File-per-slot storage in a directory |

## Building

Requires C++20 with coroutine support (GCC 10+, Clang 14+, MSVC 19.28+).

```bash
make          # Build release
make debug    # Build with debug symbols
make clean    # Clean build artifacts
```

Or manually:
```bash
g++ -std=c++20 -O2 -I include -o demo examples/demo.cpp
```

## Project Structure

```
include/oram/
  oram.hpp           - ORAM base class and coroutine machinery
  driver.hpp         - Driver base class
  memory_driver.hpp  - In-memory storage driver
  disk_driver.hpp    - File-based storage driver
  plaintext_oram.hpp - Non-oblivious baseline implementation
  naive_oram.hpp     - O(N) oblivious implementation
  toy_oram.hpp       - Example/demo implementation
  all.hpp            - Convenience header (includes all)
examples/
  demo.cpp           - Usage examples
Makefile             - Build configuration
README.md            - This file
```

## Extending the Library

### Custom ORAM Algorithm

```cpp
#include <oram/oram.hpp>

template <class T>
class PathORAM : public ORAM<T> {
public:
    PathORAM(size_t num_leaves, size_t bucket_size);

protected:
    typename ORAM<T>::AccessResult access_impl(size_t pos, T val) override {
        // 1. Lookup position in position map
        // 2. Read path from root to leaf
        auto path_data = STORE_ACCESS(...);

        // 3. Find block, update stash
        // 4. Write path back with eviction
        STORE_ACCESS(...);

        co_return old_value;
    }
};
```

### Custom Driver

```cpp
#include <oram/driver.hpp>

template <class T>
class EncryptedDriver : public Driver<T> {
public:
    T read_one(size_t pos) override {
        auto ciphertext = backend_.read(pos);
        return decrypt(ciphertext);
    }

    T exchange_one(size_t pos, const T& value) override {
        T old = read_one(pos);
        backend_.write(pos, encrypt(value));
        return old;
    }
};
```

## DSL Helpers

For the common single-op cases, use the built-in macros:

```cpp
auto values = STORE_READ((std::vector<size_t>{pos, pos + 1}));
STORE_WRITE((std::vector<size_t>{pos}), (std::vector<T>{val}));
auto old = STORE_EXCHANGE((std::vector<size_t>{pos}), (std::vector<T>{val}));
```

For multi-op batching, you can build the request directly:

```cpp
auto results = STORE_ACCESS(
  ORAM<T>::read(std::vector<size_t>{a}) +
  ORAM<T>::exchange(std::vector<size_t>{b}, std::vector<T>{v})
);
```

Or via the builder:

```cpp
auto req = typename ORAM<T>::AccessBuilder{}
  .read(std::vector<size_t>{a})
  .write(std::vector<size_t>{b}, std::vector<T>{v})
  .build();

auto results = STORE_ACCESS(req);
```
