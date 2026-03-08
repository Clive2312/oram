#ifndef ORAM_HPP
#define ORAM_HPP

#include "oram_lock.hpp"
#include <coroutine>
#include <exception>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

template <class T, class U = T>
class ORAM {
public:
  struct Range {
    size_t start;
    size_t count;
    size_t stride;
  };
  using Positions = std::variant<std::vector<size_t>, Range>;

  struct Op {
    enum class Kind { Read, Write, Exchange };
    Kind kind;
    Positions positions;
    std::vector<U> values;
  };

  // OpList is a lightweight chainable container for operator+ syntax.
  // It gets converted into an AccessReq when yielded.
  struct OpList {
    std::vector<Op> ops;
  };

  // AccessReq is the concrete request type yielded to the driver.
  struct AccessReq {
    std::vector<Op> ops;
  };

  struct AccessResults {
    std::vector<std::optional<std::vector<U>>> results;
  };

  using YieldValue = std::variant<AccessReq, LockReq, UnlockReq>;

  static Op read(Positions positions) { return make_read(std::move(positions)); }
  static Op write(Positions positions, std::vector<U> values) {
    return make_write(std::move(positions), std::move(values));
  }
  static Op exchange(Positions positions, std::vector<U> values) {
    return make_exchange(std::move(positions), std::move(values));
  }

  friend OpList operator+(Op lhs, Op rhs) {
    OpList out;
    out.ops.push_back(std::move(lhs));
    out.ops.push_back(std::move(rhs));
    return out;
  }
  friend OpList operator+(OpList lhs, Op rhs) {
    lhs.ops.push_back(std::move(rhs));
    return lhs;
  }
  friend OpList operator+(Op lhs, OpList rhs) {
    rhs.ops.insert(rhs.ops.begin(), std::move(lhs));
    return rhs;
  }
  friend OpList operator+(OpList lhs, OpList rhs) {
    for (auto& op : rhs.ops) lhs.ops.push_back(std::move(op));
    return lhs;
  }

  class AccessBuilder {
  public:
    AccessBuilder& read(Positions positions) {
      ops_.push_back(ORAM::read(std::move(positions)));
      return *this;
    }
    AccessBuilder& write(Positions positions, std::vector<U> values) {
      ops_.push_back(ORAM::write(std::move(positions), std::move(values)));
      return *this;
    }
    AccessBuilder& exchange(Positions positions, std::vector<U> values) {
      ops_.push_back(ORAM::exchange(std::move(positions), std::move(values)));
      return *this;
    }

    AccessReq build() { return AccessReq{std::move(ops_)}; }
    OpList build_list() { return OpList{std::move(ops_)}; }

  private:
    std::vector<Op> ops_;
  };

  class AccessResult {
  public:
    struct promise_type {
      std::optional<YieldValue> pending_yield;

      std::optional<AccessResults> pending_results;
      bool waiting_for_results = false;

      std::optional<T> final_value;
      std::exception_ptr eptr;

      AccessResult get_return_object() {
        return AccessResult{std::coroutine_handle<promise_type>::from_promise(*this)};
      }
      std::suspend_always initial_suspend() noexcept { return {}; }
      std::suspend_always final_suspend() noexcept { return {}; }
      void unhandled_exception() noexcept { eptr = std::current_exception(); }
      void return_value(T v) noexcept { final_value = std::move(v); }

      // Unified awaiter that handles both read and write requests.
      // For reads, await_resume returns the read results.
      // For writes, await_resume returns an empty vector (caller ignores it).
      struct RequestAwaiter {
        promise_type* p;

        bool await_ready() const noexcept { return false; }

        bool await_suspend(std::coroutine_handle<>) noexcept {
          p->waiting_for_results = true;
          return true;
        }

        AccessResults await_resume() {
          if (!p->pending_results) throw std::runtime_error("resumed without results");
          auto out = std::move(*p->pending_results);
          p->pending_results.reset();
          return out;
        }
      };

      // Yielding a storage request returns an awaiter for results.
      RequestAwaiter yield_value(AccessReq req) noexcept {
        pending_yield = std::move(req);
        return RequestAwaiter{this};
      }

      std::suspend_always yield_value(LockReq req) noexcept {
        pending_yield = std::move(req);
        return {};
      }

      std::suspend_always yield_value(UnlockReq req) noexcept {
        pending_yield = std::move(req);
        return {};
      }
    };

    using handle_type = std::coroutine_handle<promise_type>;

    explicit AccessResult(handle_type h) : h_(h) {}
    AccessResult(AccessResult&& o) noexcept : h_(std::exchange(o.h_, {})) {}
    AccessResult& operator=(AccessResult&& o) noexcept {
      if (this != &o) {
        if (h_) h_.destroy();
        h_ = std::exchange(o.h_, {});
      }
      return *this;
    }
    AccessResult(const AccessResult&) = delete;
    AccessResult& operator=(const AccessResult&) = delete;
    ~AccessResult() { if (h_) h_.destroy(); }

