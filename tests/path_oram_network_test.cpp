/**
 * Path ORAM Network Test
 *
 * Tests Path ORAM with actual client-server communication over network.
 * Server runs in a separate thread, client connects via NetIO.
 */

#include <oram/oram_lib.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <cassert>
#include <map>
#include <random>

void run_server(int port, size_t num_buckets, size_t bucket_size,
                const oram::AeadKey& key, size_t block_size, size_t Z) {
    std::cout << "[Server] Starting on port " << port << std::endl;

    try {
        // Create server
        oram::OramServer server(num_buckets, bucket_size);

        // Initialize with properly encrypted dummy buckets
        // Server needs to use the same key as the client
        oram::Aead aead(key);
        oram::BucketCodec codec(aead, block_size, Z);

        server.init([&](oram::NodeId) {
            // Generate encrypted dummy bucket
            oram::Bucket dummy_bucket(Z, block_size);
            return codec.encode(dummy_bucket);
        });

        std::cout << "[Server] Initialized with " << num_buckets
                  << " buckets, " << bucket_size << " bytes each" << std::endl;

        // Create server-side NetIO and wait for client connection
        NetIO io(nullptr, port, true);  // server mode

        std::cout << "[Server] Waiting for client connection..." << std::endl;

        // Handle client requests
        server.handle_client(io);

    } catch (const std::exception& e) {
        std::cerr << "[Server] Error: " << e.what() << std::endl;
    }

    std::cout << "[Server] Shutting down" << std::endl;
}

void test_network_mode() {
    std::cout << "Test: Path ORAM with network client-server" << std::endl;

    const int port = 12345;
    const size_t num_blocks = 16;
    const size_t block_size = 64;
    const size_t Z = 4;

    // Compute parameters to know bucket size
    auto params = oram::OramParams::compute(num_blocks, block_size, Z);

    // Create AEAD key and codec to compute encrypted bucket size
    auto key = oram::AeadKey::generate();
    oram::Aead aead(key);
    oram::BucketCodec codec(aead, block_size, Z);
    size_t encrypted_bucket_size = codec.encrypted_bucket_size();

    std::cout << "  Starting server thread..." << std::endl;

    // Start server in background thread
    std::thread server_thread([&]() {
        run_server(port, params.num_buckets, encrypted_bucket_size, key, block_size, Z);
    });

    // Give server time to start up
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    std::cout << "  Connecting client to server..." << std::endl;

    try {
        // Create client-side network connection
        NetIO client_io("127.0.0.1", port);

        std::cout << "  Client connected!" << std::endl;

        // Create Path ORAM with network mode
        oram::PathOram oram;
        oram::OramConfig config;
        config.num_blocks = num_blocks;
        config.block_size = block_size;
        config.Z = Z;
        config.use_local_server = false;  // Use remote server
        config.network = &client_io;
        config.key = key;  // Use same key as codec
        config.rng = std::make_shared<oram::DeterministicOramRng>(99999);

        std::cout << "  Initializing ORAM..." << std::endl;
        oram.init(config);

        std::cout << "  Running ORAM operations..." << std::endl;

        // Test 1: Write some blocks
        std::cout << "    Writing blocks..." << std::endl;
        for (oram::BlockId i = 0; i < 8; i++) {
            std::vector<oram::Byte> data(block_size, static_cast<oram::Byte>(i * 10));
            oram.write(i, data);
        }

        // Test 2: Read them back
        std::cout << "    Reading blocks..." << std::endl;
        for (oram::BlockId i = 0; i < 8; i++) {
            auto data = oram.read(i);
            assert(data.size() == block_size);
            assert(data[0] == static_cast<oram::Byte>(i * 10));
        }

        // Test 3: Overwrite
        std::cout << "    Overwriting blocks..." << std::endl;
        std::vector<oram::Byte> new_data(block_size, 0xAA);
        oram.write(3, new_data);
        auto read_data = oram.read(3);
        assert(read_data == new_data);

        // Test 4: Read unwritten block
        std::cout << "    Reading unwritten block..." << std::endl;
        auto zero_data = oram.read(15);
        assert(zero_data.size() == block_size);
        for (auto byte : zero_data) {
            assert(byte == 0);
        }

        std::cout << "  All operations completed successfully!" << std::endl;
        std::cout << "  Final stash size: " << oram.stash_size() << std::endl;

        // Close client connection
        // NetIO will close on destruction

    } catch (const std::exception& e) {
        std::cerr << "[Client] Error: " << e.what() << std::endl;
        server_thread.detach();
        throw;
    }

    // Wait a bit for final messages to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::cout << "  Test completed, stopping server..." << std::endl;

    // Note: In a real implementation, we'd send a shutdown message
    // For now, server will close when client disconnects or we can just detach
    server_thread.detach();

    std::cout << "  PASSED" << std::endl;
}

