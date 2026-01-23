#ifndef DRIVER_HPP
#define DRIVER_HPP

#include "oram.hpp"
#include <deque>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

template <class T, class U = T>
class Driver {
public:
  using Base = ORAM<T, U>;
  using AccessReq = typename Base::AccessReq;
  using AccessResult = typename Base::AccessResult;
  using AccessResults = typename Base::AccessResults;
  using Token = size_t;

  virtual ~Driver() = default;

  virtual U read_one(size_t pos) = 0;
  virtual U exchange_one(size_t pos, const U& value) = 0;

  virtual AccessResults execute(const AccessReq& req) {
    AccessResults out;
    out.results.reserve(req.ops.size());

    for (const auto& op : req.ops) {
      std::vector<size_t> positions;
      expand_positions(op.positions, positions);

      if (op.kind == Base::Op::Kind::Read) {
        std::vector<U> values;
        values.reserve(positions.size());
        for (size_t pos : positions) {
          values.push_back(read_one(pos));
        }
        out.results.push_back(std::move(values));
      } else if (op.kind == Base::Op::Kind::Exchange) {
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

  Token submit(AccessResult op) {
    const Token id = tasks_.size();
    tasks_.push_back(Task{std::move(op), false, std::nullopt});
    held_locks_.emplace_back();
    ready_queue_.push_back(id);
    return id;
  }

  void run() {
    while (!ready_queue_.empty()) {
      const Token id = ready_queue_.front();
      ready_queue_.pop_front();
      process_task(id);
    }
  }

  T wait(Token token) {
    while (!is_finished(token)) {
      if (ready_queue_.empty()) {
        throw std::runtime_error("Driver: task stalled waiting on locks");
      }
      run();
    }
    return take_result(token);
  }

  T do_access(AccessResult op) {
    auto token = submit(std::move(op));
    run();
    return wait(token);
  }

protected:
  static void expand_positions(const typename Base::Positions& positions,
                               std::vector<size_t>& out) {
    if (auto* vec = std::get_if<std::vector<size_t>>(&positions)) {
      out.insert(out.end(), vec->begin(), vec->end());
      return;
    }

    auto range = std::get<typename Base::Range>(positions);
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

  struct Waiter {
    Token task_id;
    LockMode mode;
  };

  struct LockState {
    size_t reader_count = 0;
    std::optional<Token> writer;
    std::deque<Waiter> waiters;
  };

  struct HeldLock {
    OramLock* lock;
    LockMode mode;
  };

  struct Task {
    AccessResult coro;
    bool finished = false;
    std::optional<T> result;
  };

  bool is_finished(Token token) const {
    if (token >= tasks_.size()) {
      throw std::out_of_range("Driver: token out of range");
    }
    return tasks_[token].finished;
  }

  T take_result(Token token) {
    if (!is_finished(token)) {
      throw std::runtime_error("Driver: task not finished");
    }
    auto& task = tasks_[token];
    if (!task.result) {
      throw std::runtime_error("Driver: missing task result");
    }
    return std::move(*task.result);
  }

  void process_task(Token id) {
    if (id >= tasks_.size()) {
      throw std::out_of_range("Driver: token out of range");
    }

    auto& task = tasks_[id];
    if (task.finished) return;

    if (!task.coro.has_yield()) {
      task.coro.resume();
    }

    if (task.coro.done()) {
      finalize_task(id);
      return;
    }

    if (!task.coro.has_yield()) {
      ready_queue_.push_back(id);
      return;
    }

    auto yielded = task.coro.take_yield();
    if (auto* req = std::get_if<AccessReq>(&yielded)) {
      auto results = execute(*req);
      task.coro.provide_results(std::move(results));
      if (task.coro.done()) {
        finalize_task(id);
      } else {
        ready_queue_.push_back(id);
      }
      return;
    }

    if (auto* lock_req = std::get_if<LockReq>(&yielded)) {
      handle_lock_req(id, *lock_req);
      return;
    }

    if (auto* unlock_req = std::get_if<UnlockReq>(&yielded)) {
      handle_unlock_req(id, *unlock_req);
      ready_queue_.push_back(id);
      return;
    }
  }

  void finalize_task(Token id) {
    auto& task = tasks_[id];
    task.finished = true;
    task.result = task.coro.result();
    release_all_locks(id);
  }

  bool holds_lock(Token id, OramLock* lock) const {
    for (const auto& held : held_locks_[id]) {
      if (held.lock == lock) return true;
    }
    return false;
  }

  LockMode remove_held_lock(Token id, OramLock* lock) {
    auto& held = held_locks_[id];
    for (size_t i = 0; i < held.size(); ++i) {
      if (held[i].lock == lock) {
        LockMode mode = held[i].mode;
        held.erase(held.begin() +
                   static_cast<std::vector<HeldLock>::difference_type>(i));
        return mode;
      }
    }
    throw std::runtime_error("Driver: unlock without lock");
  }

  void handle_lock_req(Token id, const LockReq& req) {
    if (holds_lock(id, req.lock)) {
      throw std::runtime_error("Driver: lock already held");
    }

    auto& state = locks_[req.lock];
    if (req.mode == LockMode::Read) {
      if (!state.writer && state.waiters.empty()) {
        state.reader_count++;
        held_locks_[id].push_back(HeldLock{req.lock, LockMode::Read});
        ready_queue_.push_back(id);
      } else {
        state.waiters.push_back(Waiter{id, LockMode::Read});
      }
      return;
    }

    if (!state.writer && state.reader_count == 0 && state.waiters.empty()) {
      state.writer = id;
      held_locks_[id].push_back(HeldLock{req.lock, LockMode::Exclusive});
      ready_queue_.push_back(id);
    } else {
      state.waiters.push_back(Waiter{id, LockMode::Exclusive});
    }
  }

  void handle_unlock_req(Token id, const UnlockReq& req) {
    auto it = locks_.find(req.lock);
    if (it == locks_.end()) {
      throw std::runtime_error("Driver: unlock for unknown lock");
    }

    auto& state = it->second;
    const LockMode mode = remove_held_lock(id, req.lock);
    if (mode == LockMode::Exclusive) {
      if (!state.writer || *state.writer != id) {
        throw std::runtime_error("Driver: unlock without exclusive owner");
      }
      state.writer.reset();
    } else {
      if (state.reader_count == 0) {
        throw std::runtime_error("Driver: unlock without reader");
      }
      state.reader_count--;
    }

    if (!state.writer && state.reader_count == 0) {
      grant_waiters(req.lock, state);
    }

    if (!state.writer && state.reader_count == 0 && state.waiters.empty()) {
      locks_.erase(it);
    }
  }

  void grant_waiters(OramLock* lock, LockState& state) {
    if (state.writer || state.reader_count != 0) return;
    if (state.waiters.empty()) return;

    if (state.waiters.front().mode == LockMode::Exclusive) {
      auto waiter = state.waiters.front();
      state.waiters.pop_front();
      state.writer = waiter.task_id;
      held_locks_[waiter.task_id].push_back(HeldLock{lock, LockMode::Exclusive});
      ready_queue_.push_back(waiter.task_id);
      return;
    }

    while (!state.waiters.empty() && state.waiters.front().mode == LockMode::Read) {
      auto waiter = state.waiters.front();
      state.waiters.pop_front();
      state.reader_count++;
      held_locks_[waiter.task_id].push_back(HeldLock{lock, LockMode::Read});
      ready_queue_.push_back(waiter.task_id);
    }
  }

  void release_all_locks(Token id) {
    auto held = std::move(held_locks_[id]);
    held_locks_[id].clear();
    for (const auto& entry : held) {
      auto it = locks_.find(entry.lock);
      if (it == locks_.end()) continue;

      auto& state = it->second;
      if (entry.mode == LockMode::Exclusive) {
        if (state.writer && *state.writer == id) {
          state.writer.reset();
        }
      } else {
        if (state.reader_count > 0) {
          state.reader_count--;
        }
      }

      if (!state.writer && state.reader_count == 0) {
        grant_waiters(entry.lock, state);
      }

      if (!state.writer && state.reader_count == 0 && state.waiters.empty()) {
        locks_.erase(it);
      }
    }
  }

  std::vector<Task> tasks_;
  std::vector<std::vector<HeldLock>> held_locks_;
  std::deque<Token> ready_queue_;
  std::unordered_map<OramLock*, LockState> locks_;
};

#endif // DRIVER_HPP
