#include "oram/path_oram.h"
#include <algorithm>
#include <stdexcept>

namespace oram {

PathOram::PathOram() = default;
PathOram::~PathOram() = default;

void PathOram::init(const OramConfig& config) {
    // Validate configuration
    if (config.num_blocks == 0) {
        throw std::invalid_argument("num_blocks must be > 0");
    }
    if (config.block_size == 0) {
        throw std::invalid_argument("block_size must be > 0");
    }
    if (config.Z < 4) {
        throw std::invalid_argument("Z must be >= 4 for Path ORAM security");
    }

    // Compute parameters
    params_ = OramParams::compute(config.num_blocks, config.block_size, config.Z);

    // Set up RNG
    if (config.rng) {
        rng_ = config.rng;
    } else {
        rng_ = std::make_shared<SecureOramRng>();
    }

    // Set up encryption
    AeadKey key = config.key.value_or(AeadKey::generate());
    aead_ = std::make_unique<Aead>(key);

    // Set up bucket codec
    codec_ = std::make_unique<BucketCodec>(*aead_, params_.block_size, params_.Z);

    // Set up position map
    position_map_ = std::make_unique<PositionMap>(
        params_.num_leaves,
        [this]() { return rng_->random_leaf(params_.num_leaves); }
    );

    // Clear stash
    stash_.clear();

    // Set up server connection
    if (config.use_local_server) {
        // Create local in-process server
        local_server_ = std::make_unique<OramServer>(
            params_.num_buckets,
            codec_->encrypted_bucket_size()
        );

        // Initialize with encrypted dummy buckets
        local_server_->init([this](NodeId) {
            return generate_initial_bucket();
        });

        remote_client_ = nullptr;
    } else if (config.network) {
        // Use remote server
        remote_client_ = std::make_unique<OramClient>(
            *config.network,
            codec_->encrypted_bucket_size(),
            params_.tree_depth
        );
        remote_client_->init(params_.num_buckets, codec_->encrypted_bucket_size());
        local_server_ = nullptr;
    } else {
        throw std::invalid_argument("Must specify either use_local_server=true or provide network");
    }
}

std::vector<Byte> PathOram::read(BlockId block_id) {
    return access(block_id, std::nullopt);
}

void PathOram::write(BlockId block_id, std::span<const Byte> data) {
    access(block_id, data);
}

std::vector<Byte> PathOram::access(BlockId block_id, std::optional<std::span<const Byte>> data) {
    if (data.has_value()) {
        return path_oram_access(Operation::Write, block_id, data.value());
    } else {
        return path_oram_access(Operation::Read, block_id, {});
    }
}

std::vector<Byte> PathOram::path_oram_access(Operation op, BlockId block_id,
                                              std::span<const Byte> new_data) {
    // Validate block_id
    if (block_id >= params_.num_blocks) {
        throw std::out_of_range("Block ID out of range");
    }

    // Validate write data size
    if (op == Operation::Write && new_data.size() != params_.block_size) {
        throw std::invalid_argument("Write data size must match block_size");
    }

    // Step 1: Remap position
    LeafId x_old = position_map_->get(block_id);
    LeafId x_new = position_map_->random_leaf();
    position_map_->set(block_id, x_new);

    // Step 2: Read full path into stash
    read_path_into_stash(x_old);

    // Step 3: Serve request from stash
    Block* block_ptr = stash_.find(block_id);
    std::vector<Byte> data_old;

    if (block_ptr) {
        // Block found in stash
        data_old = block_ptr->data;
    } else {
        // Block not found - return zeros (never written before)
        data_old.resize(params_.block_size, 0);
    }

    if (op == Operation::Write) {
        // Update block in stash with new data and new leaf
        stash_.update(block_id, x_new, std::vector<Byte>(new_data.begin(), new_data.end()));
    } else {
        // For read, update the leaf assignment in stash
        if (block_ptr) {
            block_ptr->leaf = x_new;
        } else {
            // Block was never written, create it with zeros
            stash_.insert(block_id, x_new, std::vector<Byte>(params_.block_size, 0));
        }
    }

    // Step 4: Evict path (greedy deep-first)
    evict_path(x_old);

    return data_old;
}

void PathOram::read_path_into_stash(LeafId leaf_id) {
    // Read all encrypted buckets on the path from root to leaf
    auto encrypted_buckets = server_read_path(leaf_id);

    // Process each bucket
    for (size_t level = 0; level <= params_.tree_depth; level++) {
        // Decrypt bucket
        Bucket bucket = codec_->decode(encrypted_buckets[level]);

        // Insert all real blocks into stash
        for (const auto& block : bucket.slots) {
            if (!block.is_dummy()) {
                stash_.insert(Block(block.block_id, block.leaf, block.data));
            }
        }
    }
}

void PathOram::evict_path(LeafId leaf_id) {
    std::vector<std::vector<Byte>> encrypted_buckets;
    encrypted_buckets.reserve(params_.tree_depth + 1);

    // Greedy deep-first: process from leaf (level L) to root (level 0)
    for (int level = static_cast<int>(params_.tree_depth); level >= 0; level--) {
        NodeId node_id = TreeUtil::node_on_path(leaf_id, static_cast<size_t>(level), params_.tree_depth);

        // Select up to Z blocks from stash that can be placed at this node
        std::vector<Block> selected = select_blocks_for_node(node_id, params_.Z);

        // Create bucket with selected blocks (automatically pads with dummies)
        Bucket bucket;
        bucket.slots = std::move(selected);
        bucket.pad_to(params_.Z, params_.block_size);

        // Encrypt and store
        encrypted_buckets.push_back(codec_->encode(bucket));
    }

    // Reverse to get root-to-leaf order for server_write_path
    std::reverse(encrypted_buckets.begin(), encrypted_buckets.end());

    // Write all buckets to server
    server_write_path(leaf_id, encrypted_buckets);
}

std::vector<Block> PathOram::select_blocks_for_node(NodeId node_id, size_t max_blocks) {
    std::vector<Block> selected;
    std::vector<BlockId> to_remove;

    // Find all blocks in stash that can be placed at this node
    // A block can be placed at node_id if node_id is on the path to the block's assigned leaf
    for (auto* block_ptr : stash_.all_blocks()) {
        if (TreeUtil::can_place_at_node(block_ptr->leaf, node_id, params_.tree_depth)) {
            selected.push_back(*block_ptr);
            to_remove.push_back(block_ptr->block_id);

            if (selected.size() >= max_blocks) {
                break;
            }
        }
    }

    // Remove selected blocks from stash
    for (BlockId id : to_remove) {
        stash_.remove(id);
    }

    return selected;
}

void PathOram::check_invariants() const {
    // For testing/debugging only - checks all ORAM invariants
    // Only practical for small instances

    std::unordered_map<BlockId, size_t> block_counts;  // Count occurrences of each block
    std::unordered_map<BlockId, NodeId> block_locations;  // Where each block is found

    // Check stash
    for (const auto* block : stash_.all_blocks()) {
        block_counts[block->block_id]++;
        block_locations[block->block_id] = INVALID_NODE_ID;  // Special marker for stash
    }

    // Check all buckets in the tree
    for (NodeId node_id = 0; node_id < params_.num_buckets; node_id++) {
        // Read and decrypt bucket
        auto encrypted = server_read_bucket(node_id);
        Bucket bucket = codec_->decode(encrypted);

        // Check each slot
        for (const auto& block : bucket.slots) {
            if (!block.is_dummy()) {
                block_counts[block.block_id]++;

                // Verify path constraint: block can only be in a bucket on the path to its leaf
                if (!TreeUtil::can_place_at_node(block.leaf, node_id, params_.tree_depth)) {
                    throw std::runtime_error(
                        "Invariant violation: Block " + std::to_string(block.block_id) +
                        " with leaf " + std::to_string(block.leaf) +
                        " found in node " + std::to_string(node_id) +
                        " which is not on its path"
                    );
                }

                if (block_counts[block.block_id] > 1) {
                    throw std::runtime_error(
                        "Invariant violation: Block " + std::to_string(block.block_id) +
                        " appears multiple times (found in stash=" +
                        std::to_string(block_locations[block.block_id] == INVALID_NODE_ID) +
                        ", node " + std::to_string(node_id) + ")"
                    );
                }

                block_locations[block.block_id] = node_id;
            }
        }
    }

    // Verify uniqueness: each block appears at most once
    for (const auto& [block_id, count] : block_counts) {
        if (count > 1) {
            throw std::runtime_error(
                "Invariant violation: Block " + std::to_string(block_id) +
                " appears " + std::to_string(count) + " times"
            );
        }
    }

    // Verify position map consistency
    // For each block in stash or tree, its position map entry should point to a valid leaf
    for (const auto& [block_id, node_id] : block_locations) {
        if (!position_map_->contains(block_id)) {
            throw std::runtime_error(
                "Invariant violation: Block " + std::to_string(block_id) +
                " exists but has no position map entry"
            );
        }

        LeafId mapped_leaf = position_map_->get(block_id);
        if (mapped_leaf >= params_.num_leaves) {
            throw std::runtime_error(
                "Invariant violation: Block " + std::to_string(block_id) +
                " has invalid leaf " + std::to_string(mapped_leaf)
            );
        }
    }
}

} // namespace oram
