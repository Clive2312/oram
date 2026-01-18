#pragma once

#include "common.h"
#include <array>
#include <stdexcept>
#include <memory>

// Forward declarations for OpenSSL types
struct evp_cipher_ctx_st;
typedef struct evp_cipher_ctx_st EVP_CIPHER_CTX;

namespace oram {

/**
 * AEAD encryption constants for AES-256-GCM.
 */
namespace crypto {
    constexpr size_t KEY_SIZE = 32;       // AES-256
    constexpr size_t IV_SIZE = 12;        // GCM recommended IV size
    constexpr size_t TAG_SIZE = 16;       // GCM authentication tag
    constexpr size_t OVERHEAD = IV_SIZE + TAG_SIZE;  // Total overhead per encryption
}

/**
 * AEAD key for AES-256-GCM encryption.
 */
struct AeadKey {
    std::array<Byte, crypto::KEY_SIZE> bytes;

    AeadKey() { bytes.fill(0); }

    explicit AeadKey(std::span<const Byte> key_data) {
        if (key_data.size() != crypto::KEY_SIZE) {
            throw std::invalid_argument("AEAD key must be 32 bytes");
        }
        std::memcpy(bytes.data(), key_data.data(), crypto::KEY_SIZE);
    }

    // Generate a random key
    static AeadKey generate();

    // Create from hex string (for testing)
    static AeadKey from_hex(const char* hex);
};

/**
 * AEAD nonce for AES-256-GCM.
 * Must NEVER be reused with the same key.
 */
struct AeadNonce {
    std::array<Byte, crypto::IV_SIZE> bytes;

    AeadNonce() { bytes.fill(0); }

    // Generate a random nonce
    static AeadNonce generate();

    // Create from counter (for deterministic testing)
    static AeadNonce from_counter(uint64_t counter);
};

/**
 * AEAD (Authenticated Encryption with Associated Data) wrapper using AES-256-GCM.
 *
 * Provides:
 * - Confidentiality: Data is encrypted
 * - Integrity: Tampering is detected via authentication tag
 * - Authenticity: Data origin is verified
 *
 * Format of encrypted data: [IV (12 bytes)][ciphertext][tag (16 bytes)]
 *
 * Security requirements:
 * - Never reuse a nonce with the same key
 * - Each bucket encryption must use a fresh random nonce
 */
class Aead {
public:
    /**
     * Construct AEAD instance with the given key.
     * @param key The 256-bit encryption key
     */
    explicit Aead(const AeadKey& key);

    ~Aead();

    // Non-copyable
    Aead(const Aead&) = delete;
    Aead& operator=(const Aead&) = delete;

    // Movable
    Aead(Aead&& other) noexcept;
    Aead& operator=(Aead&& other) noexcept;

    /**
     * Encrypt plaintext with AEAD.
     *
     * @param plaintext The data to encrypt
     * @param aad Optional associated data (authenticated but not encrypted)
     * @return Ciphertext with prepended IV and appended tag
     */
    std::vector<Byte> encrypt(
        std::span<const Byte> plaintext,
        std::span<const Byte> aad = {}
    );

    /**
     * Encrypt plaintext with a specific nonce (for deterministic testing).
     *
     * WARNING: Only use this for deterministic tests. In production,
     * always use the version that generates random nonces.
     *
     * @param plaintext The data to encrypt
     * @param nonce The nonce to use (MUST NOT BE REUSED)
     * @param aad Optional associated data
     * @return Ciphertext with prepended IV and appended tag
     */
    std::vector<Byte> encrypt_with_nonce(
        std::span<const Byte> plaintext,
        const AeadNonce& nonce,
        std::span<const Byte> aad = {}
    );

    /**
     * Decrypt ciphertext with AEAD.
     *
     * @param ciphertext The encrypted data (IV + ciphertext + tag)
     * @param aad Optional associated data (must match what was used during encryption)
     * @return Decrypted plaintext
     * @throws std::runtime_error if authentication fails (tampering detected)
     */
    std::vector<Byte> decrypt(
        std::span<const Byte> ciphertext,
        std::span<const Byte> aad = {}
    );

    /**
     * Compute the ciphertext size for a given plaintext size.
     * @param plaintext_size Size of plaintext in bytes
     * @return Size of ciphertext including IV and tag
     */
    static size_t ciphertext_size(size_t plaintext_size) {
        return plaintext_size + crypto::OVERHEAD;
    }

    /**
     * Compute the plaintext size for a given ciphertext size.
     * @param ciphertext_size Size of ciphertext including IV and tag
     * @return Size of original plaintext
     */
    static size_t plaintext_size(size_t ciphertext_size) {
        if (ciphertext_size < crypto::OVERHEAD) {
            throw std::invalid_argument("Ciphertext too short");
        }
        return ciphertext_size - crypto::OVERHEAD;
    }

private:
    AeadKey key_;
};

/**
 * Cryptographically secure random number generator.
 * Uses OpenSSL's RAND_bytes under the hood.
 */
class SecureRng {
public:
    /**
     * Generate random bytes.
     * @param out Buffer to fill with random bytes
     */
    static void fill(std::span<Byte> out);

    /**
     * Generate a random 64-bit integer.
     * @return Random uint64_t
     */
    static uint64_t random_u64();

    /**
     * Generate a random integer in [0, max).
     * @param max Upper bound (exclusive)
     * @return Random integer in range
     */
    static uint64_t random_range(uint64_t max);
};

} // namespace oram
