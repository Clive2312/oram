#pragma once

#include "common.h"
#include "crypto.h"
#include <vector>

namespace oram {

/**
 * Bucket codec for encoding/decoding and encrypting/decrypting buckets.
 *
 * Handles conversion between plaintext Bucket objects and encrypted byte arrays
 * that can be stored on the server.
 *
 * Encryption format per bucket:
 *   [AEAD ciphertext of serialized bucket]
 *
 * Serialized bucket format (plaintext):
 *   For each slot (Z slots for Path ORAM, Z+S for Ring ORAM):
 *     [block_id (8 bytes)][leaf (8 bytes)][data (block_size bytes)]
 */
class BucketCodec {
public:
    /**
     * Construct a bucket codec.
     *
     * @param aead Reference to AEAD encryption instance
     * @param block_size Size of each block's data in bytes
     * @param slots_per_bucket Number of slots per bucket (Z or Z+S)
     */
    BucketCodec(Aead& aead, size_t block_size, size_t slots_per_bucket);

    /**
     * Encode and encrypt a bucket.
     *
     * @param bucket The plaintext bucket to encode
     * @return Encrypted bucket data
     */
    std::vector<Byte> encode(const Bucket& bucket);

    /**
     * Decrypt and decode a bucket.
     *
     * @param ciphertext The encrypted bucket data
     * @return Decrypted bucket
     */
    Bucket decode(std::span<const Byte> ciphertext);

    /**
     * Get the encrypted bucket size.
     *
     * @return Size of encrypted bucket in bytes
     */
    size_t encrypted_bucket_size() const { return encrypted_size_; }

    /**
     * Get the plaintext bucket size.
     *
     * @return Size of plaintext serialized bucket in bytes
     */
    size_t plaintext_bucket_size() const { return plaintext_size_; }

    /**
     * Get number of slots per bucket.
     */
    size_t slots_per_bucket() const { return slots_per_bucket_; }

    /**
     * Get block size.
     */
    size_t block_size() const { return block_size_; }

private:
    Aead& aead_;
    size_t block_size_;
    size_t slots_per_bucket_;
    size_t slot_size_;       // Size of one serialized slot
    size_t plaintext_size_;  // Total plaintext bucket size
    size_t encrypted_size_;  // Total encrypted bucket size

    // Serialize a bucket to a byte buffer
    void serialize_bucket(const Bucket& bucket, std::span<Byte> out);

    // Deserialize a bucket from a byte buffer
    Bucket deserialize_bucket(std::span<const Byte> in);
};

} // namespace oram
