#pragma once

#include "common.h"
#include "protocol.h"
#include "tree.h"
#include "../third_party/net/net_io_channel.h"

#include <vector>
#include <memory>
#include <functional>

namespace oram {

/**
 * Server-side storage for encrypted ORAM buckets.
 *
 * The server stores encrypted buckets indexed by node ID.
 * It knows nothing about the plaintext data or block IDs.
 *
 * The server can run either:
 * 1. As a separate process with its own network loop (run())
 * 2. In-process using the storage directly (for testing/benchmarking)
 */
class OramServer {
public:
    /**
     * Construct server with given storage parameters.
     *
     * @param num_buckets Total number of buckets in the ORAM tree
     * @param bucket_size Size of each encrypted bucket in bytes
     */
    OramServer(size_t num_buckets, size_t bucket_size);

    ~OramServer();

    // Non-copyable
    OramServer(const OramServer&) = delete;
    OramServer& operator=(const OramServer&) = delete;

    /**
     * Initialize storage (allocate and zero all buckets).
     */
    void init();

    /**
     * Initialize storage with dummy encrypted buckets.
     *
     * @param bucket_generator Function that generates initial encrypted bucket data
     */
    void init(std::function<std::vector<Byte>(NodeId)> bucket_generator);

    /**
     * Read a single bucket.
     *
     * @param node_id The bucket's node ID
     * @return The encrypted bucket data
     */
    std::vector<Byte> read_bucket(NodeId node_id) const;

    /**
     * Write a single bucket.
     *
     * @param node_id The bucket's node ID
     * @param data The encrypted bucket data
     */
    void write_bucket(NodeId node_id, std::span<const Byte> data);

    /**
     * Read all buckets on a path from root to leaf.
     *
     * @param leaf_id The leaf ID
     * @param depth The tree depth
     * @return Vector of encrypted buckets from root to leaf
     */
    std::vector<std::vector<Byte>> read_path(LeafId leaf_id, size_t depth) const;

    /**
     * Write all buckets on a path from root to leaf.
     *
     * @param leaf_id The leaf ID
     * @param depth The tree depth
     * @param buckets Vector of encrypted buckets from root to leaf
     */
    void write_path(LeafId leaf_id, size_t depth, const std::vector<std::vector<Byte>>& buckets);

    /**
     * Read multiple specific buckets.
     *
     * @param node_ids The node IDs to read
     * @return Vector of encrypted buckets
     */
    std::vector<std::vector<Byte>> read_buckets(const std::vector<NodeId>& node_ids) const;

    /**
     * Write multiple specific buckets.
     *
     * @param node_ids The node IDs to write
     * @param buckets Vector of encrypted bucket data
     */
    void write_buckets(const std::vector<NodeId>& node_ids,
                       const std::vector<std::vector<Byte>>& buckets);

    /**
     * Read a single slot from a bucket (for Ring ORAM).
     *
     * @param node_id The bucket's node ID
     * @param slot_index The slot index within the bucket
     * @param encrypted_slot_size The size of an encrypted slot
     * @return The encrypted slot data
     */
    std::vector<Byte> read_slot(NodeId node_id, size_t slot_index, size_t encrypted_slot_size) const;

    /**
     * Write a single slot to a bucket (for Ring ORAM).
     *
     * @param node_id The bucket's node ID
     * @param slot_index The slot index within the bucket
     * @param data The encrypted slot data
     */
    void write_slot(NodeId node_id, size_t slot_index, std::span<const Byte> data);

    /**
     * Run the server network loop.
     * Listens for client connections and handles requests.
     *
     * @param port The port to listen on
     */
    void run(int port);

    /**
     * Handle a single client connection.
     * Processes requests until the connection is closed.
     *
     * @param io The network channel
     */
    void handle_client(NetIO& io);

    /**
     * Get total storage size in bytes.
     */
    size_t storage_size() const { return storage_.size() * bucket_size_; }

    /**
     * Get number of buckets.
     */
    size_t num_buckets() const { return storage_.size(); }

