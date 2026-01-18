# ORAM Library - Build Guide

## Quick Start

```bash
make              # Build everything
make test         # Run all tests
```

## Build System

The project uses **CMake** with a convenience **Makefile** wrapper.

### Directory Structure
```
oram/
├── build/              # Build directory (generated)
├── include/oram/       # Public headers
├── src/               # Implementation files
├── tests/             # Test files
├── CMakeLists.txt     # CMake configuration
└── Makefile           # Convenience wrapper
```

## Available Make Targets

### Building

| Target | Description |
|--------|-------------|
| `make` | Build everything (default) |
| `make build` | Build all targets |
| `make configure` | Configure CMake (auto-run by build) |

### Testing

| Target | Description |
|--------|-------------|
| `make test` | Run all tests |
| `make test-simple` | Run Step 1 verification tests |
| `make test-path` | Run Path ORAM integration tests |
| `make test-network` | Run Path ORAM network tests |

### Cleaning

| Target | Description |
|--------|-------------|
| `make clean` | Remove build artifacts (keeps CMake cache) |
| `make distclean` | Remove everything including CMake cache and build/ |
| `make rebuild` | Clean + build |
| `make rebuild-all` | Distclean + build (full rebuild) |

### Help

| Target | Description |
|--------|-------------|
| `make help` | Show all available targets |

## Build Artifacts

After building, you'll find:

```
build/
├── liboram.a                    # ORAM static library
├── oram_simple_test             # Step 1 verification tests
├── path_oram_test               # Path ORAM integration tests
└── path_oram_network_test       # Path ORAM network tests
```

## CMake Direct Usage

If you prefer using CMake directly:

```bash
# Configure
mkdir -p build && cd build
cmake ..

# Build
make

# Clean (removes build artifacts)
make clean

# Distclean (removes CMake cache too)
make distclean

# Or from build directory:
cd build
cmake --build .                  # Build
cmake --build . --target clean   # Clean
```

## Examples

```bash
# First time setup
make                             # Configure + build

# Run all tests
make test

# Work on code, then rebuild
make rebuild

# Start completely fresh
make distclean
make

# Run specific test
make test-network
```

## Requirements

- **CMake** 3.16 or higher
- **C++20** compiler (GCC 11+ or Clang 13+)
- **OpenSSL** 3.0+
- **Make** (for convenience wrapper)

## Build Options

CMake options (use with `cmake -D<OPTION>=ON`):

```bash
# Enable testing (for Step 4)
cmake -DBUILD_TESTS=ON ..

# Enable benchmarks (for Step 4)
cmake -DBUILD_BENCHMARKS=ON ..
```

## Troubleshooting

### "CMake not found"
```bash
sudo apt install cmake  # Ubuntu/Debian
brew install cmake      # macOS
```

### "OpenSSL not found"
```bash
sudo apt install libssl-dev  # Ubuntu/Debian
brew install openssl         # macOS
```

### Build errors after updating code
```bash
make rebuild-all  # Full clean rebuild
```

### Stale CMake cache
```bash
make distclean    # Remove everything
make              # Reconfigure and build
```
