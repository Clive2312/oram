#include "oram/crypto.h"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <stdexcept>
#include <sstream>
#include <iomanip>

namespace oram {

namespace {

void throw_openssl_error(const char* context) {
    unsigned long err = ERR_get_error();
    char buf[256];
    ERR_error_string_n(err, buf, sizeof(buf));
    throw std::runtime_error(std::string(context) + ": " + buf);
}

} // anonymous namespace

// AeadKey implementation

AeadKey AeadKey::generate() {
    AeadKey key;
    if (RAND_bytes(key.bytes.data(), crypto::KEY_SIZE) != 1) {
        throw_openssl_error("Failed to generate random key");
    }
    return key;
}

// AeadNonce implementation
AeadNonce AeadNonce::generate() {
    AeadNonce nonce;
    if (RAND_bytes(nonce.bytes.data(), crypto::IV_SIZE) != 1) {
        throw_openssl_error("Failed to generate random nonce");
    }
    return nonce;
}

AeadNonce AeadNonce::from_counter(uint64_t counter) {
    AeadNonce nonce;
    nonce.bytes.fill(0);
    // Store counter in big-endian at the end of nonce
    for (int i = 7; i >= 0; i--) {
        nonce.bytes[crypto::IV_SIZE - 8 + i] = static_cast<Byte>(counter & 0xFF);
        counter >>= 8;
    }
    return nonce;
}

// Aead implementation
Aead::Aead(const AeadKey& key) : key_(key) {}

Aead::~Aead() = default;

Aead::Aead(Aead&& other) noexcept : key_(std::move(other.key_)) {}

Aead& Aead::operator=(Aead&& other) noexcept {
    if (this != &other) {
        key_ = std::move(other.key_);
    }
    return *this;
}

std::vector<Byte> Aead::encrypt(
    std::span<const Byte> plaintext,
    std::span<const Byte> aad
) {
    return encrypt_with_nonce(plaintext, AeadNonce::generate(), aad);
}

std::vector<Byte> Aead::encrypt_with_nonce(
    std::span<const Byte> plaintext,
    const AeadNonce& nonce,
    std::span<const Byte> aad
) {
    // Output format: [IV][ciphertext][tag]
    std::vector<Byte> output(ciphertext_size(plaintext.size()));

    // Copy IV to output
    std::memcpy(output.data(), nonce.bytes.data(), crypto::IV_SIZE);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        throw_openssl_error("Failed to create cipher context");
    }

    try {
        // Initialize encryption
        if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
            throw_openssl_error("EVP_EncryptInit_ex failed");
        }

        // Set IV length
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, crypto::IV_SIZE, nullptr) != 1) {
            throw_openssl_error("Failed to set IV length");
        }

        // Set key and IV
        if (EVP_EncryptInit_ex(ctx, nullptr, nullptr, key_.bytes.data(), nonce.bytes.data()) != 1) {
            throw_openssl_error("Failed to set key and IV");
        }

        int len;

        // Process AAD if provided
        if (!aad.empty()) {
            if (EVP_EncryptUpdate(ctx, nullptr, &len, aad.data(), static_cast<int>(aad.size())) != 1) {
                throw_openssl_error("Failed to process AAD");
            }
        }

        // Encrypt plaintext
        Byte* ciphertext_ptr = output.data() + crypto::IV_SIZE;
        if (EVP_EncryptUpdate(ctx, ciphertext_ptr, &len, plaintext.data(), static_cast<int>(plaintext.size())) != 1) {
            throw_openssl_error("EVP_EncryptUpdate failed");
        }
        int ciphertext_len = len;

        // Finalize encryption
        if (EVP_EncryptFinal_ex(ctx, ciphertext_ptr + len, &len) != 1) {
            throw_openssl_error("EVP_EncryptFinal_ex failed");
        }
        ciphertext_len += len;

        // Get authentication tag
        Byte* tag_ptr = output.data() + crypto::IV_SIZE + plaintext.size();
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, crypto::TAG_SIZE, tag_ptr) != 1) {
            throw_openssl_error("Failed to get auth tag");
        }

        EVP_CIPHER_CTX_free(ctx);
        return output;

    } catch (...) {
        EVP_CIPHER_CTX_free(ctx);
        throw;
    }
}

std::vector<Byte> Aead::decrypt(
    std::span<const Byte> ciphertext,
    std::span<const Byte> aad
) {
    if (ciphertext.size() < crypto::OVERHEAD) {
        throw std::runtime_error("Ciphertext too short");
    }

    size_t plaintext_len = ciphertext.size() - crypto::OVERHEAD;
    std::vector<Byte> plaintext(plaintext_len);

    // Extract IV, encrypted data, and tag
    const Byte* iv_ptr = ciphertext.data();
    const Byte* encrypted_ptr = ciphertext.data() + crypto::IV_SIZE;
    const Byte* tag_ptr = ciphertext.data() + crypto::IV_SIZE + plaintext_len;

    // Copy tag to a mutable buffer (required by OpenSSL)
    std::array<Byte, crypto::TAG_SIZE> tag_copy;
    std::memcpy(tag_copy.data(), tag_ptr, crypto::TAG_SIZE);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        throw_openssl_error("Failed to create cipher context");
    }

    // Initialize decryption
    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw_openssl_error("EVP_DecryptInit_ex failed");
    }

    // Set IV length
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, crypto::IV_SIZE, nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw_openssl_error("Failed to set IV length");
    }

    // Set key and IV
    if (EVP_DecryptInit_ex(ctx, nullptr, nullptr, key_.bytes.data(), iv_ptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw_openssl_error("Failed to set key and IV");
    }

    int len;

    // Process AAD if provided
    if (!aad.empty()) {
        if (EVP_DecryptUpdate(ctx, nullptr, &len, aad.data(), static_cast<int>(aad.size())) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw_openssl_error("Failed to process AAD");
        }
    }

    // Decrypt ciphertext
    if (EVP_DecryptUpdate(ctx, plaintext.data(), &len, encrypted_ptr, static_cast<int>(plaintext_len)) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw_openssl_error("EVP_DecryptUpdate failed");
    }

    // Set expected tag (must be done before DecryptFinal)
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, crypto::TAG_SIZE, tag_copy.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw_openssl_error("Failed to set auth tag");
    }

    // Finalize and verify tag
    int ret = EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &len);
    EVP_CIPHER_CTX_free(ctx);

    if (ret <= 0) {
        throw std::runtime_error("AEAD authentication failed - ciphertext may have been tampered with");
    }

    return plaintext;
}

// SecureRng implementation

void SecureRng::fill(std::span<Byte> out) {
    if (RAND_bytes(out.data(), static_cast<int>(out.size())) != 1) {
        throw_openssl_error("RAND_bytes failed");
    }
}

uint64_t SecureRng::random_u64() {
    uint64_t value;
    fill(std::span<Byte>(reinterpret_cast<Byte*>(&value), sizeof(value)));
    return value;
}

uint64_t SecureRng::random_range(uint64_t max) {
    if (max == 0) return 0;
    if (max == 1) return 0;

    // Use rejection sampling to avoid modulo bias
    uint64_t mask = max - 1;
    mask |= mask >> 1;
    mask |= mask >> 2;
    mask |= mask >> 4;
    mask |= mask >> 8;
    mask |= mask >> 16;
    mask |= mask >> 32;

    uint64_t value;
    do {
        value = random_u64() & mask;
    } while (value >= max);

    return value;
}

} // namespace oram
