#ifndef ORAM_ENCRYPTION_DUMMY_ENCRYPTOR_HPP
#define ORAM_ENCRYPTION_DUMMY_ENCRYPTOR_HPP

#include "encryptor.hpp"

// DummyEncryptor: identity transform for testing and plumbing.
// Uses an empty key type since no actual encryption is performed.
class DummyEncryptor final {
public:
  struct secret_key_type {};

  std::vector<unsigned char> encrypt(
      const secret_key_type&,
      const std::vector<unsigned char>& plaintext) const {
    return plaintext;
  }

  std::vector<unsigned char> decrypt(
      const secret_key_type&,
      const std::vector<unsigned char>& ciphertext) const {
    return ciphertext;
  }
};

static_assert(Encryptor<DummyEncryptor>);

#endif // ORAM_ENCRYPTION_DUMMY_ENCRYPTOR_HPP
