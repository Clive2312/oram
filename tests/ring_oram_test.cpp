/**
 * Ring ORAM Integration Tests
 *
 * Tests the Ring ORAM implementation with various scenarios.
 */

#include <oram/oram_lib.h>
#include <iostream>
#include <cassert>
#include <random>

// Helper: Generate random data with seed (deterministic)
std::vector<oram::Byte> generate_random_data_seeded(size_t size, uint64_t seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> dist(0, 255);
    std::vector<oram::Byte> data(size);
    for (auto& byte : data) {
        byte = static_cast<oram::Byte>(dist(rng));
    }
    return data;
}

void test_single_block_read_write() {
    std::cout << "Test: Single block read/write (Ring ORAM)" << std::endl;

    oram::RingOram oram;
    oram::OramConfig config;
    config.num_blocks = 8;
    config.block_size = 64;
    config.Z = 3;           // Bucket capacity
    config.S = 2;           // Extra dummy slots
    config.A = 4;           // Eviction rate
    config.use_local_server = true;
    config.rng = std::make_shared<oram::DeterministicOramRng>(12345);

    oram.init(config);

    // Write block 0 with random data
    auto data = generate_random_data_seeded(64, 99999);
    oram.write(0, data);

    // Read it back
    auto read_data = oram.read(0);
    assert(read_data.size() == 64);
    assert(read_data == data);

    std::cout << "  PASSED" << std::endl;
}

void test_multiple_blocks() {
    std::cout << "Test: Multiple blocks (Ring ORAM)" << std::endl;

    oram::RingOram oram;
    oram::OramConfig config;
    config.num_blocks = 16;
    config.block_size = 32;
    config.Z = 3;
    config.S = 2;
    config.A = 4;
    config.use_local_server = true;
    config.rng = std::make_shared<oram::DeterministicOramRng>(54321);

    oram.init(config);

    // Write multiple blocks with different random data
    std::vector<std::vector<oram::Byte>> written_data;
    for (oram::BlockId i = 0; i < 8; i++) {
        auto data = generate_random_data_seeded(32, 1000 + i);
        written_data.push_back(data);
        oram.write(i, data);
    }

    // Read them back and verify
    for (oram::BlockId i = 0; i < 8; i++) {
        auto data = oram.read(i);
        assert(data.size() == 32);
        assert(data == written_data[i]);
    }

    std::cout << "  PASSED" << std::endl;
}

void test_read_before_write() {
    std::cout << "Test: Read before write (should throw error)" << std::endl;

    oram::RingOram oram;
    oram::OramConfig config;
    config.num_blocks = 8;
    config.block_size = 32;
    config.Z = 3;
    config.S = 2;
    config.A = 4;
    config.use_local_server = true;

    oram.init(config);

    // Read a block that was never written - should throw error
    bool caught_exception = false;
    try {
        auto data = oram.read(5);
        assert(false && "Should have thrown exception");
    } catch (const std::runtime_error& e) {
        caught_exception = true;
    }
    assert(caught_exception);

    std::cout << "  PASSED" << std::endl;
}

void test_eviction_schedule() {
    std::cout << "Test: Eviction schedule (Ring ORAM)" << std::endl;

    oram::RingOram oram;
    oram::OramConfig config;
    config.num_blocks = 16;
    config.block_size = 64;
    config.Z = 3;
    config.S = 2;
    config.A = 3;           // Evict every 3 accesses
    config.use_local_server = true;
    config.rng = std::make_shared<oram::DeterministicOramRng>(11111);

    oram.init(config);

    // Write some blocks
    for (oram::BlockId i = 0; i < 6; i++) {
        auto data = generate_random_data_seeded(64, 5000 + i);
        oram.write(i, data);
    }

    // Read them back (this will trigger eviction every A accesses)
    for (oram::BlockId i = 0; i < 6; i++) {
        auto data = oram.read(i);
        assert(data.size() == 64);
        // Data should match what we wrote
        auto expected = generate_random_data_seeded(64, 5000 + i);
        assert(data == expected);
    }

    std::cout << "  PASSED (eviction triggered periodically)" << std::endl;
}

void test_invariants() {
    std::cout << "Test: Invariant checks on small instance" << std::endl;

    oram::RingOram oram;
    oram::OramConfig config;
    config.num_blocks = 8;
    config.block_size = 32;
    config.Z = 3;
    config.S = 2;
    config.A = 4;
    config.use_local_server = true;
    config.rng = std::make_shared<oram::DeterministicOramRng>(99999);

    oram.init(config);

    // Initial invariants should hold
    oram.check_invariants();

    // Write some blocks
    for (oram::BlockId i = 0; i < 4; i++) {
        auto data = generate_random_data_seeded(32, 6000 + i);
        oram.write(i, data);
        oram.check_invariants();  // Check after each write
    }

    // Read some blocks
    for (oram::BlockId i = 0; i < 4; i++) {
        oram.read(i);
        oram.check_invariants();  // Check after each read
    }

    std::cout << "  PASSED (invariants verified after each operation)" << std::endl;
}

void test_stash_monitoring() {
    std::cout << "Test: Stash size monitoring (Ring ORAM)" << std::endl;

    oram::RingOram oram;
    oram::OramConfig config;
    config.num_blocks = 16;
    config.block_size = 64;
    config.Z = 3;
    config.S = 2;
    config.A = 4;
    config.use_local_server = true;

    oram.init(config);

    std::cout << "  Initial stash size: " << oram.stash_size() << std::endl;

    // Perform some operations and monitor stash
    size_t max_stash = 0;
    for (int i = 0; i < 20; i++) {
        auto data = generate_random_data_seeded(64, 7000 + i);
        oram.write(i % 16, data);
        size_t stash = oram.stash_size();
        max_stash = std::max(max_stash, stash);
    }

    std::cout << "  Max stash size observed: " << max_stash << std::endl;
    std::cout << "  Final stash size: " << oram.stash_size() << std::endl;

    // Stash should remain reasonably small
    assert(max_stash < 30);

    std::cout << "  PASSED" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Ring ORAM - Integration Tests" << std::endl;
    std::cout << "========================================" << std::endl << std::endl;

    try {
        test_single_block_read_write();
        test_multiple_blocks();
        test_read_before_write();
        test_eviction_schedule();
        test_invariants();
        test_stash_monitoring();

        std::cout << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "All Ring ORAM tests passed!" << std::endl;
        std::cout << "========================================" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << std::endl << "Test failed: " << e.what() << std::endl;
        return 1;
    }
}