void test_network_mode_random_workload() {
    std::cout << "Test: Network mode with random workload" << std::endl;

    const int port = 12346;  // Different port
    const size_t num_blocks = 32;
    const size_t block_size = 128;
    const size_t Z = 4;
    const size_t num_operations = 50;

    auto params = oram::OramParams::compute(num_blocks, block_size, Z);
    auto key = oram::AeadKey::generate();
    oram::Aead aead(key);
    oram::BucketCodec codec(aead, block_size, Z);

    std::cout << "  Starting server..." << std::endl;
    std::thread server_thread([&]() {
        run_server(port, params.num_buckets, codec.encrypted_bucket_size(), key, block_size, Z);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    try {
        NetIO client_io("127.0.0.1", port);

        oram::PathOram oram;
        oram::OramConfig config;
        config.num_blocks = num_blocks;
        config.block_size = block_size;
        config.Z = Z;
        config.use_local_server = false;
        config.network = &client_io;
        config.key = key;
        config.rng = std::make_shared<oram::DeterministicOramRng>(54321);

        oram.init(config);

        std::cout << "  Running " << num_operations << " random operations..." << std::endl;

        std::mt19937 rng(12345);
        std::uniform_int_distribution<oram::BlockId> block_dist(0, num_blocks - 1);
        std::uniform_int_distribution<int> op_dist(0, 1);
        std::uniform_int_distribution<int> byte_dist(0, 255);

        // Track what we wrote for verification
        std::map<oram::BlockId, std::vector<oram::Byte>> written_data;

        for (size_t op = 0; op < num_operations; op++) {
            oram::BlockId block_id = block_dist(rng);
            bool is_write = op_dist(rng) == 1;

            if (is_write) {
                std::vector<oram::Byte> data(block_size);
                for (auto& b : data) {
                    b = static_cast<oram::Byte>(byte_dist(rng));
                }
                oram.write(block_id, data);
                written_data[block_id] = data;
            } else {
                auto data = oram.read(block_id);

                // Verify data matches what we wrote (or zeros if never written)
                auto it = written_data.find(block_id);
                if (it != written_data.end()) {
                    assert(data == it->second);
                } else {
                    // Should be all zeros
                    for (auto byte : data) {
                        assert(byte == 0);
                    }
                }
            }

            if ((op + 1) % 10 == 0) {
                std::cout << "    Completed " << (op + 1) << " operations" << std::endl;
            }
        }

        std::cout << "  All " << num_operations << " operations verified!" << std::endl;
        std::cout << "  Max stash size: " << oram.stash_size() << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "[Client] Error: " << e.what() << std::endl;
        server_thread.detach();
        throw;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    server_thread.detach();

    std::cout << "  PASSED" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Path ORAM - Network Mode Tests" << std::endl;
    std::cout << "========================================" << std::endl << std::endl;

    try {
        test_network_mode();
        std::cout << std::endl;
        test_network_mode_random_workload();

        std::cout << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "All network tests passed!" << std::endl;
        std::cout << "========================================" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << std::endl << "Network test failed: " << e.what() << std::endl;
        return 1;
    }
}
