#pragma once

#include "oram.h"

namespace oram {

/**
 * Path ORAM implementation.
 *
 * Implements the Path ORAM algorithm by Stefanov et al. (2013).
 *
 * Key properties:
 * - Binary tree of buckets, each with Z slots
 * - On each access: read entire path, update in stash, write back entire path
 * - Greedy deep-first eviction during writeback
 * - Block can only be placed on path to its assigned leaf
 *
 * Security guarantees:
 * - Access pattern reveals only which path was accessed (not block ID)
 * - All buckets encrypted with AEAD
 * - Fresh randomness on every bucket write
 */
class PathOram : public Oram {
public:
    PathOram();
    ~PathOram() override;

    /**
     * Initialize Path ORAM.
     *
     * @param config Configuration (num_blocks, block_size, Z)
     *               Z should be >= 4 for security
     */
    void init(const OramConfig& config) override;

    /**
     * Read a block.
     *
     * @param block_id The logical block address
     * @return The block data (or zeros if never written)
     */
    std::vector<Byte> read(BlockId block_id) override;

    /**
     * Write a block.
     *
     * @param block_id The logical block address
     * @param data The data to write (must be exactly block_size bytes)
     */
    void write(BlockId block_id, std::span<const Byte> data) override;

    /**
     * Combined read-write access.
     *
     * This is the core ORAM operation. Both read and write are implemented
     * as special cases of this function.
     *
     * @param block_id The logical block address
     * @param data If provided, write this data; otherwise read
     * @return The old data (for write) or current data (for read)
     */
    std::vector<Byte> access(BlockId block_id, std::optional<std::span<const Byte>> data) override;

    const OramParams& params() const override { return params_; }
    OramType type() const override { return OramType::PathOram; }
    size_t stash_size() const override { return stash_.size(); }

    /**
     * Check Path ORAM invariants.
     *
     * Verifies:
     * 1. Each block appears exactly once (in stash or in exactly one bucket)
     * 2. Blocks in buckets satisfy path constraint
     * 3. Position map consistency
     *
     * Only practical for small instances (used in testing).
     */
    void check_invariants() const override;

private:
    /**
     * Core access operation following the pseudocode.
     *
     * 1. Remap: x_old = PositionMap[a], PositionMap[a] = random leaf
     * 2. Read path: for all levels, read bucket into stash
     * 3. Serve request from stash
     * 4. Write back path: greedy deep-first eviction
     *
     * @param op Operation type (Read or Write)
     * @param block_id Block address
     * @param new_data New data for write (ignored for read)
     * @return Old/current data
     */
    std::vector<Byte> path_oram_access(Operation op, BlockId block_id,
                                       std::span<const Byte> new_data);

    /**
     * Read all buckets on path to leaf into stash.
     *
     * @param leaf_id The leaf to read path to
     */
    void read_path_into_stash(LeafId leaf_id);

    /**
     * Evict path with greedy deep-first eviction.
     *
     * For each level from leaf to root:
     * - Find stash blocks that can be placed at this level
     * - Select up to Z blocks
     * - Write encrypted bucket with selected blocks + dummies
     *
     * This is the standard "evict" operation from Path ORAM literature.
     *
     * @param leaf_id The leaf of the path being evicted
     */
    void evict_path(LeafId leaf_id);

    /**
     * Select blocks from stash that can be placed at a given node.
     *
     * A block can be placed at node N if N is on the path to the block's
     * assigned leaf.
     *
     * @param node_id The node to place blocks at
     * @param max_blocks Maximum number of blocks to select (Z)
     * @return Vector of selected blocks (removed from stash)
     */
    std::vector<Block> select_blocks_for_node(NodeId node_id, size_t max_blocks);
};

} // namespace oram
