/**
 * Path ORAM Integration Tests
 *
 * Tests the complete Path ORAM implementation with various scenarios.
 */

#include <oram/oram_lib.h>
#include <iostream>
#include <cassert>
#include <random>
#include <map>

// Reference implementation using unordered_map for correctness comparison
class ReferenceOram {
public:
    ReferenceOram(size_t block_size) : block_size_(block_size) {}

    std::vector<oram::Byte> read(oram::BlockId id) {
        auto it = data_.find(id);
        if (it != data_.end()) {
            return it->second;
        }
        return std::vector<oram::Byte>(block_size_, 0);
    }

    void write(oram::BlockId id, const std::vector<oram::Byte>& data) {
        data_[id] = data;
    }

private:
    size_t block_size_;
    std::map<oram::BlockId, std::vector<oram::Byte>> data_;
};

void test_single_block_read_write() {
    std::cout << "Test: Single block read/write" << std::endl;

    oram::PathOram oram;
    oram::OramConfig config;
    config.num_blocks = 8;
    config.block_size = 64;
    config.Z = 4;
    config.use_local_server = true;
    config.rng = std::make_shared<oram::DeterministicOramRng>(12345);

    oram.init(config);

    // Write block 0
    std::vector<oram::Byte> data(64, 0xAA);
    oram.write(0, data);

    // Read it back
    auto read_data = oram.read(0);
    assert(read_data.size() == 64);
    assert(read_data == data);

    std::cout << "  PASSED" << std::endl;
}

void test_multiple_blocks() {
    std::cout << "Test: Multiple blocks" << std::endl;

    oram::PathOram oram;
    oram::OramConfig config;
    config.num_blocks = 16;
    config.block_size = 32;
    config.Z = 4;
    config.use_local_server = true;
    config.rng = std::make_shared<oram::DeterministicOramRng>(54321);

    oram.init(config);

    // Write multiple blocks with different patterns
    for (oram::BlockId i = 0; i < 8; i++) {
        std::vector<oram::Byte> data(32, static_cast<oram::Byte>(i));
        oram.write(i, data);
    }

    // Read them back
    for (oram::BlockId i = 0; i < 8; i++) {
        auto data = oram.read(i);
        assert(data.size() == 32);
        assert(data[0] == static_cast<oram::Byte>(i));
    }

    std::cout << "  PASSED" << std::endl;
}

void test_read_before_write() {
    std::cout << "Test: Read before write (should return zeros)" << std::endl;

    oram::PathOram oram;
    oram::OramConfig config;
    config.num_blocks = 8;
    config.block_size = 32;
    config.Z = 4;
    config.use_local_server = true;

    oram.init(config);

    // Read a block that was never written
    auto data = oram.read(5);
    assert(data.size() == 32);

    // Should be all zeros
    for (auto byte : data) {
        assert(byte == 0);
    }

    std::cout << "  PASSED" << std::endl;
}

void test_overwrite() {
    std::cout << "Test: Overwrite existing block" << std::endl;

    oram::PathOram oram;
    oram::OramConfig config;
    config.num_blocks = 8;
    config.block_size = 64;
    config.Z = 4;
    config.use_local_server = true;

    oram.init(config);

    // Write block 3
    std::vector<oram::Byte> data1(64, 0x11);
    oram.write(3, data1);

    // Read it
    auto read1 = oram.read(3);
    assert(read1 == data1);

    // Overwrite with new data
    std::vector<oram::Byte> data2(64, 0x22);
    oram.write(3, data2);

    // Read again
    auto read2 = oram.read(3);
    assert(read2 == data2);

    std::cout << "  PASSED" << std::endl;
}

void test_repeated_reads() {
    std::cout << "Test: Repeated reads of same block" << std::endl;

    oram::PathOram oram;
    oram::OramConfig config;
    config.num_blocks = 8;
    config.block_size = 32;
    config.Z = 4;
    config.use_local_server = true;

    oram.init(config);

    // Write a block
    std::vector<oram::Byte> data(32, 0x42);
    oram.write(7, data);

    // Read it multiple times
    for (int i = 0; i < 5; i++) {
        auto read_data = oram.read(7);
        assert(read_data == data);
    }

    std::cout << "  PASSED" << std::endl;
}

void test_access_api() {
    std::cout << "Test: Combined access API" << std::endl;

    oram::PathOram oram;
    oram::OramConfig config;
    config.num_blocks = 8;
    config.block_size = 64;
    config.Z = 4;
    config.use_local_server = true;

    oram.init(config);

    // Write using access
    std::vector<oram::Byte> write_data(64, 0x55);
    auto old_data = oram.access(2, write_data);
    // Should return zeros since block was never written
    for (auto byte : old_data) {
        assert(byte == 0);
    }

    // Read using access
    auto read_data = oram.access(2, std::nullopt);
    assert(read_data == write_data);

    std::cout << "  PASSED" << std::endl;
}

