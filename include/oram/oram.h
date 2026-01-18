#pragma once

#include "common.h"
#include "crypto.h"
#include "position_map.h"
#include "stash.h"
#include "bucket_codec.h"
#include "server.h"
#include "tree.h"

#include <memory>
#include <random>
#include <functional>

namespace oram {

/**
 * Random number generator interface for ORAM.
 *
 * Supports both:
 * - Production mode: cryptographically secure random numbers
 * - Deterministic mode: seeded PRNG for reproducible testing
 */
class OramRng {
public:
    virtual ~OramRng() = default;

    /**
     * Generate a random leaf ID in [0, num_leaves).
     */
    virtual LeafId random_leaf(size_t num_leaves) = 0;

    /**
     * Generate a random integer in [0, max).
     */
    virtual uint64_t random_range(uint64_t max) = 0;

    /**
     * Fill buffer with random bytes.
     */
    virtual void fill_random(std::span<Byte> buffer) = 0;
};

/**
 * Cryptographically secure RNG using OpenSSL.
 */
class SecureOramRng : public OramRng {
public:
    LeafId random_leaf(size_t num_leaves) override {
        return static_cast<LeafId>(SecureRng::random_range(num_leaves));
    }

    uint64_t random_range(uint64_t max) override {
        return SecureRng::random_range(max);
    }

    void fill_random(std::span<Byte> buffer) override {
        SecureRng::fill(buffer);
    }
};

/**
 * Deterministic RNG for reproducible testing.
 */
class DeterministicOramRng : public OramRng {
public:
    explicit DeterministicOramRng(uint64_t seed) : rng_(seed) {}

    LeafId random_leaf(size_t num_leaves) override {
        std::uniform_int_distribution<LeafId> dist(0, num_leaves - 1);
        return dist(rng_);
    }

    uint64_t random_range(uint64_t max) override {
        if (max == 0) return 0;
        std::uniform_int_distribution<uint64_t> dist(0, max - 1);
        return dist(rng_);
    }

    void fill_random(std::span<Byte> buffer) override {
        std::uniform_int_distribution<int> dist(0, 255);
        for (Byte& b : buffer) {
            b = static_cast<Byte>(dist(rng_));
        }
    }

private:
    std::mt19937_64 rng_;
};

/**
 * Configuration for ORAM initialization.
 */
struct OramConfig {
    size_t num_blocks;      // Number of logical blocks (N)
    size_t block_size;      // Size of each block in bytes (B)
    size_t Z;               // Bucket capacity (typically 4 for Path ORAM)

    // Ring ORAM specific
    size_t S = 0;           // Extra dummy slots (for Ring ORAM)
    size_t A = 1;           // Eviction period (for Ring ORAM)

    // Encryption key (if not provided, a random key will be generated)
    std::optional<AeadKey> key;

    // RNG (if not provided, secure RNG will be used)
    std::shared_ptr<OramRng> rng;

    // Network connection to server (required for network mode)
    NetIO* network = nullptr;

    // For local testing: use an in-process server instead of network
    bool use_local_server = false;
};

/**
 * Abstract base class for ORAM implementations.
 *
 * Provides a unified interface for Path ORAM and Ring ORAM.
 * Derived classes implement the ORAM-specific access algorithms.
 */
class Oram {
public:
    virtual ~Oram() = default;

    /**
     * Initialize the ORAM.
     * Must be called before any access operations.
     *
     * @param config ORAM configuration
     */
    virtual void init(const OramConfig& config) = 0;

    /**
     * Read a block from the ORAM.
     *
     * @param block_id The logical block address
     * @return The block data
     */
    virtual std::vector<Byte> read(BlockId block_id) = 0;

    /**
     * Write a block to the ORAM.
     *
     * @param block_id The logical block address
     * @param data The block data to write
     */
    virtual void write(BlockId block_id, std::span<const Byte> data) = 0;

    /**
     * Combined read-write operation (more efficient than separate calls).
     *
     * If data is provided, performs a write and returns the old value.
     * If data is nullopt, performs a read.
     *
     * @param block_id The logical block address
     * @param data Optional new data to write
     * @return The block data (old data if write, current data if read)
     */
    virtual std::vector<Byte> access(BlockId block_id, std::optional<std::span<const Byte>> data) = 0;

    /**
     * Get ORAM parameters.
     */
    virtual const OramParams& params() const = 0;

    /**
     * Get ORAM type.
     */
    virtual OramType type() const = 0;

    /**
     * Get current stash size.
     * Useful for monitoring stash overflow.
     */
    virtual size_t stash_size() const = 0;

    /**
     * Check ORAM invariants (for debugging/testing).
     * Throws if any invariant is violated.
     */
    virtual void check_invariants() const = 0;

protected:
    // Shared state for derived classes
    OramParams params_;
    std::unique_ptr<PositionMap> position_map_;
    Stash stash_;
    std::unique_ptr<Aead> aead_;
    std::unique_ptr<BucketCodec> codec_;
    std::shared_ptr<OramRng> rng_;

    // Server access (either network or local)
    std::unique_ptr<OramServer> local_server_;
    std::unique_ptr<OramClient> remote_client_;

    /**
     * Read buckets from server (handles local vs remote).
     */
    std::vector<std::vector<Byte>> server_read_path(LeafId leaf_id) const;

    /**
     * Write buckets to server (handles local vs remote).
     */
    void server_write_path(LeafId leaf_id, const std::vector<std::vector<Byte>>& buckets) const;

    /**
     * Read a single bucket from server.
     */
    std::vector<Byte> server_read_bucket(NodeId node_id) const;

    /**
     * Write a single bucket to server.
     */
    void server_write_bucket(NodeId node_id, std::span<const Byte> data) const;

    /**
     * Generate initial encrypted bucket (all dummies).
     */
    std::vector<Byte> generate_initial_bucket();
};

} // namespace oram