    /**
     * Get bucket size.
     */
    size_t bucket_size() const { return bucket_size_; }

private:
    size_t bucket_size_;
    std::vector<std::vector<Byte>> storage_;  // storage_[node_id] = encrypted bucket

    // Request handlers
    void handle_read_bucket(NetIO& io, std::span<const Byte> payload);
    void handle_write_bucket(NetIO& io, std::span<const Byte> payload);
    void handle_read_path(NetIO& io, std::span<const Byte> payload);
    void handle_write_path(NetIO& io, std::span<const Byte> payload);
    void handle_read_buckets(NetIO& io, std::span<const Byte> payload);
    void handle_write_buckets(NetIO& io, std::span<const Byte> payload);
    void handle_read_slot(NetIO& io, std::span<const Byte> payload);
    void handle_write_slot(NetIO& io, std::span<const Byte> payload);
    void handle_init(NetIO& io, std::span<const Byte> payload);

    // Send message helper
    void send_message(NetIO& io, MessageType type, std::span<const Byte> payload);
};

/**
 * Client stub for communicating with an ORAM server.
 *
 * This provides a high-level interface for bucket operations
 * that handles the network protocol.
 */
class OramClient {
public:
    /**
     * Construct client connected to a server.
     *
     * @param io The network channel (must already be connected)
     * @param bucket_size Size of each encrypted bucket
     * @param depth Tree depth
     */
    OramClient(NetIO& io, size_t bucket_size, size_t depth);

    /**
     * Request server initialization.
     *
     * @param num_buckets Number of buckets
     * @param bucket_size Size of each encrypted bucket
     */
    void init(size_t num_buckets, size_t bucket_size);

    /**
     * Read a single bucket.
     *
     * @param node_id The bucket's node ID
     * @return The encrypted bucket data
     */
    std::vector<Byte> read_bucket(NodeId node_id);

    /**
     * Write a single bucket.
     *
     * @param node_id The bucket's node ID
     * @param data The encrypted bucket data
     */
    void write_bucket(NodeId node_id, std::span<const Byte> data);

    /**
     * Read all buckets on a path from root to leaf.
     * This is more efficient than reading buckets individually.
     *
     * @param leaf_id The leaf ID
     * @return Vector of encrypted buckets from root to leaf
     */
    std::vector<std::vector<Byte>> read_path(LeafId leaf_id);

    /**
     * Write all buckets on a path from root to leaf.
     * This is more efficient than writing buckets individually.
     *
     * @param leaf_id The leaf ID
     * @param buckets Vector of encrypted buckets from root to leaf
     */
    void write_path(LeafId leaf_id, const std::vector<std::vector<Byte>>& buckets);

    /**
     * Read multiple specific buckets in a single round trip.
     *
     * @param node_ids The node IDs to read
     * @return Vector of encrypted buckets
     */
    std::vector<std::vector<Byte>> read_buckets(const std::vector<NodeId>& node_ids);

    /**
     * Write multiple specific buckets in a single round trip.
     *
     * @param node_ids The node IDs to write
     * @param buckets Vector of encrypted bucket data
     */
    void write_buckets(const std::vector<NodeId>& node_ids,
                       const std::vector<std::vector<Byte>>& buckets);

    /**
     * Read a single slot from a bucket (for Ring ORAM).
     *
     * @param node_id The bucket's node ID
     * @param slot_index The slot index within the bucket
     * @param encrypted_slot_size The size of an encrypted slot
     * @return The encrypted slot data
     */
    std::vector<Byte> read_slot(NodeId node_id, size_t slot_index, size_t encrypted_slot_size);

    /**
     * Write a single slot to a bucket (for Ring ORAM).
     *
     * @param node_id The bucket's node ID
     * @param slot_index The slot index within the bucket
     * @param data The encrypted slot data
     */
    void write_slot(NodeId node_id, size_t slot_index, std::span<const Byte> data);

private:
    NetIO& io_;
    size_t bucket_size_;
    size_t depth_;

    // Send request and receive response
    void send_request(MessageType type, std::span<const Byte> payload);
    MessageHeader recv_header();
    void recv_payload(std::span<Byte> buffer);
};

} // namespace oram
