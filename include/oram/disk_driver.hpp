#ifndef DISK_DRIVER_HPP
#define DISK_DRIVER_HPP

#include "driver.hpp"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

template <class T, class U = T>
class DiskDriver : public Driver<T, U> {
public:
  explicit DiskDriver(const std::filesystem::path& root_folder, U default_value = U{})
    : root_(root_folder), default_value_(default_value) {
    std::filesystem::create_directories(root_);
  }

  U read_one(size_t pos) override {
    return read_file(pos);
  }

  U exchange_one(size_t pos, const U& value) override {
    U old = read_file(pos);
    write_file(pos, value);
    return old;
  }

  const std::filesystem::path& root() const { return root_; }

private:
  std::filesystem::path file_path(size_t pos) const {
    return root_ / std::to_string(pos);
  }

  U read_file(size_t pos) {
    auto path = file_path(pos);
    if (!std::filesystem::exists(path)) {
      return default_value_;
    }
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
      throw std::runtime_error("DiskDriver: failed to open file for reading: " + path.string());
    }
    U value;
    ifs.read(reinterpret_cast<char*>(&value), sizeof(U));
    if (!ifs) {
      throw std::runtime_error("DiskDriver: failed to read from file: " + path.string());
    }
    return value;
  }

  void write_file(size_t pos, const U& value) {
    auto path = file_path(pos);
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs) {
      throw std::runtime_error("DiskDriver: failed to open file for writing: " + path.string());
    }
    ofs.write(reinterpret_cast<const char*>(&value), sizeof(U));
    if (!ofs) {
      throw std::runtime_error("DiskDriver: failed to write to file: " + path.string());
    }
  }

  std::filesystem::path root_;
  U default_value_;
};

#endif // DISK_DRIVER_HPP