    void resume() {
      if (!h_ || h_.done()) return;
      h_.resume();
    }
    bool done() const { return !h_ || h_.done(); }

    bool has_request() const {
      return h_ && h_.promise().pending_yield.has_value() &&
             std::holds_alternative<AccessReq>(*h_.promise().pending_yield);
    }

    AccessReq take_request() {
      if (!h_) throw std::runtime_error("no coroutine handle");
      auto& p = h_.promise();
      if (!p.pending_yield) throw std::runtime_error("no pending request");
      if (!std::holds_alternative<AccessReq>(*p.pending_yield)) {
        throw std::runtime_error("pending yield is not an AccessReq");
      }
      auto req = std::get<AccessReq>(std::move(*p.pending_yield));
      p.pending_yield.reset();
      return req;
    }

    bool has_yield() const {
      return h_ && h_.promise().pending_yield.has_value();
    }

    YieldValue take_yield() {
      if (!h_) throw std::runtime_error("no coroutine handle");
      auto& p = h_.promise();
      if (!p.pending_yield) throw std::runtime_error("no pending yield");
      auto out = std::move(*p.pending_yield);
      p.pending_yield.reset();
      return out;
    }

    // For resuming after satisfying access requests
    void provide_results(AccessResults results) {
      if (!h_) throw std::runtime_error("no coroutine handle");
      auto& p = h_.promise();
      if (!p.waiting_for_results) throw std::runtime_error("not waiting for results");
      p.pending_results = std::move(results);
      p.waiting_for_results = false;
      h_.resume();
    }

    T result() {
      if (!h_) throw std::runtime_error("no coroutine handle");
      auto& p = h_.promise();
      if (!h_.done()) throw std::runtime_error("result not ready");
      if (p.eptr) std::rethrow_exception(p.eptr);
      if (!p.final_value) throw std::runtime_error("no final value");
      return *p.final_value;
    }

  private:
    handle_type h_;
  };

protected:
  // Helpers for subclasses: build requests and ops.
  static Op make_read(Positions positions) {
    return Op{Op::Kind::Read, std::move(positions), {}};
  }
  static Op make_write(Positions positions, std::vector<U> values) {
    return Op{Op::Kind::Write, std::move(positions), std::move(values)};
  }
  static Op make_exchange(Positions positions, std::vector<U> values) {
    return Op{Op::Kind::Exchange, std::move(positions), std::move(values)};
  }
  AccessReq store_access(Op op) {
    return AccessReq{std::vector<Op>{std::move(op)}};
  }
  AccessReq store_access(OpList ops) {
    return AccessReq{std::move(ops.ops)};
  }
  AccessReq store_access(std::vector<Op> ops) {
    return AccessReq{std::move(ops)};
  }
  AccessReq store_access(AccessReq req) {
    return req;
  }

  // Subclasses implement just this.
  virtual AccessResult access_impl(size_t pos, T val) = 0;

public:
  virtual ~ORAM() = default;

  // Logical and physical storage sizes.
  virtual size_t size() const = 0;
  // physical_size is an upper bound on actual storage needed so drivers
  // can allocate enough untrusted space. Tighter bounds reduce overhead.
  virtual size_t physical_size() const = 0;
  // Maximum number of blocks that may reside in the ORAM's stash.
  // Defaults to 0 (no stash). Subclasses like PathORAM override this.
  virtual size_t max_stash_size() const { return 0; }

  // Non-virtual public entrypoint.
  AccessResult access(size_t pos, T val) { return access_impl(pos, std::move(val)); }
};

// "DSL" macros for subclasses.
// co_yield already awaits the result - yield_value returns an awaiter.
// Use extra parens around args with commas in braced initializers.
#define STORE_READ(positions_vec) \
  (*((co_yield this->store_access(this->make_read(positions_vec))).results[0]))

#define STORE_WRITE(positions_vec, values_vec) \
  (void)(co_yield this->store_access(this->make_write(positions_vec, values_vec)))

#define STORE_EXCHANGE(positions_vec, values_vec) \
  (*((co_yield this->store_access(this->make_exchange(positions_vec, values_vec))).results[0]))

#define STORE_ACCESS(ops_or_req) \
  (co_yield this->store_access(ops_or_req))

#define LOCK_READ(lock_obj) \
  (void)(co_yield LockReq{&(lock_obj), LockMode::Read})

#define LOCK_EXCLUSIVE(lock_obj) \
  (void)(co_yield LockReq{&(lock_obj), LockMode::Exclusive})

#define UNLOCK(lock_obj) \
  (void)(co_yield UnlockReq{&(lock_obj)})

#endif // ORAM_HPP