void test_random_workload() {
    std::cout << "Test: Random workload with reference comparison" << std::endl;

    const size_t num_blocks = 32;
    const size_t block_size = 128;
    const size_t num_operations = 100;

    oram::PathOram oram;
    oram::OramConfig config;
    config.num_blocks = num_blocks;
    config.block_size = block_size;
    config.Z = 4;
    config.use_local_server = true;
    config.rng = std::make_shared<oram::DeterministicOramRng>(99999);

    oram.init(config);

    ReferenceOram ref(block_size);
    std::mt19937 rng(12345);
    std::uniform_int_distribution<oram::BlockId> block_dist(0, num_blocks - 1);
    std::uniform_int_distribution<int> op_dist(0, 1);  // 0=read, 1=write
    std::uniform_int_distribution<int> byte_dist(0, 255);

    for (size_t op = 0; op < num_operations; op++) {
        oram::BlockId block_id = block_dist(rng);
        bool is_write = op_dist(rng) == 1;

        if (is_write) {
            // Write with random data
            std::vector<oram::Byte> data(block_size);
            for (auto& b : data) {
                b = static_cast<oram::Byte>(byte_dist(rng));
            }

            oram.write(block_id, data);
            ref.write(block_id, data);
        } else {
            // Read and compare
            auto oram_data = oram.read(block_id);
            auto ref_data = ref.read(block_id);

            assert(oram_data.size() == ref_data.size());
            assert(oram_data == ref_data);
        }
    }

    std::cout << "  PASSED (" << num_operations << " operations)" << std::endl;
}

void test_small_instance_invariants() {
    std::cout << "Test: Invariant checks on small instance" << std::endl;

    oram::PathOram oram;
    oram::OramConfig config;
    config.num_blocks = 8;
    config.block_size = 32;
    config.Z = 4;
    config.use_local_server = true;
    config.rng = std::make_shared<oram::DeterministicOramRng>(11111);

    oram.init(config);

    // Initial invariants should hold
    oram.check_invariants();

    // Write some blocks
    for (oram::BlockId i = 0; i < 4; i++) {
        std::vector<oram::Byte> data(32, static_cast<oram::Byte>(i * 10));
        oram.write(i, data);
        oram.check_invariants();  // Check after each write
    }

    // Read some blocks
    for (oram::BlockId i = 0; i < 4; i++) {
        oram.read(i);
        oram.check_invariants();  // Check after each read
    }

    // More writes
    for (oram::BlockId i = 4; i < 8; i++) {
        std::vector<oram::Byte> data(32, static_cast<oram::Byte>(i * 10));
        oram.write(i, data);
        oram.check_invariants();
    }

    std::cout << "  PASSED (invariants verified after each operation)" << std::endl;
}

void test_stash_monitoring() {
    std::cout << "Test: Stash size monitoring" << std::endl;

    oram::PathOram oram;
    oram::OramConfig config;
    config.num_blocks = 16;
    config.block_size = 64;
    config.Z = 4;
    config.use_local_server = true;

    oram.init(config);

    std::cout << "  Initial stash size: " << oram.stash_size() << std::endl;

    // Perform some operations and monitor stash
    size_t max_stash = 0;
    for (int i = 0; i < 20; i++) {
        std::vector<oram::Byte> data(64, static_cast<oram::Byte>(i));
        oram.write(i % 16, data);
        size_t stash = oram.stash_size();
        max_stash = std::max(max_stash, stash);
    }

    std::cout << "  Max stash size observed: " << max_stash << std::endl;
    std::cout << "  Final stash size: " << oram.stash_size() << std::endl;

    // Stash should remain reasonably small (typically < 10 for these parameters)
    assert(max_stash < 20);

    std::cout << "  PASSED" << std::endl;
}

void test_different_z_values() {
    std::cout << "Test: Different Z values" << std::endl;

    for (size_t z : {4, 5, 6, 8}) {
        std::cout << "  Testing Z=" << z << "..." << std::endl;

        oram::PathOram oram;
        oram::OramConfig config;
        config.num_blocks = 16;
        config.block_size = 32;
        config.Z = z;
        config.use_local_server = true;
        config.rng = std::make_shared<oram::DeterministicOramRng>(static_cast<uint64_t>(z * 1000));

        oram.init(config);

        // Write and read some blocks
        for (oram::BlockId i = 0; i < 8; i++) {
            std::vector<oram::Byte> data(32, static_cast<oram::Byte>(i + z));
            oram.write(i, data);
        }

        for (oram::BlockId i = 0; i < 8; i++) {
            auto data = oram.read(i);
            assert(data[0] == static_cast<oram::Byte>(i + z));
        }

        oram.check_invariants();
    }

    std::cout << "  PASSED" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Path ORAM - Integration Tests" << std::endl;
    std::cout << "========================================" << std::endl << std::endl;

    try {
        test_single_block_read_write();
        test_multiple_blocks();
        test_read_before_write();
        test_overwrite();
        test_repeated_reads();
        test_access_api();
        test_random_workload();
        test_small_instance_invariants();
        test_stash_monitoring();
        test_different_z_values();

        std::cout << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "All Path ORAM tests passed!" << std::endl;
        std::cout << "========================================" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << std::endl << "Test failed: " << e.what() << std::endl;
        return 1;
    }
}
