#ifndef ORAM_ENCRYPTION_ENCRYPTOR_HPP
#define ORAM_ENCRYPTION_ENCRYPTOR_HPP

#include <concepts>
#include <vector>

// Concept for Encryptor types.
// All encryptors must define a secret_key_type.
template <typename E>
concept Encryptor = requires {
  typename E::secret_key_type;
} && requires(const E& enc,
              const typename E::secret_key_type& key,
              const std::vector<unsigned char>& data) {
  { enc.encrypt(key, data) } -> std::same_as<std::vector<unsigned char>>;
  { enc.decrypt(key, data) } -> std::same_as<std::vector<unsigned char>>;
};

#endif // ORAM_ENCRYPTION_ENCRYPTOR_HPP
