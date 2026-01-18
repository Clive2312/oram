# Convenience Makefile for ORAM library
# Wraps CMake commands so you don't need to cd into build/

BUILD_DIR := build

.PHONY: all configure build test clean distclean rebuild help

# Default target
all: build

# Configure CMake (only needed first time or after clean)
configure:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake ..

# Build everything
build: configure
	@$(MAKE) -C $(BUILD_DIR)

# Run all tests
test: build
	@echo "Running Step 1 tests..."
	@./$(BUILD_DIR)/oram_simple_test
	@echo ""
	@echo "Running Path ORAM tests..."
	@./$(BUILD_DIR)/path_oram_test
	@echo ""
	@echo "Running Path ORAM network tests..."
	@./$(BUILD_DIR)/path_oram_network_test

# Run individual tests
test-simple: build
	@./$(BUILD_DIR)/oram_simple_test

test-path: build
	@./$(BUILD_DIR)/path_oram_test

test-network: build
	@./$(BUILD_DIR)/path_oram_network_test

# Clean build artifacts (keeps CMake cache)
clean:
	@if [ -d $(BUILD_DIR) ]; then \
		$(MAKE) -C $(BUILD_DIR) clean; \
	fi

# Clean everything including CMake cache
distclean:
	@if [ -d $(BUILD_DIR) ]; then \
		$(MAKE) -C $(BUILD_DIR) distclean; \
	fi
	@rm -rf $(BUILD_DIR)
	@echo "Removed build directory"

# Rebuild from scratch (clean + build)
rebuild: clean build

# Full rebuild (distclean + build)
rebuild-all: distclean build

# Help message
help:
	@echo "ORAM Library - Build System"
	@echo ""
	@echo "Available targets:"
	@echo "  make              - Build everything (default)"
	@echo "  make configure    - Configure CMake"
	@echo "  make build        - Build all targets"
	@echo "  make test         - Run all tests"
	@echo "  make test-simple  - Run Step 1 verification tests"
	@echo "  make test-path    - Run Path ORAM integration tests"
	@echo "  make test-network - Run Path ORAM network tests"
	@echo "  make clean        - Remove build artifacts (keep CMake cache)"
	@echo "  make distclean    - Remove everything including CMake cache"
	@echo "  make rebuild      - Clean and rebuild"
	@echo "  make rebuild-all  - Distclean and rebuild from scratch"
	@echo "  make help         - Show this help message"
	@echo ""
	@echo "Examples:"
	@echo "  make              # Build everything"
	@echo "  make test         # Run all tests"
	@echo "  make clean        # Clean build artifacts"
	@echo "  make rebuild-all  # Full rebuild from scratch"
