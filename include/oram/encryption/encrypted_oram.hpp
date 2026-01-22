#ifndef ORAM_ENCRYPTION_ENCRYPTED_ORAM_HPP
#define ORAM_ENCRYPTION_ENCRYPTED_ORAM_HPP

#include "../oram.hpp"
#include "encryptor.hpp"
#include <cstring>
#include <type_traits>
#include <vector>

// EncryptedORAM: Wraps an inner ORAM and intercepts access requests to/from the driver.
// - Encrypts values in write/exchange ops before they reach the driver
// - Decrypts values in read results before passing them back to the inner ORAM
// The inner ORAM works with plaintext U; the driver sees only ciphertext.
//
// T: logical type seen by the user
// U: block type used by the inner ORAM (plaintext)
// E: encryptor type (must satisfy Encryptor concept)
template <typename T, typename U, Encryptor E>
class EncryptedORAM : public ORAM<T, std::vector<unsigned char>> {
public:
  using Base = ORAM<T, std::vector<unsigned char>>;
  using Bytes = std::vector<unsigned char>;
  using Inner = ORAM<T, U>;
  using secret_key_type = typename E::secret_key_type;

  EncryptedORAM(Inner& inner, E encryptor, secret_key_type key)
      : inner_(inner),
        encryptor_(std::move(encryptor)),
        key_(std::move(key)) {}

  size_t size() const override { return inner_.size(); }
  size_t physical_size() const override { return inner_.physical_size(); }

protected:
  typename Base::AccessResult access_impl(size_t pos, T val) override {
    // Start inner ORAM access
    auto inner_coro = inner_.access(pos, std::move(val));

    // Drive the inner coroutine, intercepting all yields
    inner_coro.resume();
    while (!inner_coro.done()) {
      if (inner_coro.has_request()) {
        auto inner_req = inner_coro.take_request();

        // Encrypt outgoing values in the request
        auto encrypted_req = encrypt_request(std::move(inner_req));

        // Yield to driver with encrypted request
        auto encrypted_results = co_yield std::move(encrypted_req);

        // Decrypt incoming values in the results
        auto decrypted_results = decrypt_results(std::move(encrypted_results));

        inner_coro.provide_results(std::move(decrypted_results));
      }
    }

    co_return inner_coro.result();
  }

private:
  // Convert Inner::Positions to Base::Positions.
  // std::vector<size_t> passes through; Range fields are copied.
  static typename Base::Positions convert_positions(typename Inner::Positions pos) {
    if (std::holds_alternative<std::vector<size_t>>(pos)) {
      return std::get<std::vector<size_t>>(std::move(pos));
    } else {
      auto& r = std::get<typename Inner::Range>(pos);
      return typename Base::Range{r.start, r.count, r.stride};
    }
  }

  // Encrypt values in write/exchange ops before sending to driver.
  typename Base::AccessReq encrypt_request(typename Inner::AccessReq inner_req) {
    typename Base::AccessReq encrypted_req;
    encrypted_req.ops.reserve(inner_req.ops.size());

    for (auto& op : inner_req.ops) {
      typename Base::Op encrypted_op;
      encrypted_op.kind = static_cast<typename Base::Op::Kind>(op.kind);
      encrypted_op.positions = convert_positions(std::move(op.positions));

      // Encrypt values for write and exchange operations
      if (op.kind == Inner::Op::Kind::Write ||
          op.kind == Inner::Op::Kind::Exchange) {
        encrypted_op.values.reserve(op.values.size());
        for (auto& plaintext : op.values) {
          encrypted_op.values.push_back(
              encryptor_.encrypt(key_, serialize(plaintext)));
        }
      }

      encrypted_req.ops.push_back(std::move(encrypted_op));
    }

    return encrypted_req;
  }

  // Decrypt values in read results before passing back to inner ORAM.
  typename Inner::AccessResults decrypt_results(typename Base::AccessResults encrypted_results) {
    typename Inner::AccessResults decrypted_results;
    decrypted_results.results.reserve(encrypted_results.results.size());

    for (auto& opt_values : encrypted_results.results) {
      if (opt_values) {
        std::vector<U> decrypted_values;
        decrypted_values.reserve(opt_values->size());
        for (auto& ciphertext : *opt_values) {
          decrypted_values.push_back(
              deserialize(encryptor_.decrypt(key_, ciphertext)));
        }
        decrypted_results.results.push_back(std::move(decrypted_values));
      } else {
        decrypted_results.results.push_back(std::nullopt);
      }
    }

    return decrypted_results;
  }

  // Serialization: convert U to bytes for encryption.
  static Bytes serialize(const U& val) {
    if constexpr (std::is_same_v<U, Bytes>) {
      return val;
    } else {
      static_assert(std::is_trivially_copyable_v<U>,
                    "U must be trivially copyable or std::vector<unsigned char>");
      Bytes bytes(sizeof(U));
      std::memcpy(bytes.data(), &val, sizeof(U));
      return bytes;
    }
  }

  // Deserialization: convert decrypted bytes back to U.
  static U deserialize(const Bytes& bytes) {
    if constexpr (std::is_same_v<U, Bytes>) {
      return bytes;
    } else {
      static_assert(std::is_trivially_copyable_v<U>,
                    "U must be trivially copyable or std::vector<unsigned char>");
      U val;
      std::memcpy(&val, bytes.data(), sizeof(U));
      return val;
    }
  }

  Inner& inner_;
  E encryptor_;
  secret_key_type key_;
};

#endif // ORAM_ENCRYPTION_ENCRYPTED_ORAM_HPP
