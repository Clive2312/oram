#pragma once

#include "common.h"
#include <cstring>

namespace oram {

/**
 * Network protocol message types for client-server communication.
 */
enum class MessageType : uint8_t {
    // Client -> Server
    ReadBucket = 0x01,      // Request to read a single bucket
    WriteBucket = 0x02,     // Request to write a single bucket
    ReadPath = 0x03,        // Request to read all buckets on a path
    WritePath = 0x04,       // Request to write all buckets on a path
    ReadBuckets = 0x05,     // Request to read multiple specific buckets
    WriteBuckets = 0x06,    // Request to write multiple specific buckets
    Init = 0x07,            // Initialize server storage

    // Server -> Client
    BucketData = 0x10,      // Response containing bucket data
    PathData = 0x11,        // Response containing path data
    BucketsData = 0x12,     // Response containing multiple buckets
    Ack = 0x13,             // Acknowledgment of write operation
    Error = 0xFF            // Error response
};

/**
 * Header for all network messages.
 * Sent before any message payload.
 */
struct MessageHeader {
    MessageType type;
    uint32_t payload_size;  // Size of payload following this header

    static constexpr size_t SIZE = sizeof(MessageType) + sizeof(uint32_t);

    void serialize(std::span<Byte> out) const {
        assert(out.size() >= SIZE);
        out[0] = static_cast<Byte>(type);
        std::memcpy(out.data() + 1, &payload_size, sizeof(payload_size));
    }

    static MessageHeader deserialize(std::span<const Byte> in) {
        assert(in.size() >= SIZE);
        MessageHeader h;
        h.type = static_cast<MessageType>(in[0]);
        std::memcpy(&h.payload_size, in.data() + 1, sizeof(h.payload_size));
        return h;
    }
};

/**
 * Request to read a single bucket by node ID.
 */
struct ReadBucketRequest {
    NodeId node_id;

    static constexpr size_t SIZE = sizeof(NodeId);

    void serialize(std::span<Byte> out) const {
        assert(out.size() >= SIZE);
        std::memcpy(out.data(), &node_id, sizeof(node_id));
    }

    static ReadBucketRequest deserialize(std::span<const Byte> in) {
        assert(in.size() >= SIZE);
        ReadBucketRequest r;
        std::memcpy(&r.node_id, in.data(), sizeof(r.node_id));
        return r;
    }
};

/**
 * Request to write a single bucket.
 * Payload follows: encrypted bucket data
 */
struct WriteBucketRequest {
    NodeId node_id;
    // Followed by: encrypted_bucket_data (variable length)

    static constexpr size_t HEADER_SIZE = sizeof(NodeId);

    void serialize_header(std::span<Byte> out) const {
        assert(out.size() >= HEADER_SIZE);
        std::memcpy(out.data(), &node_id, sizeof(node_id));
    }

    static WriteBucketRequest deserialize_header(std::span<const Byte> in) {
        assert(in.size() >= HEADER_SIZE);
        WriteBucketRequest r;
        std::memcpy(&r.node_id, in.data(), sizeof(r.node_id));
        return r;
    }
};

/**
 * Request to read all buckets on a root-to-leaf path.
 */
struct ReadPathRequest {
    LeafId leaf_id;
    uint32_t depth;  // Tree depth (number of levels to read)

    static constexpr size_t SIZE = sizeof(LeafId) + sizeof(uint32_t);

    void serialize(std::span<Byte> out) const {
        assert(out.size() >= SIZE);
        size_t offset = 0;
        std::memcpy(out.data() + offset, &leaf_id, sizeof(leaf_id));
        offset += sizeof(leaf_id);
        std::memcpy(out.data() + offset, &depth, sizeof(depth));
    }

    static ReadPathRequest deserialize(std::span<const Byte> in) {
        assert(in.size() >= SIZE);
        ReadPathRequest r;
        size_t offset = 0;
        std::memcpy(&r.leaf_id, in.data() + offset, sizeof(r.leaf_id));
        offset += sizeof(r.leaf_id);
        std::memcpy(&r.depth, in.data() + offset, sizeof(r.depth));
        return r;
    }
};

/**
 * Request to write all buckets on a path.
 * Payload follows: array of (node_id, encrypted_data) pairs
 */
struct WritePathRequest {
    LeafId leaf_id;
    uint32_t depth;
    uint32_t bucket_size;  // Size of each encrypted bucket
    // Followed by: (depth+1) encrypted buckets

    static constexpr size_t HEADER_SIZE = sizeof(LeafId) + sizeof(uint32_t) * 2;

    void serialize_header(std::span<Byte> out) const {
        assert(out.size() >= HEADER_SIZE);
        size_t offset = 0;
        std::memcpy(out.data() + offset, &leaf_id, sizeof(leaf_id));
        offset += sizeof(leaf_id);
        std::memcpy(out.data() + offset, &depth, sizeof(depth));
        offset += sizeof(depth);
        std::memcpy(out.data() + offset, &bucket_size, sizeof(bucket_size));
    }

