#pragma once

#include "oram.h"
#include <bitset>

namespace oram {

/**
 * Ring ORAM bucket metadata.
 *
 * Each bucket maintains additional metadata for Ring ORAM's
 * selective read and early reshuffle mechanisms.
 *
 * Slots are organized as:
 * - First Z slots: may contain real blocks
 * - Last S slots: always contain dummies (used for dummy reads)
 *
 * The metadata tracks:
 * - count: number of times bucket has been touched since last reshuffle
 * - valids: bitset indicating which slots are still valid (not yet read)
 * - addrs[]: block IDs for the Z real-block slots (INVALID_BLOCK_ID if dummy)
 * - leaves[]: assigned leaves for blocks in real slots
 * - ptrs[]: permutation mapping real slots to physical positions
 */
struct RingBucketMetadata {
    // TODO: This is wrong, could be larger
    static constexpr size_t MAX_SLOTS = 64;  // Maximum Z+S

    uint32_t count;                          // Touch count since last reshuffle
    std::bitset<MAX_SLOTS> valids;           // Valid slots bitmap

    // Metadata for real-block slots (first Z slots conceptually)
    std::array<BlockId, MAX_SLOTS> addrs;    // Block IDs (INVALID_BLOCK_ID for dummy)
    std::array<LeafId, MAX_SLOTS> leaves;    // Assigned leaves
    std::array<uint8_t, MAX_SLOTS> ptrs;     // Physical slot indices

    RingBucketMetadata() : count(0) {
        addrs.fill(INVALID_BLOCK_ID);
        leaves.fill(INVALID_LEAF_ID);
        for (size_t i = 0; i < MAX_SLOTS; i++) {
            ptrs[i] = static_cast<uint8_t>(i);
        }
    }

    /**
     * Serialize metadata for storage.
     * Note: This metadata is stored in plaintext as per the Ring ORAM paper.
     */
    void serialize(std::span<Byte> out, size_t Z, size_t S) const;

    /**
     * Deserialize metadata from storage.
     */
    static RingBucketMetadata deserialize(std::span<const Byte> in, size_t Z, size_t S);

    /**
     * Compute serialized size.
     */
    static size_t serialized_size(size_t Z, size_t S);
};

/**
 * Ring ORAM implementation.
 *
 * Implements the Ring ORAM algorithm by Ren et al. (2015).
 *
 * Key differences from Path ORAM:
 * - Each bucket has Z+S slots (S extra dummy slots)
 * - ReadPath reads only ONE slot per bucket (not entire bucket)
 * - Uses per-bucket metadata (count, valids, permutation)
 * - Periodic EvictPath on a fixed schedule (every A accesses)
 * - Early reshuffle when bucket.count >= S
 *
 * This achieves better bandwidth but requires more complex bookkeeping.
 */
class RingOram : public Oram {
public:
    RingOram();
    ~RingOram() override;

    /**
     * Initialize Ring ORAM.
     *
     * @param config Configuration (num_blocks, block_size, Z, S, A)
     */
    void init(const OramConfig& config) override;

    std::vector<Byte> read(BlockId block_id) override;
    void write(BlockId block_id, std::span<const Byte> data) override;
    std::vector<Byte> access(Operation op, BlockId block_id, std::optional<std::span<const Byte>> data) override;

    const OramParams& params() const override { return params_; }
    OramType type() const override { return OramType::RingOram; }
    size_t stash_size() const override { return stash_.size(); }

    void check_invariants() const override;

private:
    // Ring ORAM state
    uint64_t round_;       // Access counter mod A (for eviction schedule)
    uint64_t evict_g_;     // Global eviction counter (determines eviction path)

    // Per-bucket metadata (client-side)
    std::vector<RingBucketMetadata> bucket_metadata_;

    /**
     * Core Ring ORAM access operation.
     *
     * 1. Remap position
     * 2. ReadPath (one slot per bucket)
     * 3. Serve from stash if not found on path
     * 4. Update stash with new/modified block
     * 5. Increment round, maybe EvictPath
     * 6. EarlyReshuffle
     */

    /**
     * Read path for Ring ORAM.
     *
     * Reads exactly ONE slot per bucket on the path.
     * - If target block is present and valid, read that slot
     * - Otherwise, read a valid dummy slot
     *
     * @param leaf_id Path to read
     * @param target_block_id Block we're looking for
     * @return Data if found, nullopt if block was in stash
     */
    std::optional<std::vector<Byte>> read_path(LeafId leaf_id, BlockId target_block_id);

    /**
     * Periodic eviction on a deterministic path.
     *
     * 1. Read all remaining real blocks from buckets on eviction path
     * 2. Write back path with blocks from stash (greedy deep-first)
     * 3. Reset bucket metadata
     */
    void evict_path();

    /**
     * Early reshuffle for buckets that are running low on valid slots.
     *
     * For each bucket on the accessed path:
     *   If count >= S, reshuffle that bucket.
     *
     * @param leaf_id The accessed path
     */
    void early_reshuffle(LeafId leaf_id);

    /**
     * Get the physical slot to read for a bucket.
     *
     * If target block is present and its slot is valid, return that slot.
     * Otherwise, return a random valid dummy slot.
     *
     * @param node_id The bucket's node ID
     * @param target_block_id The block we're looking for
     * @return Physical slot index to read
     */
    size_t get_block_offset(NodeId node_id, BlockId target_block_id);

    /**
     * Read a single slot from a bucket.
     *
     * @param node_id The bucket's node ID
     * @param slot_index Physical slot index
     * @return Decrypted block
     */
    Block read_slot(NodeId node_id, size_t slot_index);

    /**
     * Read all remaining valid real blocks from a bucket into stash.
     *
     * Used during eviction. Reads exactly Z slots (real + dummy padding).
     *
     * @param node_id The bucket's node ID
     */
    void read_bucket_into_stash(NodeId node_id);

    /**
     * Write a bucket with blocks from stash.
     *
     * Selects compatible blocks, creates fresh permutation,
     * encrypts and writes all Z+S slots.
     *
     * @param node_id The bucket's node ID
     */
    void write_bucket_from_stash(NodeId node_id);

    /**
     * Invalidate a slot in bucket metadata.
     */
    void invalidate_slot(NodeId node_id, size_t slot_index);

    /**
     * Find a random valid dummy slot in a bucket.
     */
    size_t random_valid_dummy_slot(NodeId node_id);
};

} // namespace oram
