/**
 * Simple test to verify the library compiles and basic types work.
 * Full testing will be implemented in Step 4.
 */

#include <oram/oram_lib.h>
#include <iostream>
#include <cassert>

void test_tree_util() {
    std::cout << "Testing TreeUtil..." << std::endl;

    // Test path computation for a tree with depth 2 (4 leaves)
    // Tree structure:
    //          0           (level 0, root)
    //        /   \
    //       1     2        (level 1)
    //      / \   / \
    //     3   4 5   6      (level 2, leaves)
    //
    // Leaf IDs: 0=node3, 1=node4, 2=node5, 3=node6

    size_t depth = 2;

    // Path to leaf 0 (node 3): 0 -> 1 -> 3
    assert(oram::TreeUtil::node_on_path(0, 0, depth) == 0);
    assert(oram::TreeUtil::node_on_path(0, 1, depth) == 1);
    assert(oram::TreeUtil::node_on_path(0, 2, depth) == 3);

    // Path to leaf 1 (node 4): 0 -> 1 -> 4
    assert(oram::TreeUtil::node_on_path(1, 0, depth) == 0);
    assert(oram::TreeUtil::node_on_path(1, 1, depth) == 1);
    assert(oram::TreeUtil::node_on_path(1, 2, depth) == 4);

    // Path to leaf 2 (node 5): 0 -> 2 -> 5
    assert(oram::TreeUtil::node_on_path(2, 0, depth) == 0);
    assert(oram::TreeUtil::node_on_path(2, 1, depth) == 2);
    assert(oram::TreeUtil::node_on_path(2, 2, depth) == 5);

    // Path to leaf 3 (node 6): 0 -> 2 -> 6
    assert(oram::TreeUtil::node_on_path(3, 0, depth) == 0);
    assert(oram::TreeUtil::node_on_path(3, 1, depth) == 2);
    assert(oram::TreeUtil::node_on_path(3, 2, depth) == 6);

    // Test leaf_in_subtree
    assert(oram::TreeUtil::leaf_in_subtree(0, 0, depth));  // leaf 0 in root
    assert(oram::TreeUtil::leaf_in_subtree(0, 1, depth));  // leaf 0 in node 1
    assert(!oram::TreeUtil::leaf_in_subtree(0, 2, depth)); // leaf 0 NOT in node 2
    assert(oram::TreeUtil::leaf_in_subtree(3, 2, depth));  // leaf 3 in node 2

    std::cout << "TreeUtil tests passed!" << std::endl;
}

void test_crypto() {
    std::cout << "Testing AEAD encryption..." << std::endl;

    // Generate key
    auto key = oram::AeadKey::generate();
    oram::Aead aead(key);

    // Test data
    std::vector<oram::Byte> plaintext = {0x48, 0x65, 0x6c, 0x6c, 0x6f};  // "Hello"

    // Encrypt
    auto ciphertext = aead.encrypt(plaintext);

    // Verify ciphertext size
    assert(ciphertext.size() == oram::Aead::ciphertext_size(plaintext.size()));

    // Decrypt
    auto decrypted = aead.decrypt(ciphertext);

    // Verify roundtrip
    assert(decrypted.size() == plaintext.size());
    assert(decrypted == plaintext);

    // Test tampering detection
    bool tamper_detected = false;
    ciphertext[ciphertext.size() / 2] ^= 0xFF;  // Flip some bits
    try {
        aead.decrypt(ciphertext);
    } catch (const std::runtime_error& e) {
        tamper_detected = true;
    }
    assert(tamper_detected);

    std::cout << "AEAD tests passed!" << std::endl;
}

void test_position_map() {
    std::cout << "Testing PositionMap..." << std::endl;

    size_t num_leaves = 8;
    uint64_t counter = 0;
    auto rng = [&counter, num_leaves]() -> oram::LeafId {
        return counter++ % num_leaves;
    };

    oram::PositionMap pm(rng);

    // First access should assign leaf 0
    assert(pm.get(42) == 0);

    // Same block should return same leaf
    assert(pm.get(42) == 0);

    // Different block gets next leaf
    assert(pm.get(100) == 1);

    // Set explicit leaf
    pm.set(42, 5);
    assert(pm.get(42) == 5);

    std::cout << "PositionMap tests passed!" << std::endl;
}

