# Ring ORAM Implementation

## Overview

Ring ORAM (Ren et al., 2015) is implemented following the pseudocode from the paper. It achieves better bandwidth than Path ORAM by reading only one slot per bucket on each access, rather than entire buckets.

## Key Differences from Path ORAM

| Feature | Path ORAM | Ring ORAM |
|---------|-----------|-----------|
| **Slots per bucket** | Z | Z + S |
| **Slots read per access** | Full path (Z × (L+1) slots) | One per bucket ((L+1) slots) |
| **Metadata** | None (stateless buckets) | Per-bucket metadata (count, valids, ptrs) |
| **Eviction** | Every access (greedy writeback) | Periodic (every A accesses) |
| **Early reshuffle** | N/A | When bucket.count >= S |
| **Slot invalidation** | N/A | Slots marked invalid after reading |

## Implementation Details

### Bucket Structure

Each Ring ORAM bucket has:
- **Z + S physical slots**: Z for real blocks, S extra for dummy reads
- **Metadata (client-side)**:
  - `count`: Touch count since last reshuffle
  - `valids`: Bitset marking which slots are still valid
  - `addrs[Z]`: Block IDs for the Z logical "real-block" slots
  - `leaves[Z]`: Assigned leaves for those blocks
  - `ptrs[Z]`: Permutation mapping logical slots to physical positions

### Storage Layout

Ring ORAM uses **per-slot encryption** (implemented via `encode_bucket_slotwise`):

```
Bucket on server:
[encrypted_slot_0][encrypted_slot_1]...[encrypted_slot_(Z+S-1)]
```

Each encrypted slot: `IV || AEAD(block_id || leaf || data) || auth_tag`

This allows reading individual slots without decrypting the entire bucket.

### Access Algorithm

```cpp
FUNCTION RING_ORAM_ACCESS(a, op, data_new):
  // 1. Remap position
  l_old = PositionMap[a]
  l_new = RandomLeaf()
  PositionMap[a] = l_new

  // 2. Read path (one slot per bucket)
  data = ReadPath(l_old, a)

  // 3. If not found on path, retrieve from stash
  if (!data) {
    data = Stash[a]
    remove a from Stash
  }

  // 4. Update block
  if (op == WRITE) {
    data = data_new
  }
  Stash[a] = (a, l_new, data)

  // 5. Periodic eviction
  round = (round + 1) mod A
  if (round == 0) {
    EvictPath()
  }

  // 6. Early reshuffle
  EarlyReshuffle(l_old)

  return data
```

### ReadPath

Reads **exactly one slot per bucket** on the path:

```cpp
FOR each bucket on path to l_old:
  // Determine which slot to read
  offset = GetBlockOffset(bucket, target_block_id)

  // Read that slot
  block = ReadSlot(bucket, offset)

  // Invalidate it
  bucket.valids[offset] = 0

  // Check if it's our target
  if (block.id == target_block_id) {
    found = block.data
  }

  // Increment touch count
  bucket.count++
```

### GetBlockOffset

Determines which physical slot to read:

```cpp
// Search for target block in real slots
FOR j = 0 .. Z-1:
  physical_slot = bucket.ptrs[j]
  if (bucket.addrs[j] == target_id AND bucket.valids[physical_slot]) {
    return physical_slot  // Found target
  }

// Not found, return a random valid dummy slot
return RandomValidDummySlot(bucket)
```

**Important**: Dummy slots are NOT always physical slots Z..(Z+S-1). After permutation, dummy slots are any physical slots NOT in {ptrs[0], ..., ptrs[Z-1]}.

### EvictPath

Runs periodically (every A accesses) on a deterministic schedule:

```cpp
evict_leaf = G mod num_leaves
G++

// Read phase: pull all remaining real blocks into stash
FOR each bucket on path:
  ReadBucketIntoStash(bucket)  // Reads Z slots

// Write phase: deep-first writeback
FOR each bucket on path (leaf to root):
  WriteBucketFromStash(bucket)
```

### WriteBucketFromStash

1. Select up to Z compatible blocks from stash
2. Create fresh random permutation of all Z+S positions
3. Place selected blocks and dummies
4. Encrypt all Z+S slots (slotwise)
5. Update metadata:
   - `ptrs[j]` = physical location of j-th logical slot
   - Reset all `valids` to 1
   - Reset `count` to 0
6. Write to server

