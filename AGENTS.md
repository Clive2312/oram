# agent.md — C++ ORAM Library (Path ORAM + Ring ORAM) — Client/Server + Multi-step

You are an engineering agent implementing an ORAM library in **C++** with a **mandatory client–server network model**.

Papers (treat as the source of truth for algorithms and terminology):
- Path ORAM (Stefanov et al., 2013): https://eprint.iacr.org/2013/280.pdf
- Ring ORAM (Ren et al., 2015): https://www.usenix.org/system/files/conference/usenixsecurity15/sec15-paper-ren-ling.pdf

This library will be used as part of the storage layer of a larger project.

Priorities: **correctness**, **clean unified APIs**, **tests**, **measurable performance**, and **minimized network round-trips**.
Avoid research-prototype shortcuts that silently violate ORAM invariants.

---

## Mandatory Repo Dependencies (already in repo)
- `third_party/argmap`: command line args parsing
- `third_party/net`: client-server communication (**mandatory**)
- `third_party/openssl`: cryptography (**mandatory**)

---

## Mandatory Architecture (Client/Server)
Client:
- Holds **PositionMap**, **Stash**, client-side metadata, and all ORAM logic.

Server:
- Stores encrypted ORAM state (tree buckets / ring buckets) indexed by bucket/node ID.
- Responds to client requests over `third_party/net`.
- Must not learn block IDs or plaintext data.

All ORAM accesses must go through the network layer.

---

## Mandatory Design Requirement: Class Structure (Inheritance)
Path ORAM and Ring ORAM differ substantially, so the implementation MUST use inheritance:

- `class Oram` (abstract base class)
  - Unified public API:
    - `init(...)`
    - `read(block_id)`
    - `write(block_id, data)`
    - `read_write(block_id, optional<data>)`
  - Shared utilities:
    - network request/response helpers
    - crypto wrappers
    - common types (Block, Bucket codec, params, etc.)
  - Declares pure virtual methods for ORAM-specific behavior.

- `class PathOram : public Oram` implements Path ORAM logic.
- `class RingOram : public Oram` implements Ring ORAM logic.

Hard constraints:
- DO NOT implement algorithm selection using a flag inside one monolithic ORAM class.
- DO NOT duplicate shared network/crypto plumbing across both implementations (share in base class).

---

## Security & Correctness Invariants (Non-negotiable)
1) Position map correctness: each block_id maps to exactly one leaf.
2) Uniqueness: each real block exists exactly once (in stash OR in exactly one bucket).
3) Path constraint (Path ORAM): a block may appear only on path to its assigned leaf.
4) Oblivious access pattern:
   - Path ORAM touches the full root-to-leaf path each access.
   - Ring ORAM follows the fixed access pattern from the Ring ORAM paper.
5) Encryption:
   - Use OpenSSL.
   - Use AEAD (AES-GCM or ChaCha20-Poly1305).
   - Never reuse nonce with the same key.
6) Dummy indistinguishability: dummy and real ciphertexts must be indistinguishable.

Integrity note:
- You may ignore **global integrity mechanisms** (e.g., Merkle trees, global verification protocols).
- Per-bucket AEAD is still required.

Ring ORAM metadata note:
- Ring ORAM uses per-bucket metadata (e.g., `count`, `valids`, permutation pointers). The paper treats some metadata as public.
- It is acceptable for Ring ORAM to store `count` and `valids` in plaintext if following the paper’s design.
- Plaintext metadata MUST NOT include plaintext block IDs or plaintext user data.

---

## Performance Guidance (must follow)
- Avoid unnecessary copies for large blocks: use `std::span`, move semantics, and preallocated buffers.
- Minimize network calls: batch messages; prefer fewer `send/recv`.
- Correctness > performance. Start with a correct baseline, then optimize.
- Parameter constraints:
  - Path ORAM should use **Z >= 4** (Z=4 typical). Smaller Z not supported.

---

# ALGORITHM SPECIFICATION (DO NOT INVENT)
The pseudocode below must be treated as the intended algorithmic behavior.
Implementations may differ in engineering details, but MUST preserve:
- access patterns
- placement constraints
- fixed sizes (bucket slot counts)
- remapping logic
- eviction/reshuffle schedules

---

## Path ORAM — Pseudocode to Follow

### Data model (conceptual)
- Client:
  - `PositionMap[a] -> leaf` for each logical block address `a`
  - `Stash` holds blocks `(addr, leaf, data)`
- Server:
  - Binary tree of buckets with depth `L` (root at level 0, leaves at level L)
  - Each bucket holds **exactly Z slots**.
  - Each slot is either a real block or dummy.

### Helper: path node at level
`NodeOnPath(leaf, level)` returns the unique tree node at `level` on the root-to-`leaf` path.

### Fixed-size bucket rules (required)
- `ReadBucket(node)` reads and returns exactly Z slots (decrypt client-side; include dummies but skip them when inserting into stash).
- `WriteBucket(node, blocks)` writes exactly Z slots (pad with dummies), and re-encrypts the full bucket with fresh randomness.