    static WritePathRequest deserialize_header(std::span<const Byte> in) {
        assert(in.size() >= HEADER_SIZE);
        WritePathRequest r;
        size_t offset = 0;
        std::memcpy(&r.leaf_id, in.data() + offset, sizeof(r.leaf_id));
        offset += sizeof(r.leaf_id);
        std::memcpy(&r.depth, in.data() + offset, sizeof(r.depth));
        offset += sizeof(r.depth);
        std::memcpy(&r.bucket_size, in.data() + offset, sizeof(r.bucket_size));
        return r;
    }
};

/**
 * Request to read multiple specific buckets.
 */
struct ReadBucketsRequest {
    uint32_t count;  // Number of buckets to read
    // Followed by: count node IDs

    static constexpr size_t HEADER_SIZE = sizeof(uint32_t);

    void serialize_header(std::span<Byte> out) const {
        assert(out.size() >= HEADER_SIZE);
        std::memcpy(out.data(), &count, sizeof(count));
    }

    static ReadBucketsRequest deserialize_header(std::span<const Byte> in) {
        assert(in.size() >= HEADER_SIZE);
        ReadBucketsRequest r;
        std::memcpy(&r.count, in.data(), sizeof(r.count));
        return r;
    }
};

/**
 * Request to write multiple specific buckets.
 */
struct WriteBucketsRequest {
    uint32_t count;        // Number of buckets to write
    uint32_t bucket_size;  // Size of each encrypted bucket
    // Followed by: count (node_id, encrypted_data) pairs

    static constexpr size_t HEADER_SIZE = sizeof(uint32_t) * 2;

    void serialize_header(std::span<Byte> out) const {
        assert(out.size() >= HEADER_SIZE);
        std::memcpy(out.data(), &count, sizeof(count));
        std::memcpy(out.data() + sizeof(count), &bucket_size, sizeof(bucket_size));
    }

    static WriteBucketsRequest deserialize_header(std::span<const Byte> in) {
        assert(in.size() >= HEADER_SIZE);
        WriteBucketsRequest r;
        std::memcpy(&r.count, in.data(), sizeof(r.count));
        std::memcpy(&r.bucket_size, in.data() + sizeof(r.count), sizeof(r.bucket_size));
        return r;
    }
};

/**
 * Request to initialize server storage.
 */
struct InitRequest {
    uint32_t num_buckets;   // Total number of buckets
    uint32_t bucket_size;   // Size of each encrypted bucket

    static constexpr size_t SIZE = sizeof(uint32_t) * 2;

    void serialize(std::span<Byte> out) const {
        assert(out.size() >= SIZE);
        std::memcpy(out.data(), &num_buckets, sizeof(num_buckets));
        std::memcpy(out.data() + sizeof(num_buckets), &bucket_size, sizeof(bucket_size));
    }

    static InitRequest deserialize(std::span<const Byte> in) {
        assert(in.size() >= SIZE);
        InitRequest r;
        std::memcpy(&r.num_buckets, in.data(), sizeof(r.num_buckets));
        std::memcpy(&r.bucket_size, in.data() + sizeof(r.num_buckets), sizeof(r.bucket_size));
        return r;
    }
};

/**
 * Response containing bucket data.
 * Payload is the raw encrypted bucket.
 */
struct BucketDataResponse {
    // Payload is the encrypted bucket data
};

/**
 * Response containing path data.
 */
struct PathDataResponse {
    uint32_t depth;
    uint32_t bucket_size;
    // Followed by: (depth+1) encrypted buckets

    static constexpr size_t HEADER_SIZE = sizeof(uint32_t) * 2;

    void serialize_header(std::span<Byte> out) const {
        assert(out.size() >= HEADER_SIZE);
        std::memcpy(out.data(), &depth, sizeof(depth));
        std::memcpy(out.data() + sizeof(depth), &bucket_size, sizeof(bucket_size));
    }

    static PathDataResponse deserialize_header(std::span<const Byte> in) {
        assert(in.size() >= HEADER_SIZE);
        PathDataResponse r;
        std::memcpy(&r.depth, in.data(), sizeof(r.depth));
        std::memcpy(&r.bucket_size, in.data() + sizeof(r.depth), sizeof(r.bucket_size));
        return r;
    }
};

/**
 * Response containing multiple buckets data.
 */
struct BucketsDataResponse {
    uint32_t count;
    uint32_t bucket_size;
    // Followed by: count encrypted buckets

    static constexpr size_t HEADER_SIZE = sizeof(uint32_t) * 2;

    void serialize_header(std::span<Byte> out) const {
        assert(out.size() >= HEADER_SIZE);
        std::memcpy(out.data(), &count, sizeof(count));
        std::memcpy(out.data() + sizeof(count), &bucket_size, sizeof(bucket_size));
    }

    static BucketsDataResponse deserialize_header(std::span<const Byte> in) {
        assert(in.size() >= HEADER_SIZE);
        BucketsDataResponse r;
        std::memcpy(&r.count, in.data(), sizeof(r.count));
        std::memcpy(&r.bucket_size, in.data() + sizeof(r.count), sizeof(r.bucket_size));
        return r;
    }
};

} // namespace oram
