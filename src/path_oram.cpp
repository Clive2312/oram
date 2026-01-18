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
    // TODO: Implement in Step 2
    // This is the core Path ORAM access algorithm from the pseudocode:
    //
    // 1. x_old := PositionMap[a]
    //    x_new := UniformRandomLeaf()
    //    PositionMap[a] := x_new
    //
    // 2. Read full path into stash
    //    FOR level = 0 .. L:
    //      node := NodeOnPath(x_old, level)
    //      bucket_slots := ReadBucket(node)
    //      FOR each slot in bucket_slots:
    //        IF slot is real:
    //          InsertOrUpdate(Stash, slot.addr, slot.leaf, slot.data)
    //
    // 3. Serve request from stash
    //    data_old := LookupOrDefault(Stash, a)
    //    IF op == WRITE:
    //      Update(Stash, a, x_new, data_new)
    //
    // 4. Write back path (greedy deep-first)
    //    FOR level = L .. 0:
    //      node := NodeOnPath(x_old, level)
    //      Cand := { b in Stash | NodeOnPath(b.leaf, level) == node }
    //      chosen := TakeUpToZ(Cand)
    //      RemoveFromStash(Stash, chosen)
    //      WriteBucket(node, chosen)
    //
    // 5. RETURN data_old

    ORAM_UNIMPLEMENTED();
}

void PathOram::read_path_into_stash(LeafId leaf_id) {
    // TODO: Implement in Step 2
    // Read all encrypted buckets on path from server,
    // decrypt each, and insert real blocks into stash
    ORAM_UNIMPLEMENTED();
}

void PathOram::write_back_path(LeafId leaf_id) {
    // TODO: Implement in Step 2
    // For each level from leaf to root:
    // - Select up to Z blocks from stash that can be placed here
    // - Pad with dummies
    // - Encrypt and write to server
    ORAM_UNIMPLEMENTED();
}

std::vector<Block> PathOram::select_blocks_for_node(NodeId node_id, size_t max_blocks) {
    // TODO: Implement in Step 2
    // Find blocks in stash whose assigned leaf is in the subtree rooted at node_id
    // Select up to max_blocks (any tie-breaking strategy is fine)
    // Remove selected blocks from stash and return them
    ORAM_UNIMPLEMENTED();
}

void PathOram::check_invariants() const {
    // TODO: Implement in Step 2
    // For testing/debugging only. Check:
    // 1. Each written block appears exactly once (stash XOR bucket)
    // 2. Blocks in buckets satisfy path constraint
    // 3. Position map is consistent with block locations
    ORAM_UNIMPLEMENTED();
}

} // namespace oram