### EarlyReshuffle

Triggered when a bucket runs low on valid slots:

```cpp
FOR each bucket on accessed path:
  if (bucket.count >= S) {
    ReadBucketIntoStash(bucket)
    WriteBucketFromStash(bucket)
  }
```

This prevents starvation of valid slots for dummy reads.

## Parameters

Typical Ring ORAM parameters:
- **Z = 3-4**: Bucket capacity (real blocks)
- **S = 2-3**: Extra dummy slots
- **A = 3-5**: Eviction rate

Trade-offs:
- Larger S: More resilient to burst accesses, but larger buckets
- Larger A: Fewer evictions, but larger stash
- Smaller Z: Smaller bandwidth, but may increase stash size

## Correctness Invariants

Ring ORAM maintains the same invariants as Path ORAM:

1. **Uniqueness**: Each real block appears exactly once (stash OR one bucket)
2. **Position map consistency**: Block's assigned leaf matches stash/bucket metadata
3. **Capacity**: At most Z valid real blocks per bucket
4. **Oblivious access**: Server sees fixed access pattern (one slot per bucket, regardless of which block)

## Bandwidth Comparison

For tree depth L:

| Operation | Path ORAM | Ring ORAM |
|-----------|-----------|-----------|
| **Read** | (L+1) × Z blocks | (L+1) blocks |
| **Write** | (L+1) × Z blocks | (L+1) blocks |
| **Eviction** | (L+1) × Z blocks | (L+1) × (2Z) blocks |

**Amortized bandwidth per access**:
- Path ORAM: (L+1) × Z blocks
- Ring ORAM: (L+1) × (1 + 2Z/A) blocks

For typical parameters (Z=4, A=4), Ring ORAM uses ~3x less bandwidth than Path ORAM.

## Testing

Ring ORAM is tested with:
- Single/multiple block read/write
- Read before write (zeros)
- Eviction schedule (periodic EvictPath)
- Early reshuffle (count >= S)
- Invariant checking after each operation
- Stash size monitoring

All tests pass with Z=3, S=2, A=4 on small instances (N=8-16 blocks).

## Implementation Notes

### Slot-Level Operations

Ring ORAM required adding slot-level read/write to the server:
- `read_slot(node_id, slot_index, encrypted_slot_size)`
- `write_slot(node_id, slot_index, data)`

These are implemented by calculating byte offsets within the bucket storage.

### Metadata Storage

Bucket metadata is **client-side only** (not sent to server). The server only stores encrypted bucket data. This maintains the oblivious property - the server cannot learn which slots are real vs dummy.

### Network Protocol

Added `ReadSlot` and `WriteSlot` message types to the protocol for efficient individual slot access.

## Limitations

Current implementation:
- ✅ Follows Ring ORAM paper pseudocode exactly
- ✅ Deterministic and secure RNG modes
- ✅ Local and network modes
- ✅ Invariant checking
- ❌ **No XOR optimization** (reads full slots, not XOR'd differences)
- ❌ **No partition ORAM** (flat position map, not recursive)

The XOR optimization from the paper is not implemented - each slot read fetches the full encrypted slot. This could be added in a future optimization.

## Files Modified/Created

**Core Implementation**:
- `include/oram/ring_oram.h` - Ring ORAM class and metadata structures
- `src/ring_oram.cpp` - Complete Ring ORAM implementation

**Bucket Layer**:
- `include/oram/bucket_codec.h` - Added slotwise encoding methods
- `src/bucket_codec.cpp` - Implemented per-slot encryption

**Server Layer**:
- `include/oram/server.h` - Added slot-level operations
- `src/server.cpp` - Implemented read_slot/write_slot

**Protocol**:
- `include/oram/protocol.h` - Added ReadSlot/WriteSlot messages

**Tests**:
- `tests/ring_oram_test.cpp` - Integration tests for Ring ORAM

## Comparison with Path ORAM

Both implementations share:
- Same base `Oram` class API
- Same position map and stash
- Same encryption (AEAD)
- Same client-server architecture
- Same tree utilities

Key implementation differences:
- Ring ORAM uses slotwise bucket encoding
- Ring ORAM maintains per-bucket metadata
- Ring ORAM has periodic eviction schedule
- Ring ORAM implements early reshuffle
- Ring ORAM reads individual slots via server slot operations

Both pass comprehensive tests and maintain ORAM correctness invariants.
