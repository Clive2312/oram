#include "oram/ring_oram.h"
#include <algorithm>
#include <stdexcept>
#include <cstring>

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
    if (config.use_local_server) {
        local_server_ = std::make_unique<OramServer>(
            params_.num_buckets,
            codec_->encrypted_bucket_size()
        );

        // Initialize with encrypted dummy buckets
        local_server_->init([this](NodeId) {
            Bucket bucket(params_.Z + params_.S, params_.block_size);
            return codec_->encode(bucket);
        });

        remote_client_ = nullptr;
    } else if (config.network) {
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
    // TODO: Implement in Step 3
    //
    // The Ring ORAM access algorithm from the pseudocode:
    //
    // l_old := PositionMap[a]
    // l_new := UniformRandomLeaf()
    // PositionMap[a] := l_new
    //
    // data := ReadPath(l_old, a)  // returns data if found, else nullopt
    //
    // IF data == nullopt:
    //   data := RemoveFromStash(a)
    //
    // IF op == READ:
    //   RETURN data
    // ELSE:
    //   data := new_data
    //   Stash := Stash ∪ {(a, l_new, data)}
    //
    // round := (round + 1) mod A
    // IF round == 0:
    //   EvictPath()
    //
    // EarlyReshuffle(l_old)

    ORAM_UNIMPLEMENTED();
}

std::optional<std::vector<Byte>> RingOram::read_path(LeafId leaf_id, BlockId target_block_id) {
    // TODO: Implement in Step 3
    //
    // ReadPath reads exactly one slot per bucket on the path, invalidates it,
    // and increments count.
    //
    // FOR level = 0 .. L:
    //   bucket := BucketAtPath(l, level)
    //   offset := GetBlockOffset(bucket, a)
    //   blk := ReadSlot(bucket, offset)
    //   Invalidate(bucket, offset)
    //   IF blk.addr == a:
    //     found := blk.data
    //   bucket.count := bucket.count + 1
    //
    // RETURN found

    ORAM_UNIMPLEMENTED();
}

void RingOram::evict_path() {
    // TODO: Implement in Step 3
    //
    // EvictPath runs on a deterministic public schedule:
    //
    // l := G mod 2^L
    // G := G + 1
    //
    // # Read phase: move remaining real blocks from buckets into stash
    // FOR level = 0 .. L:
    //   bucket := BucketAtPath(l, level)
    //   Stash := Stash ∪ ReadBucket(bucket)
    //
    // # Write phase: push stash blocks down and reshuffle buckets
    // FOR level = L .. 0:
    //   bucket := BucketAtPath(l, level)
    //   WriteBucket(bucket, Stash)
    //   bucket.count := 0

    ORAM_UNIMPLEMENTED();
}

void RingOram::early_reshuffle(LeafId leaf_id) {
    // TODO: Implement in Step 3
    //
    // FOR level = 0 .. L:
    //   bucket := BucketAtPath(l, level)
    //   IF bucket.count >= S:
    //     Stash := Stash ∪ ReadBucket(bucket)
    //     WriteBucket(bucket, Stash)
    //     bucket.count := 0

    ORAM_UNIMPLEMENTED();
}

size_t RingOram::get_block_offset(NodeId node_id, BlockId target_block_id) {
    // TODO: Implement in Step 3
    //
    // Chooses which physical slot to read for this bucket during ReadPath.
    // If the target is present and still valid, read that slot;
    // otherwise choose a valid dummy slot.
    //
    // FOR j = 0 .. Z-1:
    //   IF bucket.addrs[j] == a AND bucket.valids[ bucket.ptrs[j] ] == 1:
    //     RETURN bucket.ptrs[j]
    // RETURN RandomValidDummySlot(bucket.valids)

    ORAM_UNIMPLEMENTED();
}

Block RingOram::read_slot(NodeId node_id, size_t slot_index) {
    // TODO: Implement in Step 3
    // Read encrypted bucket from server, decrypt, return specific slot

    ORAM_UNIMPLEMENTED();
}

void RingOram::read_bucket_into_stash(NodeId node_id) {
    // TODO: Implement in Step 3
    //
    // Read up to Z real slots (padding with dummy reads if fewer remain).
    //
    // real_read := 0
    // FOR j = 0 .. Z-1:
    //   t := bucket.ptrs[j]
    //   IF bucket.valids[t] == 1:
    //     blk := ReadSlot(bucket, t)
    //     real_read := real_read + 1
    //     IF bucket.addrs[j] != ⊥:
    //       Stash := Stash ∪ {(bucket.addrs[j], bucket.leaves[j], blk.data)}
    //
    // WHILE real_read < Z:
    //   t := RandomValidDummySlot(bucket.valids)
    //   ReadSlot(bucket, t)
    //   real_read := real_read + 1

    ORAM_UNIMPLEMENTED();
}

void RingOram::write_bucket_from_stash(NodeId node_id) {
    // TODO: Implement in Step 3
    //
    // Evict compatible blocks from stash into this bucket, then reshuffle
    // and reset metadata. The bucket must end up with exactly Z+S data slots.
    //
    // selected := SelectUpToZCompatibleBlocks(Stash, bucket)
    // RemoveFromStash(Stash, selected)
    //
    // perm := FreshRandomPermutation(0 .. Z+S-1)
    //
    // Place selected blocks into permuted data slots
    // Fill remaining slots with dummy blocks
    //
    // Set bucket.valids[*] = 1
    // Set bucket.count = 0
    // Refresh and apply encryption to bucket contents

    ORAM_UNIMPLEMENTED();
}

void RingOram::invalidate_slot(NodeId node_id, size_t slot_index) {
    // TODO: Implement in Step 3
    bucket_metadata_[node_id].valids.reset(slot_index);
}

size_t RingOram::random_valid_dummy_slot(NodeId node_id) {
    // TODO: Implement in Step 3
    // Find valid slots in the dummy region (indices Z to Z+S-1)
    // Return a random one

    ORAM_UNIMPLEMENTED();
}

void RingOram::check_invariants() const {
    // TODO: Implement in Step 3
    // Verify Ring ORAM invariants (for testing only)

    ORAM_UNIMPLEMENTED();
}

} // namespace oram
