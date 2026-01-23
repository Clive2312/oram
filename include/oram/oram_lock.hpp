#ifndef ORAM_LOCK_HPP
#define ORAM_LOCK_HPP

enum class LockMode { Read, Exclusive };

struct OramLock {};

struct LockReq {
  OramLock* lock;
  LockMode mode;
};

struct UnlockReq {
  OramLock* lock;
};

#endif // ORAM_LOCK_HPP
