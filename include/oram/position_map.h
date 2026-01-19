#pragma once

#include "common.h"
#include <unordered_map>
#include <random>
#include <functional>

namespace oram {

/**
 * Position map: maps each block_id to its assigned leaf.
 *
 * This is a core client-side data structure that tracks where each logical
 * block is positioned in the ORAM tree. Each block is mapped to exactly one
 * leaf, and the mapping is updated on every access.
 */
class PositionMap {
public:
    /**
     * Construct a position map.
     * @param rng Random number generator for leaf assignment
     */
    explicit PositionMap(std::function<LeafId()> rng)
        : rng_(std::move(rng)) {}

    /**
     * Get the current leaf assignment for a block.
     * If the block has no entry yet, assigns a random leaf.
     * @param block_id The logical block address
     * @return The assigned leaf ID
     */
    LeafId get(BlockId block_id) {
        auto it = map_.find(block_id);
        if (it == map_.end()) {
            // First access to this block - assign random leaf
            LeafId leaf = rng_();
            map_[block_id] = leaf;
            return leaf;
        }
        return it->second;
    }

    /**
     * Set the leaf assignment for a block.
     * @param block_id The logical block address
     * @param leaf The new leaf assignment
     */
    void set(BlockId block_id, LeafId leaf) {
        map_[block_id] = leaf;
    }

    /**
     * Check if a block has an entry in the position map.
     * @param block_id The logical block address
     * @return true if the block has an entry
     */
    bool contains(BlockId block_id) const {
        return map_.find(block_id) != map_.end();
    }

    /**
     * Get a new random leaf.
     * Used when remapping a block during access.
     * @return A uniformly random leaf ID
     */
    LeafId random_leaf() {
        return rng_();
    }

    /**
     * Number of entries in the position map.
     */
    size_t size() const { return map_.size(); }

    /**
     * Clear all entries.
     */
    void clear() { map_.clear(); }

private:
    std::unordered_map<BlockId, LeafId> map_;
    std::function<LeafId()> rng_;
};

} // namespace oram
