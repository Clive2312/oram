#pragma once

#include <cstdint>
#include <cstddef>
#include <stdexcept>
#include <vector>
#include <array>
#include <optional>
#include <memory>
#include <span>
#include <cassert>
#include <cstring>

namespace oram {

// Macro for unimplemented methods (to be implemented in later steps)
#define ORAM_UNIMPLEMENTED() \
    throw std::logic_error("ORAM method not yet implemented: " + std::string(__FUNCTION__))

// Type aliases for clarity
using BlockId = uint64_t;
using LeafId = uint64_t;
using NodeId = uint64_t;
using Byte = uint8_t;

// Special sentinel values
constexpr BlockId INVALID_BLOCK_ID = UINT64_MAX;
constexpr LeafId INVALID_LEAF_ID = UINT64_MAX;
constexpr NodeId INVALID_NODE_ID = UINT64_MAX;

// ORAM algorithm type
enum class OramType {
    PathOram,
    RingOram
};

// Operation type for ORAM access
enum class Operation {
    Read,
    Write
};

/**
 * ORAM configuration parameters.
 * Shared between Path ORAM and Ring ORAM with some fields used only by one.
 */
struct OramParams {
    // Common parameters
    size_t num_blocks;       // Total number of logical blocks (N)
    size_t block_size;       // Size of each block in bytes (B)
    size_t tree_depth;       // Depth of the ORAM tree (L), leaves are at level L
    size_t num_leaves;       // Number of leaves = 2^L
    size_t num_buckets;      // Total number of buckets in the tree = 2^(L+1) - 1

    // Path ORAM parameters
    size_t Z;                // Bucket capacity (slots per bucket), typically 4 for Path ORAM

    // Ring ORAM parameters (used only by Ring ORAM)
    size_t S;                // Extra dummy slots per bucket for Ring ORAM
    size_t A;                // Eviction period for Ring ORAM

    /**
     * Compute tree parameters from block count and bucket capacity.
     * @param n Number of logical blocks
     * @param b Block size in bytes
     * @param z Bucket capacity (Z)
     * @param s Extra dummy slots (S) for Ring ORAM, 0 for Path ORAM
     * @param a Eviction period (A) for Ring ORAM
     */
    static OramParams compute(size_t n, size_t b, size_t z, size_t s = 0, size_t a = 1) {
        OramParams p;
        p.num_blocks = n;
        p.block_size = b;
        p.Z = z;
        p.S = s;
        p.A = a;

        // Compute tree depth L such that num_leaves >= num_blocks
        // num_leaves = 2^L
        p.tree_depth = 0;
        size_t leaves = 1;
        while (leaves < n) {
            p.tree_depth++;
            leaves *= 2;
        }
        p.num_leaves = leaves;
        p.num_buckets = (1ULL << (p.tree_depth + 1)) - 1;

        return p;
    }

    // Total slots per bucket (for Ring ORAM this is Z+S, for Path ORAM just Z)
    size_t slots_per_bucket(OramType type) const {
        return (type == OramType::RingOram) ? (Z + S) : Z;
    }
};

/**
 * A single ORAM block with metadata.
 * Holds the block ID, assigned leaf, and payload data.
 */
struct Block {
    BlockId block_id;          // Logical block address (INVALID_BLOCK_ID for dummy)
    LeafId leaf;               // Assigned leaf for position map
    std::vector<Byte> data;    // Payload

    Block() : block_id(INVALID_BLOCK_ID), leaf(INVALID_LEAF_ID) {}

    Block(BlockId id, LeafId l, size_t block_size)
        : block_id(id), leaf(l), data(block_size, 0) {}

    Block(BlockId id, LeafId l, std::vector<Byte> d)
        : block_id(id), leaf(l), data(std::move(d)) {}

    Block(BlockId id, LeafId l, std::span<const Byte> d)
        : block_id(id), leaf(l), data(d.begin(), d.end()) {}

    bool is_dummy() const { return block_id == INVALID_BLOCK_ID; }

    // Create a dummy block of given size
    static Block dummy(size_t block_size) {
        return Block(INVALID_BLOCK_ID, INVALID_LEAF_ID, block_size);
    }

    // Serialization size (excluding padding)
    size_t serialized_size() const {
        return sizeof(BlockId) + sizeof(LeafId) + data.size();
    }

    // Serialize block to buffer
    void serialize(std::span<Byte> out) const {
        assert(out.size() >= serialized_size());
        size_t offset = 0;
        std::memcpy(out.data() + offset, &block_id, sizeof(block_id));
        offset += sizeof(block_id);
        std::memcpy(out.data() + offset, &leaf, sizeof(leaf));
        offset += sizeof(leaf);
        std::memcpy(out.data() + offset, data.data(), data.size());
    }

    // Deserialize block from buffer
    static Block deserialize(std::span<const Byte> in, size_t block_size) {
        assert(in.size() >= sizeof(BlockId) + sizeof(LeafId) + block_size);
        Block b;
        size_t offset = 0;
        std::memcpy(&b.block_id, in.data() + offset, sizeof(b.block_id));
        offset += sizeof(b.block_id);
        std::memcpy(&b.leaf, in.data() + offset, sizeof(b.leaf));
        offset += sizeof(b.leaf);
        b.data.resize(block_size);
        std::memcpy(b.data.data(), in.data() + offset, block_size);
        return b;
    }
};

/**
 * A plaintext bucket containing Z (or Z+S for Ring ORAM) slots.
 * Each slot is either a real block or a dummy.
 */
struct Bucket {
    std::vector<Block> slots;

    Bucket() = default;

    explicit Bucket(size_t num_slots, size_t block_size) {
        slots.reserve(num_slots);
        for (size_t i = 0; i < num_slots; i++) {
            slots.push_back(Block::dummy(block_size));
        }
    }

    size_t size() const { return slots.size(); }

    // Pad bucket with dummies to reach target size
    void pad_to(size_t target_size, size_t block_size) {
        while (slots.size() < target_size) {
            slots.push_back(Block::dummy(block_size));
        }
    }
};

} // namespace oram
