#include "oram/ring_oram.h"
#include <algorithm>
#include <stdexcept>
#include <cstring>
#include <unordered_set>

namespace oram {

// ============================================================================
// RingBucketMetadata Implementation
// ============================================================================

void RingBucketMetadata::serialize(std::span<Byte> out, size_t Z, size_t S) const {
    size_t total_slots = Z + S;
    size_t expected_size = serialized_size(Z, S);

    if (out.size() < expected_size) {
        throw std::runtime_error("Output buffer too small for metadata");
    }

    Byte* ptr = out.data();

    // Count (4 bytes)
    std::memcpy(ptr, &count, sizeof(count));
    ptr += sizeof(count);

    // Valids bitmap (8 bytes for up to 64 slots)
    uint64_t valids_bits = valids.to_ullong();
    std::memcpy(ptr, &valids_bits, sizeof(valids_bits));
    ptr += sizeof(valids_bits);

    // Block IDs for Z real slots
    for (size_t i = 0; i < Z; i++) {
        std::memcpy(ptr, &addrs[i], sizeof(BlockId));
        ptr += sizeof(BlockId);
    }

    // Leaves for Z real slots
    for (size_t i = 0; i < Z; i++) {
        std::memcpy(ptr, &leaves[i], sizeof(LeafId));
        ptr += sizeof(LeafId);
    }

    // Ptrs for Z real slots
    for (size_t i = 0; i < Z; i++) {
        *ptr++ = ptrs[i];
    }
}

RingBucketMetadata RingBucketMetadata::deserialize(std::span<const Byte> in, size_t Z, size_t S) {
    size_t expected_size = serialized_size(Z, S);

    if (in.size() < expected_size) {
        throw std::runtime_error("Input buffer too small for metadata");
    }

    RingBucketMetadata meta;
    const Byte* ptr = in.data();

    // Count
    std::memcpy(&meta.count, ptr, sizeof(meta.count));
    ptr += sizeof(meta.count);

    // Valids bitmap
    uint64_t valids_bits;
    std::memcpy(&valids_bits, ptr, sizeof(valids_bits));
    meta.valids = std::bitset<MAX_SLOTS>(valids_bits);
    ptr += sizeof(valids_bits);

    // Block IDs
    for (size_t i = 0; i < Z; i++) {
        std::memcpy(&meta.addrs[i], ptr, sizeof(BlockId));
        ptr += sizeof(BlockId);
    }

    // Leaves
    for (size_t i = 0; i < Z; i++) {
        std::memcpy(&meta.leaves[i], ptr, sizeof(LeafId));
        ptr += sizeof(LeafId);
    }

    // Ptrs
    for (size_t i = 0; i < Z; i++) {
        meta.ptrs[i] = *ptr++;
    }

    return meta;
}

size_t RingBucketMetadata::serialized_size(size_t Z, size_t /* S */) {
    // count (4) + valids (8) + addrs (8*Z) + leaves (8*Z) + ptrs (Z)
    return 4 + 8 + Z * 8 + Z * 8 + Z;
}

// ============================================================================
// RingOram Implementation
// ============================================================================

RingOram::RingOram() : round_(0), evict_g_(0) {}
RingOram::~RingOram() = default;

void RingOram::init(const OramConfig& config) {
    // Validate configuration
    if (config.num_blocks == 0) {
        throw std::invalid_argument("num_blocks must be > 0");
    }
    if (config.block_size == 0) {
        throw std::invalid_argument("block_size must be > 0");
    }
    if (config.Z == 0) {
        throw std::invalid_argument("Z must be > 0 for Ring ORAM");
    }
    if (config.S == 0) {
        throw std::invalid_argument("S must be > 0 for Ring ORAM");
    }
    if (config.A == 0) {
        throw std::invalid_argument("A (eviction period) must be > 0");
    }
    if (config.Z + config.S > RingBucketMetadata::MAX_SLOTS) {
        throw std::invalid_argument("Z + S exceeds maximum slots");
    }

    // Compute parameters
    params_ = OramParams::compute(config.num_blocks, config.block_size,
                                  config.Z, config.S, config.A);

    // Set up RNG
    if (config.rng) {
        rng_ = config.rng;
    } else {
        rng_ = std::make_shared<SecureOramRng>();
    }

    // Set up encryption
    AeadKey key = config.key.value_or(AeadKey::generate());
    aead_ = std::make_unique<Aead>(key);

    // Set up bucket codec (Z+S slots for Ring ORAM)
    codec_ = std::make_unique<BucketCodec>(*aead_, params_.block_size, params_.Z + params_.S);

    // Set up position map
    position_map_ = std::make_unique<PositionMap>(
        params_.num_leaves,
        [this]() { return rng_->random_leaf(params_.num_leaves); }
    );

    // Clear stash
    stash_.clear();

    // Initialize round counters
    round_ = 0;
    evict_g_ = 0;

    // Initialize bucket metadata (client-side)
    bucket_metadata_.resize(params_.num_buckets);
    for (auto& meta : bucket_metadata_) {
        meta = RingBucketMetadata();
        // Mark all slots as valid initially
        for (size_t i = 0; i < params_.Z + params_.S; i++) {
            meta.valids.set(i);
        }
    }

    // Set up server connection
    // Ring ORAM uses slotwise encryption for individual slot access
    size_t encrypted_bucket_size_slotwise = codec_->encrypted_slot_size() * (params_.Z + params_.S);

    if (config.use_local_server) {
        local_server_ = std::make_unique<OramServer>(
            params_.num_buckets,
            encrypted_bucket_size_slotwise
        );

        // Initialize with encrypted dummy buckets (slotwise encoding)
        local_server_->init([this](NodeId) {
            Bucket bucket(params_.Z + params_.S, params_.block_size);
            return codec_->encode_bucket_slotwise(bucket);
        });

        remote_client_ = nullptr;
    } else if (config.network) {
        remote_client_ = std::make_unique<OramClient>(
            *config.network,
            encrypted_bucket_size_slotwise,
            params_.tree_depth
        );
        remote_client_->init(params_.num_buckets, encrypted_bucket_size_slotwise);
        local_server_ = nullptr;
    } else {
        throw std::invalid_argument("Must specify either use_local_server=true or provide network");
    }
}

std::vector<Byte> RingOram::read(BlockId block_id) {
    return access(block_id, std::nullopt);
}

void RingOram::write(BlockId block_id, std::span<const Byte> data) {
    access(block_id, data);
}

std::vector<Byte> RingOram::access(BlockId block_id, std::optional<std::span<const Byte>> data) {
    if (data.has_value()) {
        return ring_oram_access(Operation::Write, block_id, data.value());
    } else {
        return ring_oram_access(Operation::Read, block_id, {});
    }
}

std::vector<Byte> RingOram::ring_oram_access(Operation op, BlockId block_id,
                                              std::span<const Byte> new_data) {
    // Step 1: Remap position
    LeafId l_old = position_map_->get(block_id);
    LeafId l_new = position_map_->random_leaf();
    position_map_->set(block_id, l_new);

    // Step 2: Read path (reads exactly one slot per bucket)
    std::optional<std::vector<Byte>> data = read_path(l_old, block_id);

    // Step 3: If block not found on path, it must be in stash
    if (!data.has_value()) {
        Block* stash_block = stash_.find(block_id);
        if (stash_block) {
            data = stash_block->data;
            stash_.remove(block_id);  // Remove from stash since we're remapping it
        } else {
            // Block never written, return zeros
            data = std::vector<Byte>(params_.block_size, 0);
        }
    }

    std::vector<Byte> result_data = data.value();

    // Step 4: Handle write operation
    if (op == Operation::Write) {
        // Update data and place back in stash with new leaf assignment
        Block updated_block(block_id, l_new, new_data);
        stash_.insert(updated_block);
    } else {
        // For read, place block back in stash with new leaf assignment
        Block updated_block(block_id, l_new, result_data);
        stash_.insert(updated_block);
    }

    // Step 5: Increment round counter and maybe evict
    round_ = (round_ + 1) % params_.A;
    if (round_ == 0) {
        evict_path();
    }

    // Step 6: Early reshuffle
    early_reshuffle(l_old);

    return result_data;
}

std::optional<std::vector<Byte>> RingOram::read_path(LeafId leaf_id, BlockId target_block_id) {
    std::optional<std::vector<Byte>> found = std::nullopt;

    // Traverse path from root to leaf
    for (size_t level = 0; level <= params_.tree_depth; level++) {
        NodeId node_id = TreeUtil::node_on_path(leaf_id, level, params_.tree_depth);

        // Get physical slot to read (either target block or dummy)
        size_t slot_offset = get_block_offset(node_id, target_block_id);

        // Read this slot
        Block block = read_slot(node_id, slot_offset);

        // Invalidate the slot
        invalidate_slot(node_id, slot_offset);

        // Check if this is the target block
        if (block.block_id == target_block_id) {
            found = block.data;
        }

        // Increment bucket's touch count
        bucket_metadata_[node_id].count++;
    }

    return found;
}

void RingOram::evict_path() {
    // Determine eviction path using global counter G
    LeafId evict_leaf = evict_g_ % params_.num_leaves;
    evict_g_++;

    // Read phase: move remaining real blocks from buckets on path into stash
    for (size_t level = 0; level <= params_.tree_depth; level++) {
        NodeId node_id = TreeUtil::node_on_path(evict_leaf, level, params_.tree_depth);
        read_bucket_into_stash(node_id);
    }

    // Write phase: push stash blocks down (greedy deep-first) and reshuffle buckets
    for (int level = static_cast<int>(params_.tree_depth); level >= 0; level--) {
        NodeId node_id = TreeUtil::node_on_path(evict_leaf, level, params_.tree_depth);
        write_bucket_from_stash(node_id);
        // count is reset to 0 in write_bucket_from_stash
    }
}

void RingOram::early_reshuffle(LeafId leaf_id) {
    // Check each bucket on the accessed path
    for (size_t level = 0; level <= params_.tree_depth; level++) {
        NodeId node_id = TreeUtil::node_on_path(leaf_id, level, params_.tree_depth);

        // If bucket is running low on valid slots, reshuffle it
        if (bucket_metadata_[node_id].count >= params_.S) {
            // Read remaining real blocks into stash
            read_bucket_into_stash(node_id);

            // Write bucket back with stash blocks (reshuffles and resets count)
            write_bucket_from_stash(node_id);
        }
    }
}

size_t RingOram::get_block_offset(NodeId node_id, BlockId target_block_id) {
    const auto& meta = bucket_metadata_[node_id];

    // Search for target block in the Z real-block slots
    for (size_t j = 0; j < params_.Z; j++) {
        size_t physical_slot = meta.ptrs[j];
        if (meta.addrs[j] == target_block_id && meta.valids.test(physical_slot)) {
            // Found target block and it's still valid
            return physical_slot;
        }
    }

    // Target not found or not valid, return random valid dummy slot
    return random_valid_dummy_slot(node_id);
}

Block RingOram::read_slot(NodeId node_id, size_t slot_index) {
    // Read encrypted slot from server
    size_t encrypted_slot_size = codec_->encrypted_slot_size();
    std::vector<Byte> encrypted_slot;

    if (local_server_) {
        encrypted_slot = local_server_->read_slot(node_id, slot_index, encrypted_slot_size);
    } else if (remote_client_) {
        encrypted_slot = remote_client_->read_slot(node_id, slot_index, encrypted_slot_size);
    } else {
        throw std::runtime_error("No server connection");
    }

    // Decrypt and decode slot
    return codec_->decode_slot(encrypted_slot);
}

void RingOram::read_bucket_into_stash(NodeId node_id) {
    auto& meta = bucket_metadata_[node_id];
    size_t real_read = 0;

    // Read up to Z real slots
    for (size_t j = 0; j < params_.Z; j++) {
        size_t physical_slot = meta.ptrs[j];

        if (meta.valids.test(physical_slot)) {
            // Read this slot
            Block block = read_slot(node_id, physical_slot);
            real_read++;

            // If it's a real block (not dummy), add to stash
            if (meta.addrs[j] != INVALID_BLOCK_ID) {
                // Update block's leaf assignment from metadata
                block.block_id = meta.addrs[j];
                block.leaf = meta.leaves[j];
                stash_.insert(block);
            }

            // Invalidate this slot
            invalidate_slot(node_id, physical_slot);
        }
    }

    // Pad with dummy reads to reach exactly Z reads
    while (real_read < params_.Z) {
        size_t dummy_slot = random_valid_dummy_slot(node_id);
        read_slot(node_id, dummy_slot);  // Read but discard
        invalidate_slot(node_id, dummy_slot);
        real_read++;
    }
}

void RingOram::write_bucket_from_stash(NodeId node_id) {
    auto& meta = bucket_metadata_[node_id];

    // Determine which level this bucket is at
    size_t level = TreeUtil::level_of(node_id);

    // Select up to Z compatible blocks from stash
    // A block is compatible if it can be placed on this bucket's subtree
    std::vector<Block> selected;
    std::vector<BlockId> to_remove;

    for (Block* block_ptr : stash_.all_blocks()) {
        if (selected.size() >= params_.Z) break;

        // Check if block's assigned leaf passes through this node
        NodeId node_on_block_path = TreeUtil::node_on_path(block_ptr->leaf, level, params_.tree_depth);
        if (node_on_block_path == node_id) {
            selected.push_back(*block_ptr);
            to_remove.push_back(block_ptr->block_id);
        }
    }

    // Remove selected blocks from stash
    for (BlockId id : to_remove) {
        stash_.remove(id);
    }

    // Create fresh random permutation for physical slots
    std::vector<size_t> perm(params_.Z + params_.S);
    for (size_t i = 0; i < perm.size(); i++) {
        perm[i] = i;
    }
    // Fisher-Yates shuffle
    for (size_t i = perm.size() - 1; i > 0; i--) {
        size_t j = rng_->random_range(i + 1);
        std::swap(perm[i], perm[j]);
    }

    // Build bucket with Z+S slots
    Bucket bucket;
    bucket.slots.reserve(params_.Z + params_.S);

    // Place selected real blocks in first Z positions (conceptually)
    // Then place dummies to fill remaining positions
    for (size_t j = 0; j < params_.Z + params_.S; j++) {
        if (j < selected.size()) {
            // Real block
            bucket.slots.push_back(selected[j]);
        } else {
            // Dummy block
            bucket.slots.push_back(Block::dummy(params_.block_size));
        }
    }

    // Apply permutation: reorder slots according to perm
    Bucket permuted_bucket;
    permuted_bucket.slots.resize(params_.Z + params_.S);
    for (size_t i = 0; i < params_.Z + params_.S; i++) {
        permuted_bucket.slots[perm[i]] = std::move(bucket.slots[i]);
    }

    // Encrypt bucket (slotwise)
    std::vector<Byte> encrypted_bucket = codec_->encode_bucket_slotwise(permuted_bucket);

    // Write to server
    if (local_server_) {
        local_server_->write_bucket(node_id, encrypted_bucket);
    } else if (remote_client_) {
        remote_client_->write_bucket(node_id, encrypted_bucket);
    } else {
        throw std::runtime_error("No server connection");
    }

    // Update metadata
    // Store metadata for the Z real slots (before permutation)
    for (size_t j = 0; j < params_.Z; j++) {
        if (j < selected.size()) {
            meta.addrs[j] = selected[j].block_id;
            meta.leaves[j] = selected[j].leaf;
        } else {
            meta.addrs[j] = INVALID_BLOCK_ID;
            meta.leaves[j] = INVALID_LEAF_ID;
        }
        meta.ptrs[j] = static_cast<uint8_t>(perm[j]);
    }

    // Reset all slots to valid
    for (size_t i = 0; i < params_.Z + params_.S; i++) {
        meta.valids.set(i);
    }

    // Reset count
    meta.count = 0;
}

void RingOram::invalidate_slot(NodeId node_id, size_t slot_index) {
    // TODO: Implement in Step 3
    bucket_metadata_[node_id].valids.reset(slot_index);
}

size_t RingOram::random_valid_dummy_slot(NodeId node_id) {
    const auto& meta = bucket_metadata_[node_id];

    // Find which physical slots are occupied by real blocks
    std::unordered_set<size_t> real_block_slots;
    for (size_t j = 0; j < params_.Z; j++) {
        real_block_slots.insert(meta.ptrs[j]);
    }

    // Collect all valid dummy slots (physical slots NOT used by real blocks)
    std::vector<size_t> valid_dummies;
    for (size_t i = 0; i < params_.Z + params_.S; i++) {
        if (real_block_slots.find(i) == real_block_slots.end() && meta.valids.test(i)) {
            valid_dummies.push_back(i);
        }
    }

    if (valid_dummies.empty()) {
        throw std::runtime_error("No valid dummy slots available in bucket");
    }

    // Return random valid dummy slot
    size_t idx = rng_->random_range(valid_dummies.size());
    return valid_dummies[idx];
}

void RingOram::check_invariants() const {
    // This is only for testing small instances
    // Check basic invariants to ensure ORAM correctness

    // Invariant 1: Each block ID appears at most once across stash + all buckets
    std::unordered_map<BlockId, size_t> block_counts;

    // Count blocks in stash
    for (const Block* block_ptr : stash_.all_blocks()) {
        if (block_ptr->block_id != INVALID_BLOCK_ID) {
            block_counts[block_ptr->block_id]++;
        }
    }

    // Count blocks in buckets (using metadata)
    for (size_t node_id = 0; node_id < bucket_metadata_.size(); node_id++) {
        const auto& meta = bucket_metadata_[node_id];

        // Check the Z real-block slots
        for (size_t j = 0; j < params_.Z; j++) {
            if (meta.addrs[j] != INVALID_BLOCK_ID) {
                size_t physical_slot = meta.ptrs[j];

                // Only count if slot is still valid (not yet read)
                if (meta.valids.test(physical_slot)) {
                    block_counts[meta.addrs[j]]++;
                }
            }
        }
    }

    // Verify uniqueness
    for (const auto& [block_id, count] : block_counts) {
        if (count > 1) {
            throw std::runtime_error("Invariant violated: Block " +
                                   std::to_string(block_id) +
                                   " appears " + std::to_string(count) + " times");
        }
    }

    // Invariant 2: Position map consistency
    // Each block's assigned leaf should match what's in stash/buckets
    for (BlockId block_id = 0; block_id < params_.num_blocks; block_id++) {
        LeafId pm_leaf = position_map_->get(block_id);

        // Find block in stash
        Block* stash_block = const_cast<Stash&>(stash_).find(block_id);
        if (stash_block) {
            if (stash_block->leaf != pm_leaf) {
                throw std::runtime_error("Invariant violated: Position map mismatch for block " +
                                       std::to_string(block_id));
            }
        }

        // Find block in buckets (check metadata)
        for (size_t node_id = 0; node_id < bucket_metadata_.size(); node_id++) {
            const auto& meta = bucket_metadata_[node_id];
            for (size_t j = 0; j < params_.Z; j++) {
                if (meta.addrs[j] == block_id) {
                    size_t physical_slot = meta.ptrs[j];
                    if (meta.valids.test(physical_slot)) {
                        if (meta.leaves[j] != pm_leaf) {
                            throw std::runtime_error("Invariant violated: Bucket leaf mismatch for block " +
                                                   std::to_string(block_id));
                        }
                    }
                }
            }
        }
    }

    // Invariant 3: Bucket capacity
    // At most Z real blocks per bucket (not counting invalidated ones)
    for (size_t node_id = 0; node_id < bucket_metadata_.size(); node_id++) {
        const auto& meta = bucket_metadata_[node_id];
        size_t valid_real_blocks = 0;

        for (size_t j = 0; j < params_.Z; j++) {
            if (meta.addrs[j] != INVALID_BLOCK_ID) {
                size_t physical_slot = meta.ptrs[j];
                if (meta.valids.test(physical_slot)) {
                    valid_real_blocks++;
                }
            }
        }

        if (valid_real_blocks > params_.Z) {
            throw std::runtime_error("Invariant violated: Bucket " + std::to_string(node_id) +
                                   " has " + std::to_string(valid_real_blocks) + " real blocks (max " +
                                   std::to_string(params_.Z) + ")");
        }
    }
}

} // namespace oram
