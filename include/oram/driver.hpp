#ifndef DRIVER_HPP
#define DRIVER_HPP

#include "oram.hpp"
#include <stdexcept>
#include <vector>

template <class T, class U = T>
class Driver {
public:
  virtual ~Driver() = default;

  virtual U read_one(size_t pos) = 0;
  virtual U exchange_one(size_t pos, const U& value) = 0;

  virtual typename ORAM<T, U>::AccessResults execute(
      const typename ORAM<T, U>::AccessReq& req) {
    typename ORAM<T, U>::AccessResults out;
    out.results.reserve(req.ops.size());

    for (const auto& op : req.ops) {
      std::vector<size_t> positions;
      expand_positions(op.positions, positions);

      if (op.kind == ORAM<T, U>::Op::Kind::Read) {
        std::vector<U> values;
        values.reserve(positions.size());
        for (size_t pos : positions) {
          values.push_back(read_one(pos));
        }
        out.results.push_back(std::move(values));
      } else if (op.kind == ORAM<T, U>::Op::Kind::Exchange) {
        validate_values(positions, op.values);
        std::vector<U> old_values;
        old_values.reserve(positions.size());
        for (size_t i = 0; i < positions.size(); ++i) {
          old_values.push_back(exchange_one(positions[i], op.values[i]));
        }
        out.results.push_back(std::move(old_values));
      } else {
        validate_values(positions, op.values);
        for (size_t i = 0; i < positions.size(); ++i) {
          exchange_one(positions[i], op.values[i]);
        }
        out.results.push_back(std::nullopt);
      }
    }

    return out;
  }

  // Run an ORAM access operation to completion
  T run(typename ORAM<T, U>::AccessResult op) {
    op.resume();
    while (!op.done()) {
      if (!op.has_request()) {
        op.resume();
        continue;
      }

      auto req = op.take_request();
      op.provide_results(execute(req));
    }
    return op.result();
  }

protected:
  static void expand_positions(const typename ORAM<T, U>::Positions& positions,
                               std::vector<size_t>& out) {
    if (auto* vec = std::get_if<std::vector<size_t>>(&positions)) {
      out.insert(out.end(), vec->begin(), vec->end());
      return;
    }

    auto range = std::get<typename ORAM<T, U>::Range>(positions);
    if (range.count == 0) return;
    if (range.stride == 0) throw std::invalid_argument("Driver: range stride cannot be zero");

    out.reserve(out.size() + range.count);
    size_t pos = range.start;
    for (size_t i = 0; i < range.count; ++i) {
      out.push_back(pos);
      pos += range.stride;
    }
  }

  static void validate_values(const std::vector<size_t>& positions,
                              const std::vector<U>& values) {
    if (positions.size() != values.size()) {
      throw std::invalid_argument("Driver: positions and values size mismatch");
    }
  }
};

#endif // DRIVER_HPP