void test_stash() {
    std::cout << "Testing Stash..." << std::endl;

    oram::Stash stash;

    // Insert block
    oram::Block b1(10, 5, std::vector<oram::Byte>{1, 2, 3});
    stash.insert(b1);

    assert(stash.size() == 1);
    assert(stash.contains(10));
    assert(!stash.contains(11));

    // Find block
    auto* found = stash.find(10);
    assert(found != nullptr);
    assert(found->block_id == 10);
    assert(found->leaf == 5);

    // Take block
    auto taken = stash.take(10);
    assert(taken.has_value());
    assert(taken->block_id == 10);
    assert(stash.size() == 0);

    // Dummies should not be stored
    oram::Block dummy = oram::Block::dummy(4);
    stash.insert(dummy);
    assert(stash.size() == 0);

    std::cout << "Stash tests passed!" << std::endl;
}

void test_bucket_codec() {
    std::cout << "Testing BucketCodec..." << std::endl;

    auto key = oram::AeadKey::generate();
    oram::Aead aead(key);
    size_t block_size = 32;
    size_t slots_per_bucket = 4;

    oram::BucketCodec codec(aead, block_size, slots_per_bucket);

    // Create a bucket with some blocks
    oram::Bucket bucket;
    bucket.slots.push_back(oram::Block(1, 10, std::vector<oram::Byte>(block_size, 0xAA)));
    bucket.slots.push_back(oram::Block(2, 20, std::vector<oram::Byte>(block_size, 0xBB)));
    bucket.slots.push_back(oram::Block::dummy(block_size));
    bucket.slots.push_back(oram::Block::dummy(block_size));

    // Encode
    auto encrypted = codec.encode(bucket);
    assert(encrypted.size() == codec.encrypted_bucket_size());

    // Decode
    auto decoded = codec.decode(encrypted);
    assert(decoded.slots.size() == slots_per_bucket);

    // Verify first block
    assert(decoded.slots[0].block_id == 1);
    assert(decoded.slots[0].leaf == 10);
    assert(decoded.slots[0].data[0] == 0xAA);

    // Verify second block
    assert(decoded.slots[1].block_id == 2);
    assert(decoded.slots[1].leaf == 20);
    assert(decoded.slots[1].data[0] == 0xBB);

    // Verify dummies
    assert(decoded.slots[2].is_dummy());
    assert(decoded.slots[3].is_dummy());

    std::cout << "BucketCodec tests passed!" << std::endl;
}

void test_oram_params() {
    std::cout << "Testing OramParams..." << std::endl;

    // 8 blocks with Z=4
    auto params = oram::OramParams::compute(8, 1024, 4);

    assert(params.num_blocks == 8);
    assert(params.block_size == 1024);
    assert(params.Z == 4);
    assert(params.tree_depth == 3);   // 2^3 = 8 leaves
    assert(params.num_leaves == 8);
    assert(params.num_buckets == 15); // 2^4 - 1

    // 100 blocks
    params = oram::OramParams::compute(100, 4096, 4);
    assert(params.tree_depth == 7);   // 2^7 = 128 >= 100
    assert(params.num_leaves == 128);
    assert(params.num_buckets == 255); // 2^8 - 1

    std::cout << "OramParams tests passed!" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "ORAM Library - Step 1 Verification Tests" << std::endl;
    std::cout << "========================================" << std::endl;

    try {
        test_tree_util();
        test_crypto();
        test_position_map();
        test_stash();
        test_bucket_codec();
        test_oram_params();

        std::cout << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "All Step 1 tests passed!" << std::endl;
        std::cout << "========================================" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << std::endl;
        return 1;
    }
}
