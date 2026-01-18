#include "oram/oram.h"

namespace oram {

std::vector<std::vector<Byte>> Oram::server_read_path(LeafId leaf_id) {
    if (local_server_) {
        return local_server_->read_path(leaf_id, params_.tree_depth);
    } else if (remote_client_) {
        return remote_client_->read_path(leaf_id);
    } else {
        throw std::runtime_error("No server connection available");
    }
}

void Oram::server_write_path(LeafId leaf_id, const std::vector<std::vector<Byte>>& buckets) {
    if (local_server_) {
        local_server_->write_path(leaf_id, params_.tree_depth, buckets);
    } else if (remote_client_) {
        remote_client_->write_path(leaf_id, buckets);
    } else {
        throw std::runtime_error("No server connection available");
    }
}

std::vector<Byte> Oram::server_read_bucket(NodeId node_id) {
    if (local_server_) {
        return local_server_->read_bucket(node_id);
    } else if (remote_client_) {
        return remote_client_->read_bucket(node_id);
    } else {
        throw std::runtime_error("No server connection available");
    }
}

void Oram::server_write_bucket(NodeId node_id, std::span<const Byte> data) {
    if (local_server_) {
        local_server_->write_bucket(node_id, data);
    } else if (remote_client_) {
        remote_client_->write_bucket(node_id, data);
    } else {
        throw std::runtime_error("No server connection available");
    }
}

std::vector<Byte> Oram::generate_initial_bucket() {
    // Create a bucket with all dummy blocks
    Bucket bucket(params_.Z, params_.block_size);
    return codec_->encode(bucket);
}

} // namespace oram