### Required writeback placement rule (critical)
During writeback for accessed leaf `x_old`, a stash block `b` with assigned leaf `b.leaf` may be placed in the bucket at `level`
iff `NodeOnPath(x_old, level) == NodeOnPath(b.leaf, level)`.

### Access algorithm
```text
FUNCTION PATH_ORAM_ACCESS(op, a, data_new_optional):
  x_old := PositionMap[a]
  x_new := UniformRandomLeaf()
  PositionMap[a] := x_new

  # 1) Read full path into stash
  FOR level = 0 .. L:
    node := NodeOnPath(x_old, level)
    bucket_slots := ReadBucket(node)                 # reads exactly Z slots
    FOR each slot in bucket_slots:
      IF slot is real:
        InsertOrUpdate(Stash, slot.addr, slot.leaf, slot.data)

  # 2) Serve request from stash
  data_old := LookupOrDefault(Stash, a)              # define "default" semantics clearly
  IF op == WRITE:
    Update(Stash, a, x_new, data_new_optional)

  # 3) Write back path (greedy deep-first)
  FOR level = L .. 0:                                # leaf -> root
    node := NodeOnPath(x_old, level)

    Cand := { b in Stash | NodeOnPath(b.leaf, level) == node }
    chosen := TakeUpToZ(Cand)                        # any deterministic tie-break in test mode
    RemoveFromStash(Stash, chosen)

    WriteBucket(node, chosen)                        # writes exactly Z slots padded with dummies

  RETURN data_old
```

## Ring ORAM — Pseudocode to Follow
Ring ORAM differs from Path ORAM. Do not approximate it using Path ORAM logic.
Follow the access + eviction + early reshuffle schedule.

Parameters (conceptual)
Each bucket has Z+S data slots.

At most Z real blocks reside in a bucket at any time.

S controls dummy slack and early reshuffle trigger.

A is the eviction period (every A accesses, run EvictPath()).

Tree depth L, number of leaves 2^L.

### Bucket metadata (conceptual)
Each bucket maintains:

count (integer): number of times touched since last reshuffle

valids[0..Z+S-1] (bitset): whether a slot is still valid to read since last reshuffle

metadata mapping real blocks to physical slots (e.g., ptrs), plus encrypted data

### Implementation rule

The server may see count and valids if stored in plaintext.

The server must not learn plaintext block addresses or plaintext block data.

### High-level ACCESS
```text
GLOBAL round in [0..A-1], initially 0

FUNCTION RING_ORAM_ACCESS(a, op, data_new_optional):
  l_old := PositionMap[a]
  l_new := UniformRandomLeaf()
  PositionMap[a] := l_new

  data := ReadPath(l_old, a)              # returns data if found, else ⊥

  IF data == ⊥:
    data := RemoveFromStash(a)            # block must be in stash if not on path

  IF op == READ:
    RETURN data
  ELSE:
    data := data_new_optional
    Stash := Stash ∪ {(a, l_new, data)}   # updated block placed in stash with new leaf

  round := (round + 1) mod A
  IF round == 0:
    EvictPath()

  EarlyReshuffle(l_old)
```

### ReadPath
ReadPath reads exactly one slot per bucket on the path, invalidates it, and increments count.

```text
FUNCTION ReadPath(l, a):
  found := ⊥
  FOR level = 0 .. L:
    bucket := BucketAtPath(l, level)

    offset := GetBlockOffset(bucket, a)         # real block slot if present+valid; else a valid dummy slot
    blk := ReadSlot(bucket, offset)             # read exactly 1 slot
    Invalidate(bucket, offset)                  # mark this slot invalid until next reshuffle

    IF blk.addr == a:
      found := blk.data

    bucket.count := bucket.count + 1
  RETURN found
```

### EvictPath (periodic)
Eviction runs on a deterministic public schedule using a global counter G selecting leaf paths.

```text
GLOBAL G, initially 0

FUNCTION EvictPath():
  l := G mod 2^L
  G := G + 1

  # Read phase: move remaining real blocks from buckets on path into stash
  FOR level = 0 .. L:
    bucket := BucketAtPath(l, level)
    Stash := Stash ∪ ReadBucket(bucket)         # ReadBucket reads a fixed number of slots as defined below

  # Write phase: push stash blocks down (deep-first) and reshuffle buckets
  FOR level = L .. 0:
    bucket := BucketAtPath(l, level)
    WriteBucket(bucket, Stash)
    bucket.count := 0
```

### EarlyReshuffle (per access)
Early reshuffle triggers when a bucket on the accessed path risks running out of valid slots.

```text
FUNCTION EarlyReshuffle(l):
  FOR level = 0 .. L:
    bucket := BucketAtPath(l, level)
    IF bucket.count >= S:
      Stash := Stash ∪ ReadBucket(bucket)
      WriteBucket(bucket, Stash)
      bucket.count := 0
```

### Ring ORAM helper subroutines (implementation-critical)
#### GetBlockOffset(bucket, a)
Chooses which physical slot to read for this bucket during ReadPath.
If the target is present and still valid, read that slot; otherwise choose a valid dummy slot.

