#pragma once

#include "common.h"
#include <unordered_map>
#include <vector>

namespace oram {

/**
 * Stash: client-side storage for blocks that couldn't fit in the tree.
 *
 * The stash holds blocks that have been read from the tree but haven't
 * been written back yet, plus blocks that couldn't fit during eviction.
 * ORAM correctness requires that stash overflow probability is negligible.
 */
class Stash {
public:
    Stash() = default;

    /**
     * Insert or update a block in the stash.
     * If a block with the same ID exists, it is replaced.
     * @param block The block to insert/update
     */
    void insert(Block block) {
        if (block.is_dummy()) return;  // Never store dummies
        blocks_[block.block_id] = std::move(block);
    }

    /**
     * Insert or update a block with explicit parameters.
     * @param block_id The logical block address
     * @param leaf The assigned leaf
     * @param data The block data
     */
    void insert(BlockId block_id, LeafId leaf, std::vector<Byte> data) {
        if (block_id == INVALID_BLOCK_ID) return;
        blocks_[block_id] = Block(block_id, leaf, std::move(data));
    }

    /**
     * Update the data of an existing block, or insert if not present.
     * @param block_id The logical block address
     * @param leaf The new assigned leaf
     * @param data The new block data
     */
    void update(BlockId block_id, LeafId leaf, std::vector<Byte> data) {
        if (block_id == INVALID_BLOCK_ID) return;
        blocks_[block_id] = Block(block_id, leaf, std::move(data));
    }

    /**
     * Lookup a block by ID.
     * @param block_id The logical block address
     * @return Pointer to the block if found, nullptr otherwise
     */
    Block* find(BlockId block_id) {
        auto it = blocks_.find(block_id);
        if (it != blocks_.end()) {
            return &it->second;
        }
        return nullptr;
    }

    const Block* find(BlockId block_id) const {
        auto it = blocks_.find(block_id);
        if (it != blocks_.end()) {
            return &it->second;
        }
        return nullptr;
    }

    /**
     * Remove a block from the stash.
     * @param block_id The logical block address
     * @return true if the block was found and removed
     */
    bool remove(BlockId block_id) {
        return blocks_.erase(block_id) > 0;
    }

    /**
     * Remove and return a block from the stash.
     * @param block_id The logical block address
     * @return The block if found, nullopt otherwise
     */
    std::optional<Block> take(BlockId block_id) {
        auto it = blocks_.find(block_id);
        if (it != blocks_.end()) {
            Block b = std::move(it->second);
            blocks_.erase(it);
            return b;
        }
        return std::nullopt;
    }

    /**
     * Check if a block is in the stash.
     * @param block_id The logical block address
     * @return true if the block is in the stash
     */
    bool contains(BlockId block_id) const {
        return blocks_.find(block_id) != blocks_.end();
    }

    /**
     * Get all blocks currently in the stash.
     * Used during writeback to select blocks for eviction.
     * @return Vector of pointers to all blocks
     */
    std::vector<Block*> all_blocks() {
        std::vector<Block*> result;
        result.reserve(blocks_.size());
        for (auto& [id, block] : blocks_) {
            result.push_back(&block);
        }
        return result;
    }

    /**
     * Get all blocks (const version).
     */
    std::vector<const Block*> all_blocks() const {
        std::vector<const Block*> result;
        result.reserve(blocks_.size());
        for (const auto& [id, block] : blocks_) {
            result.push_back(&block);
        }
        return result;
    }

    /**
     * Number of blocks in the stash.
     */
    size_t size() const { return blocks_.size(); }

    /**
     * Check if stash is empty.
     */
    bool empty() const { return blocks_.empty(); }

    /**
     * Clear all blocks from the stash.
     */
    void clear() { blocks_.clear(); }

    /**
     * Merge another stash into this one.
     * @param other The stash to merge from
     */
    void merge(Stash&& other) {
        for (auto& [id, block] : other.blocks_) {
            blocks_[id] = std::move(block);
        }
        other.blocks_.clear();
    }

    /**
     * Insert multiple blocks.
     * @param blocks Vector of blocks to insert
     */
    void insert_all(std::vector<Block>&& blocks) {
        for (auto& block : blocks) {
            if (!block.is_dummy()) {
                blocks_[block.block_id] = std::move(block);
            }
        }
    }

private:
    std::unordered_map<BlockId, Block> blocks_;
};

} // namespace oram
