#include "oram/server.h"
#include <stdexcept>
#include <cstring>

namespace oram {

// ============================================================================
// OramServer Implementation
// ============================================================================

OramServer::OramServer(size_t num_buckets, size_t bucket_size)
    : bucket_size_(bucket_size)
    , storage_(num_buckets)
{
    for (auto& bucket : storage_) {
        bucket.resize(bucket_size_, 0);
    }
}

OramServer::~OramServer() = default;

void OramServer::init() {
    for (auto& bucket : storage_) {
        std::fill(bucket.begin(), bucket.end(), 0);
    }
}

void OramServer::init(std::function<std::vector<Byte>(NodeId)> bucket_generator) {
    for (size_t i = 0; i < storage_.size(); i++) {
        storage_[i] = bucket_generator(static_cast<NodeId>(i));
        if (storage_[i].size() != bucket_size_) {
            throw std::runtime_error("Bucket generator produced wrong size bucket");
        }
    }
}

std::vector<Byte> OramServer::read_bucket(NodeId node_id) const {
    if (node_id >= storage_.size()) {
        throw std::out_of_range("Invalid node ID");
    }
    return storage_[node_id];
}

void OramServer::write_bucket(NodeId node_id, std::span<const Byte> data) {
    if (node_id >= storage_.size()) {
        throw std::out_of_range("Invalid node ID");
    }
    if (data.size() != bucket_size_) {
        throw std::invalid_argument("Bucket size mismatch");
    }
    std::memcpy(storage_[node_id].data(), data.data(), bucket_size_);
}

std::vector<std::vector<Byte>> OramServer::read_path(LeafId leaf_id, size_t depth) const {
    std::vector<std::vector<Byte>> result;
    result.reserve(depth + 1);

    auto path = TreeUtil::path_to_leaf(leaf_id, depth);
    for (NodeId node_id : path) {
        result.push_back(read_bucket(node_id));
    }
    return result;
}

void OramServer::write_path(LeafId leaf_id, size_t depth,
                            const std::vector<std::vector<Byte>>& buckets) {
    if (buckets.size() != depth + 1) {
        throw std::invalid_argument("Wrong number of buckets for path");
    }

    auto path = TreeUtil::path_to_leaf(leaf_id, depth);
    for (size_t i = 0; i < path.size(); i++) {
        write_bucket(path[i], buckets[i]);
    }
}

std::vector<std::vector<Byte>> OramServer::read_buckets(
    const std::vector<NodeId>& node_ids) const
{
    std::vector<std::vector<Byte>> result;
    result.reserve(node_ids.size());
    for (NodeId id : node_ids) {
        result.push_back(read_bucket(id));
    }
    return result;
}

void OramServer::write_buckets(const std::vector<NodeId>& node_ids,
                               const std::vector<std::vector<Byte>>& buckets) {
    if (node_ids.size() != buckets.size()) {
        throw std::invalid_argument("Mismatched node_ids and buckets sizes");
    }
    for (size_t i = 0; i < node_ids.size(); i++) {
        write_bucket(node_ids[i], buckets[i]);
    }
}

std::vector<Byte> OramServer::read_slot(NodeId node_id, size_t slot_index,
                                        size_t encrypted_slot_size) const {
    if (node_id >= storage_.size()) {
        throw std::out_of_range("Invalid node ID");
    }

    size_t offset = slot_index * encrypted_slot_size;
    if (offset + encrypted_slot_size > bucket_size_) {
        throw std::out_of_range("Invalid slot index");
    }

    std::vector<Byte> slot_data(encrypted_slot_size);
    std::memcpy(slot_data.data(), storage_[node_id].data() + offset, encrypted_slot_size);
    return slot_data;
}

void OramServer::write_slot(NodeId node_id, size_t slot_index, std::span<const Byte> data) {
    if (node_id >= storage_.size()) {
        throw std::out_of_range("Invalid node ID");
    }

    size_t offset = slot_index * data.size();
    if (offset + data.size() > bucket_size_) {
        throw std::out_of_range("Invalid slot index");
    }

    std::memcpy(storage_[node_id].data() + offset, data.data(), data.size());
}

void OramServer::run(int port) {
    // Create server socket and wait for connection
    NetIO io(nullptr, port, true, true);  // server mode, full buffering, quiet

    // Handle client requests until connection closes
    handle_client(io);
}

void OramServer::handle_client(NetIO& io) {
    std::vector<Byte> header_buf(MessageHeader::SIZE);
    std::vector<Byte> payload_buf;

    while (true) {
        // Read message header
        io.recv_data(header_buf.data(), MessageHeader::SIZE);
        auto header = MessageHeader::deserialize(header_buf);

        // Read payload
        payload_buf.resize(header.payload_size);
        if (header.payload_size > 0) {
            io.recv_data(payload_buf.data(), header.payload_size);
        }

        // Dispatch based on message type
        switch (header.type) {
            case MessageType::ReadBucket:
                handle_read_bucket(io, payload_buf);
                break;
            case MessageType::WriteBucket:
                handle_write_bucket(io, payload_buf);
                break;
            case MessageType::ReadPath:
                handle_read_path(io, payload_buf);
                break;
            case MessageType::WritePath:
                handle_write_path(io, payload_buf);
                break;
            case MessageType::ReadBuckets:
                handle_read_buckets(io, payload_buf);
                break;
            case MessageType::WriteBuckets:
                handle_write_buckets(io, payload_buf);
                break;
            case MessageType::ReadSlot:
                handle_read_slot(io, payload_buf);
                break;
            case MessageType::WriteSlot:
                handle_write_slot(io, payload_buf);
                break;
            case MessageType::Init:
                handle_init(io, payload_buf);
                break;
            default:
                // Unknown message type - send error
                send_message(io, MessageType::Error, {});
                break;
        }
    }
}

void OramServer::handle_read_bucket(NetIO& io, std::span<const Byte> payload) {
    auto req = ReadBucketRequest::deserialize(payload);
    auto bucket = read_bucket(req.node_id);
    send_message(io, MessageType::BucketData, bucket);
}

void OramServer::handle_write_bucket(NetIO& io, std::span<const Byte> payload) {
    auto req = WriteBucketRequest::deserialize_header(payload);
    std::span<const Byte> data(payload.data() + WriteBucketRequest::HEADER_SIZE,
                               payload.size() - WriteBucketRequest::HEADER_SIZE);
    write_bucket(req.node_id, data);
    send_message(io, MessageType::Ack, {});
}

void OramServer::handle_read_path(NetIO& io, std::span<const Byte> payload) {
    auto req = ReadPathRequest::deserialize(payload);
    auto buckets = read_path(req.leaf_id, req.depth);

    // Build response
    PathDataResponse resp;
    resp.depth = req.depth;
    resp.bucket_size = static_cast<uint32_t>(bucket_size_);

    size_t total_size = PathDataResponse::HEADER_SIZE + (req.depth + 1) * bucket_size_;
    std::vector<Byte> response(total_size);
    resp.serialize_header(response);

    // Copy bucket data
    Byte* ptr = response.data() + PathDataResponse::HEADER_SIZE;
    for (const auto& bucket : buckets) {
        std::memcpy(ptr, bucket.data(), bucket_size_);
        ptr += bucket_size_;
    }

    send_message(io, MessageType::PathData, response);
}

void OramServer::handle_write_path(NetIO& io, std::span<const Byte> payload) {
    auto req = WritePathRequest::deserialize_header(payload);

    // Extract buckets from payload
    std::vector<std::vector<Byte>> buckets;
    buckets.reserve(req.depth + 1);

    const Byte* ptr = payload.data() + WritePathRequest::HEADER_SIZE;
    for (size_t i = 0; i <= req.depth; i++) {
        buckets.emplace_back(ptr, ptr + req.bucket_size);
        ptr += req.bucket_size;
    }

    write_path(req.leaf_id, req.depth, buckets);
    send_message(io, MessageType::Ack, {});
}

void OramServer::handle_read_buckets(NetIO& io, std::span<const Byte> payload) {
    auto req = ReadBucketsRequest::deserialize_header(payload);

    // Extract node IDs
    std::vector<NodeId> node_ids(req.count);
    const Byte* ptr = payload.data() + ReadBucketsRequest::HEADER_SIZE;
    for (uint32_t i = 0; i < req.count; i++) {
        std::memcpy(&node_ids[i], ptr, sizeof(NodeId));
        ptr += sizeof(NodeId);
    }

    auto buckets = read_buckets(node_ids);

    // Build response
    BucketsDataResponse resp;
    resp.count = req.count;
    resp.bucket_size = static_cast<uint32_t>(bucket_size_);

    size_t total_size = BucketsDataResponse::HEADER_SIZE + req.count * bucket_size_;
    std::vector<Byte> response(total_size);
    resp.serialize_header(response);

    // Copy bucket data
    Byte* out_ptr = response.data() + BucketsDataResponse::HEADER_SIZE;
    for (const auto& bucket : buckets) {
        std::memcpy(out_ptr, bucket.data(), bucket_size_);
        out_ptr += bucket_size_;
    }

    send_message(io, MessageType::BucketsData, response);
}

void OramServer::handle_write_buckets(NetIO& io, std::span<const Byte> payload) {
    auto req = WriteBucketsRequest::deserialize_header(payload);

    // Extract node IDs and bucket data
    std::vector<NodeId> node_ids(req.count);
    std::vector<std::vector<Byte>> buckets(req.count);

    const Byte* ptr = payload.data() + WriteBucketsRequest::HEADER_SIZE;

    // Read node IDs
    for (uint32_t i = 0; i < req.count; i++) {
        std::memcpy(&node_ids[i], ptr, sizeof(NodeId));
        ptr += sizeof(NodeId);
    }

    // Read bucket data
    for (uint32_t i = 0; i < req.count; i++) {
        buckets[i].assign(ptr, ptr + req.bucket_size);
        ptr += req.bucket_size;
    }

    write_buckets(node_ids, buckets);
    send_message(io, MessageType::Ack, {});
}

void OramServer::handle_read_slot(NetIO& io, std::span<const Byte> payload) {
    auto req = ReadSlotRequest::deserialize(payload);
    auto slot_data = read_slot(req.node_id, req.slot_index, req.encrypted_slot_size);
    send_message(io, MessageType::SlotData, slot_data);
}

void OramServer::handle_write_slot(NetIO& io, std::span<const Byte> payload) {
    auto req = WriteSlotRequest::deserialize_header(payload);

    // Extract slot data (everything after the header)
    const Byte* slot_data_ptr = payload.data() + WriteSlotRequest::HEADER_SIZE;
    size_t slot_data_size = payload.size() - WriteSlotRequest::HEADER_SIZE;
    std::span<const Byte> slot_data(slot_data_ptr, slot_data_size);

    write_slot(req.node_id, req.slot_index, slot_data);
    send_message(io, MessageType::Ack, {});
}

void OramServer::handle_init(NetIO& io, std::span<const Byte> payload) {
    auto req = InitRequest::deserialize(payload);

    // Reinitialize storage with new parameters
    bucket_size_ = req.bucket_size;
    storage_.resize(req.num_buckets);
    for (auto& bucket : storage_) {
        bucket.resize(bucket_size_, 0);
    }

    send_message(io, MessageType::Ack, {});
}

void OramServer::send_message(NetIO& io, MessageType type, std::span<const Byte> payload) {
    MessageHeader header;
    header.type = type;
    header.payload_size = static_cast<uint32_t>(payload.size());

    std::vector<Byte> header_buf(MessageHeader::SIZE);
    header.serialize(header_buf);

    io.send_data(header_buf.data(), MessageHeader::SIZE);
    if (!payload.empty()) {
        io.send_data(payload.data(), payload.size());
    }
    io.flush();
}

// ============================================================================
// OramClient Implementation
// ============================================================================

OramClient::OramClient(NetIO& io, size_t bucket_size, size_t depth)
    : io_(io)
    , bucket_size_(bucket_size)
    , depth_(depth)
{}

void OramClient::init(size_t num_buckets, size_t bucket_size) {
    bucket_size_ = bucket_size;

    InitRequest req;
    req.num_buckets = static_cast<uint32_t>(num_buckets);
    req.bucket_size = static_cast<uint32_t>(bucket_size);

    std::vector<Byte> payload(InitRequest::SIZE);
    req.serialize(payload);
    send_request(MessageType::Init, payload);

    // Wait for ack
    auto header = recv_header();
    if (header.type != MessageType::Ack) {
        throw std::runtime_error("Server init failed");
    }
}

std::vector<Byte> OramClient::read_bucket(NodeId node_id) {
    ReadBucketRequest req;
    req.node_id = node_id;

    std::vector<Byte> payload(ReadBucketRequest::SIZE);
    req.serialize(payload);
    send_request(MessageType::ReadBucket, payload);

    auto header = recv_header();
    if (header.type != MessageType::BucketData) {
        throw std::runtime_error("Unexpected response type");
    }

    std::vector<Byte> bucket(header.payload_size);
    recv_payload(bucket);
    return bucket;
}

void OramClient::write_bucket(NodeId node_id, std::span<const Byte> data) {
    WriteBucketRequest req;
    req.node_id = node_id;

    std::vector<Byte> payload(WriteBucketRequest::HEADER_SIZE + data.size());
    req.serialize_header(payload);
    std::memcpy(payload.data() + WriteBucketRequest::HEADER_SIZE, data.data(), data.size());
    send_request(MessageType::WriteBucket, payload);

    auto header = recv_header();
    if (header.type != MessageType::Ack) {
        throw std::runtime_error("Write bucket failed");
    }
}

std::vector<std::vector<Byte>> OramClient::read_path(LeafId leaf_id) {
    ReadPathRequest req;
    req.leaf_id = leaf_id;
    req.depth = static_cast<uint32_t>(depth_);

    std::vector<Byte> payload(ReadPathRequest::SIZE);
    req.serialize(payload);
    send_request(MessageType::ReadPath, payload);

    auto header = recv_header();
    if (header.type != MessageType::PathData) {
        throw std::runtime_error("Unexpected response type");
    }

    std::vector<Byte> response(header.payload_size);
    recv_payload(response);

    auto resp = PathDataResponse::deserialize_header(response);

    // Extract buckets
    std::vector<std::vector<Byte>> buckets;
    buckets.reserve(resp.depth + 1);

    const Byte* ptr = response.data() + PathDataResponse::HEADER_SIZE;
    for (size_t i = 0; i <= resp.depth; i++) {
        buckets.emplace_back(ptr, ptr + resp.bucket_size);
        ptr += resp.bucket_size;
    }

    return buckets;
}

void OramClient::write_path(LeafId leaf_id, const std::vector<std::vector<Byte>>& buckets) {
    WritePathRequest req;
    req.leaf_id = leaf_id;
    req.depth = static_cast<uint32_t>(depth_);
    req.bucket_size = static_cast<uint32_t>(bucket_size_);

    size_t payload_size = WritePathRequest::HEADER_SIZE + (depth_ + 1) * bucket_size_;
    std::vector<Byte> payload(payload_size);
    req.serialize_header(payload);

    Byte* ptr = payload.data() + WritePathRequest::HEADER_SIZE;
    for (const auto& bucket : buckets) {
        std::memcpy(ptr, bucket.data(), bucket_size_);
        ptr += bucket_size_;
    }

    send_request(MessageType::WritePath, payload);

    auto header = recv_header();
    if (header.type != MessageType::Ack) {
        throw std::runtime_error("Write path failed");
    }
}

std::vector<std::vector<Byte>> OramClient::read_buckets(const std::vector<NodeId>& node_ids) {
    ReadBucketsRequest req;
    req.count = static_cast<uint32_t>(node_ids.size());

    size_t payload_size = ReadBucketsRequest::HEADER_SIZE + node_ids.size() * sizeof(NodeId);
    std::vector<Byte> payload(payload_size);
    req.serialize_header(payload);

    Byte* ptr = payload.data() + ReadBucketsRequest::HEADER_SIZE;
    for (NodeId id : node_ids) {
        std::memcpy(ptr, &id, sizeof(NodeId));
        ptr += sizeof(NodeId);
    }

    send_request(MessageType::ReadBuckets, payload);

    auto header = recv_header();
    if (header.type != MessageType::BucketsData) {
        throw std::runtime_error("Unexpected response type");
    }

    std::vector<Byte> response(header.payload_size);
    recv_payload(response);

    auto resp = BucketsDataResponse::deserialize_header(response);

    // Extract buckets
    std::vector<std::vector<Byte>> buckets;
    buckets.reserve(resp.count);

    const Byte* in_ptr = response.data() + BucketsDataResponse::HEADER_SIZE;
    for (uint32_t i = 0; i < resp.count; i++) {
        buckets.emplace_back(in_ptr, in_ptr + resp.bucket_size);
        in_ptr += resp.bucket_size;
    }

    return buckets;
}

void OramClient::write_buckets(const std::vector<NodeId>& node_ids,
                               const std::vector<std::vector<Byte>>& buckets) {
    WriteBucketsRequest req;
    req.count = static_cast<uint32_t>(node_ids.size());
    req.bucket_size = static_cast<uint32_t>(bucket_size_);

    size_t payload_size = WriteBucketsRequest::HEADER_SIZE +
                          node_ids.size() * sizeof(NodeId) +
                          node_ids.size() * bucket_size_;
    std::vector<Byte> payload(payload_size);
    req.serialize_header(payload);

    Byte* ptr = payload.data() + WriteBucketsRequest::HEADER_SIZE;

    // Write node IDs
    for (NodeId id : node_ids) {
        std::memcpy(ptr, &id, sizeof(NodeId));
        ptr += sizeof(NodeId);
    }

    // Write bucket data
    for (const auto& bucket : buckets) {
        std::memcpy(ptr, bucket.data(), bucket_size_);
        ptr += bucket_size_;
    }

    send_request(MessageType::WriteBuckets, payload);

    auto header = recv_header();
    if (header.type != MessageType::Ack) {
        throw std::runtime_error("Write buckets failed");
    }
}

std::vector<Byte> OramClient::read_slot(NodeId node_id, size_t slot_index,
                                        size_t encrypted_slot_size) {
    ReadSlotRequest req;
    req.node_id = node_id;
    req.slot_index = static_cast<uint32_t>(slot_index);
    req.encrypted_slot_size = static_cast<uint32_t>(encrypted_slot_size);

    std::vector<Byte> payload(ReadSlotRequest::SIZE);
    req.serialize(payload);
    send_request(MessageType::ReadSlot, payload);

    auto header = recv_header();
    if (header.type != MessageType::SlotData) {
        throw std::runtime_error("Unexpected response type");
    }

    std::vector<Byte> slot_data(header.payload_size);
    recv_payload(slot_data);
    return slot_data;
}

void OramClient::write_slot(NodeId node_id, size_t slot_index, std::span<const Byte> data) {
    WriteSlotRequest req;
    req.node_id = node_id;
    req.slot_index = static_cast<uint32_t>(slot_index);

    std::vector<Byte> payload(WriteSlotRequest::HEADER_SIZE + data.size());
    req.serialize_header(payload);
    std::memcpy(payload.data() + WriteSlotRequest::HEADER_SIZE, data.data(), data.size());
    send_request(MessageType::WriteSlot, payload);

    auto header = recv_header();
    if (header.type != MessageType::Ack) {
        throw std::runtime_error("Write slot failed");
    }
}

void OramClient::send_request(MessageType type, std::span<const Byte> payload) {
    MessageHeader header;
    header.type = type;
    header.payload_size = static_cast<uint32_t>(payload.size());

    std::vector<Byte> header_buf(MessageHeader::SIZE);
    header.serialize(header_buf);

    io_.send_data(header_buf.data(), MessageHeader::SIZE);
    if (!payload.empty()) {
        io_.send_data(payload.data(), payload.size());
    }
    io_.flush();
}

MessageHeader OramClient::recv_header() {
    std::vector<Byte> buf(MessageHeader::SIZE);
    io_.recv_data(buf.data(), MessageHeader::SIZE);
    return MessageHeader::deserialize(buf);
}

void OramClient::recv_payload(std::span<Byte> buffer) {
    io_.recv_data(buffer.data(), buffer.size());
}

} // namespace oram