```text
FUNCTION GetBlockOffset(bucket, a):
  FOR j = 0 .. Z-1:
    IF bucket.addrs[j] == a AND bucket.valids[ bucket.ptrs[j] ] == 1:
      RETURN bucket.ptrs[j]
  RETURN RandomValidDummySlot(bucket.valids)
```

#### ReadBucket(bucket)
Purpose: move remaining real blocks in this bucket into stash while performing a fixed amount of work.
Read up to Z real slots (padding with dummy reads if fewer remain).

```text
FUNCTION ReadBucket(bucket):
  real_read := 0

  FOR j = 0 .. Z-1:
    t := bucket.ptrs[j]
    IF bucket.valids[t] == 1:
      blk := ReadSlot(bucket, t)
      real_read := real_read + 1
      IF bucket.addrs[j] != ⊥:
        Stash := Stash ∪ {(bucket.addrs[j], bucket.leaves[j], blk.data)}

  WHILE real_read < Z:
    t := RandomValidDummySlot(bucket.valids)
    ReadSlot(bucket, t)
    real_read := real_read + 1

  RETURN blocks added to stash
```

#### WriteBucket(bucket, Stash)
Purpose: evict compatible blocks from stash into this bucket, then reshuffle and reset metadata.
The bucket must end up with exactly Z+S data slots (real + dummy), with fresh permutation.

```text
FUNCTION WriteBucket(bucket, Stash):
  selected := SelectUpToZCompatibleBlocks(Stash, bucket)  # based on leaf/subtree constraints
  RemoveFromStash(Stash, selected)

  perm := FreshRandomPermutation(0 .. Z+S-1)

  Place selected blocks into permuted data slots
  Fill remaining slots with dummy blocks

  Set bucket.valids[*] = 1
  Set bucket.count = 0
  Refresh and apply encryption to bucket contents as per design
  Write out the full bucket (all metadata + all Z+S slots)
```
Note: Do NOT implement the XOR optimization unless explicitly requested; start with the standard per-level reads/writes.

#### MULTI-STEP EXECUTION PLAN (MANDATORY)
- You MUST complete this task in 4 steps.
- After finishing each step, you MUST STOP and ask for review.
- Do not start the next step until the user explicitly instructs you to continue.

For every step:
- Provide a clear summary of what you implemented.
- List created/modified files.
- Include any TODOs left intentionally for next steps.
- Ensure the code compiles (or is structurally compilable) at that step.

##### STEP 1 — Scaffolding & Data Structures ONLY (STOP AFTER THIS STEP)
Goal: create foundational data structures and class skeletons.  
DO NOT implement concrete Path ORAM or Ring ORAM algorithms yet.

You MUST implement:
- Shared types and storage structures:
  - Client-side: PositionMap, Stash, params, Block/Slot representation
  - Server-side: ciphertext bucket storage representation; request/response message structs
- Network protocol skeleton:
  - Client stub(s) for requesting bucket/path operations
  - Server handler skeleton mapping node_id -> ciphertext using third_party/net
- Class definitions:
  - Oram base class, PathOram, RingOram derived classes
  - Core algorithm methods exist but are unimplemented (throw / ORAM_UNIMPLEMENTED()).
- Crypto wrapper:
  - AEAD wrapper interface + plumbing (OpenSSL). Prefer implementing AEAD now.

STOP after Step 1 and ask for review.

##### STEP 2 — Implement Path ORAM (STOP AFTER THIS STEP)
Goal: implement Path ORAM end-to-end over client–server storage, matching the pseudocode above.

You MUST implement:
- PATH_ORAM_ACCESS, including:
  - full path read
  - stash insert/update
  - leaf remap
  - greedy deep-first writeback with the required placement rule
- Bucket codec + AEAD encryption/decryption
- Deterministic RNG mode (seeded) + production CSPRNG mode
- Debug invariant checks for small instances

Do NOT implement Ring ORAM in this step.  
STOP after Step 2 and ask for review.

##### STEP 3 — Implement Ring ORAM (STOP AFTER THIS STEP)
Goal: implement Ring ORAM end-to-end over client–server storage, matching the pseudocode above.

You MUST implement:
- Ring ACCESS schedule, ReadPath, EvictPath, EarlyReshuffle, and helper routines
- Bucket metadata layout needed for GetBlockOffset / valids / ptrs / count
- Integrate with the unified Oram API and networking layer
- Document chosen variant and design in docs/ring_oram.md

Do NOT add comprehensive testing here unless required for basic validation.  
STOP after Step 3 and ask for review.

##### STEP 4 — Testing & Benchmarks (STOP AFTER THIS STEP)
Goal: comprehensive tests for both ORAMs + basic benchmarks.

You MUST implement:
- Unit tests: tree math, bucket codec, AEAD roundtrip + tamper failure
- Randomized semantic tests vs reference unordered_map
- Invariant checks for small N
- Benchmarks: ops/sec, latency, network RTT count, stash stats

STOP after Step 4 and ask for review.

##### Interaction Rule (IMPORTANT)
After each step, you MUST stop and wait for the user's explicit instruction:
- "continue to step 2"
- "continue to step 3"
- "continue to step 4"
- or requests to revise the previous step.

Do not proceed automatically.
