#include "oram/bucket_codec.h"
#include <cstring>
#include <stdexcept>

namespace oram {

BucketCodec::BucketCodec(Aead& aead, size_t block_size, size_t slots_per_bucket)
    : aead_(aead)
    , block_size_(block_size)
    , slots_per_bucket_(slots_per_bucket)
{
    // Each slot: block_id (8) + leaf (8) + data (block_size)
    slot_size_ = sizeof(BlockId) + sizeof(LeafId) + block_size_;
    plaintext_size_ = slots_per_bucket_ * slot_size_;
    encrypted_size_ = Aead::ciphertext_size(plaintext_size_);
}

std::vector<Byte> BucketCodec::encode(const Bucket& bucket) {
    // Ensure bucket has correct number of slots (pad if necessary)
    Bucket b = bucket;
    while (b.slots.size() < slots_per_bucket_) {
        b.slots.push_back(Block::dummy(block_size_));
    }

    if (b.slots.size() > slots_per_bucket_) {
        throw std::runtime_error("Bucket has too many slots");
    }

    // Serialize to plaintext buffer
    std::vector<Byte> plaintext(plaintext_size_);
    serialize_bucket(b, plaintext);

    // Encrypt
    return aead_.encrypt(plaintext);
}

Bucket BucketCodec::decode(std::span<const Byte> ciphertext) {
    if (ciphertext.size() != encrypted_size_) {
        throw std::runtime_error("Invalid ciphertext size for bucket");
    }

    // Decrypt
    std::vector<Byte> plaintext = aead_.decrypt(ciphertext);

    // Deserialize
    return deserialize_bucket(plaintext);
}

void BucketCodec::serialize_bucket(const Bucket& bucket, std::span<Byte> out) {
    if (out.size() < plaintext_size_) {
        throw std::runtime_error("Output buffer too small for bucket serialization");
    }

    Byte* ptr = out.data();
    for (size_t i = 0; i < slots_per_bucket_; i++) {
        const Block& block = bucket.slots[i];

        // Write block_id
        std::memcpy(ptr, &block.block_id, sizeof(BlockId));
        ptr += sizeof(BlockId);

        // Write leaf
        std::memcpy(ptr, &block.leaf, sizeof(LeafId));
        ptr += sizeof(LeafId);

        // Write data (pad with zeros if needed, error if too large)
        if (block.data.size() > block_size_) {
            throw std::runtime_error("Block data size exceeds configured block_size");
        } else if (block.data.size() == block_size_) {
            std::memcpy(ptr, block.data.data(), block_size_);
        } else {
            std::memcpy(ptr, block.data.data(), block.data.size());
            std::memset(ptr + block.data.size(), 0, block_size_ - block.data.size());
        }
        ptr += block_size_;
    }
}

Bucket BucketCodec::deserialize_bucket(std::span<const Byte> in) {
    if (in.size() < plaintext_size_) {
        throw std::runtime_error("Input buffer too small for bucket deserialization");
    }

    Bucket bucket;
    bucket.slots.reserve(slots_per_bucket_);

    const Byte* ptr = in.data();
    for (size_t i = 0; i < slots_per_bucket_; i++) {
        Block block;

        // Read block_id
        std::memcpy(&block.block_id, ptr, sizeof(BlockId));
        ptr += sizeof(BlockId);

        // Read leaf
        std::memcpy(&block.leaf, ptr, sizeof(LeafId));
        ptr += sizeof(LeafId);

        // Read data
        block.data.resize(block_size_);
        std::memcpy(block.data.data(), ptr, block_size_);
        ptr += block_size_;

        bucket.slots.push_back(std::move(block));
    }

    return bucket;
}

void BucketCodec::serialize_slot(const Block& block, std::span<Byte> out) {
    if (out.size() < slot_size_) {
        throw std::runtime_error("Output buffer too small for slot serialization");
    }

    Byte* ptr = out.data();

    // Write block_id
    std::memcpy(ptr, &block.block_id, sizeof(BlockId));
    ptr += sizeof(BlockId);

    // Write leaf
    std::memcpy(ptr, &block.leaf, sizeof(LeafId));
    ptr += sizeof(LeafId);

    // Write data (pad with zeros if needed, error if too large)
    if (block.data.size() > block_size_) {
        throw std::runtime_error("Block data size exceeds configured block_size");
    } else if (block.data.size() == block_size_) {
        std::memcpy(ptr, block.data.data(), block_size_);
    } else {
        std::memcpy(ptr, block.data.data(), block.data.size());
        std::memset(ptr + block.data.size(), 0, block_size_ - block.data.size());
    }
}

Block BucketCodec::deserialize_slot(std::span<const Byte> in) {
    if (in.size() < slot_size_) {
        throw std::runtime_error("Input buffer too small for slot deserialization");
    }

    Block block;
    const Byte* ptr = in.data();

    // Read block_id
    std::memcpy(&block.block_id, ptr, sizeof(BlockId));
    ptr += sizeof(BlockId);

    // Read leaf
    std::memcpy(&block.leaf, ptr, sizeof(LeafId));
    ptr += sizeof(LeafId);

    // Read data
    block.data.resize(block_size_);
    std::memcpy(block.data.data(), ptr, block_size_);

    return block;
}

std::vector<Byte> BucketCodec::encode_slot(const Block& block) {
    // Serialize to plaintext buffer
    std::vector<Byte> plaintext(slot_size_);
    serialize_slot(block, plaintext);

    // Encrypt
    return aead_.encrypt(plaintext);
}

Block BucketCodec::decode_slot(std::span<const Byte> ciphertext) {
    size_t expected_size = Aead::ciphertext_size(slot_size_);
    if (ciphertext.size() != expected_size) {
        throw std::runtime_error("Invalid ciphertext size for slot");
    }

    // Decrypt
    std::vector<Byte> plaintext = aead_.decrypt(ciphertext);

    // Deserialize
    return deserialize_slot(plaintext);
}

size_t BucketCodec::encrypted_slot_size() const {
    return Aead::ciphertext_size(slot_size_);
}

std::vector<Byte> BucketCodec::encode_bucket_slotwise(const Bucket& bucket) {
    // Ensure bucket has correct number of slots (pad if necessary)
    Bucket b = bucket;
    while (b.slots.size() < slots_per_bucket_) {
        b.slots.push_back(Block::dummy(block_size_));
    }

    if (b.slots.size() > slots_per_bucket_) {
        throw std::runtime_error("Bucket has too many slots");
    }

    // Encrypt each slot individually and concatenate
    size_t encrypted_slot_sz = encrypted_slot_size();
    std::vector<Byte> result;
    result.reserve(encrypted_slot_sz * slots_per_bucket_);

    for (const auto& block : b.slots) {
        std::vector<Byte> encrypted_slot = encode_slot(block);
        result.insert(result.end(), encrypted_slot.begin(), encrypted_slot.end());
    }

    return result;
}

Bucket BucketCodec::decode_bucket_slotwise(std::span<const Byte> ciphertext) {
    size_t encrypted_slot_sz = encrypted_slot_size();
    size_t expected_size = encrypted_slot_sz * slots_per_bucket_;

    if (ciphertext.size() != expected_size) {
        throw std::runtime_error("Invalid ciphertext size for slotwise bucket");
    }

    Bucket bucket;
    bucket.slots.reserve(slots_per_bucket_);

    const Byte* ptr = ciphertext.data();
    for (size_t i = 0; i < slots_per_bucket_; i++) {
        std::span<const Byte> slot_ciphertext(ptr, encrypted_slot_sz);
        Block block = decode_slot(slot_ciphertext);
        bucket.slots.push_back(std::move(block));
        ptr += encrypted_slot_sz;
    }

    return bucket;
}

} // namespace oram
