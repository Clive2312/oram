# Unused Public Functions in ORAM Library

This document lists all public functions declared in header files but never called in the codebase (source files in `src/` or test files in `tests/`).

**Generated:** 2026-01-18

---

## Summary

| Header File | Total Public Methods | Used | Unused |
|-------------|---------------------|------|--------|
| crypto.h | 10 | 9 | 1 |
| position_map.h | 8 | 5 | 3 |
| stash.h | 14 | 11 | 3 |
| bucket_codec.h | 11 | 10 | 1 |
| server.h (OramServer) | 12 | 7 | 5 |
| tree.h (TreeUtil) | 15 | 5 | 10 |
| protocol.h | ~20+ | ~10 | ~10+ |

**Total Unused: ~32+ functions**

---

## Detailed List

### 1. crypto.h

#### Unused Functions

```cpp
// AeadNonce - deterministic nonce generation for testing
static AeadNonce from_counter(uint64_t counter);
```

**Location:** `include/oram/crypto.h:59`, `src/crypto.cpp:56-66`

**Purpose:** Generate deterministic nonces for reproducible testing

**Why unused:** Tests use `DeterministicOramRng` with seeded random nonces instead

**Recommendation:** Can be removed safely

---

### 2. position_map.h

#### Unused Functions

```cpp
// Generate a random leaf ID
LeafId random_leaf();

// Get the number of blocks in the position map
size_t size() const;

// Clear all mappings
void clear();
```

**Location:** `include/oram/position_map.h`

**Purpose:** Utility methods for position map operations

**Why unused:**
- `random_leaf()`: RNG is now handled by `OramRng` interface at higher level
- `size()`: Not needed for current algorithms
- `clear()`: Not needed (position map is recreated on init)

**Recommendation:**
- Remove `random_leaf()` (superseded by OramRng)
- Keep `size()` and `clear()` as they're reasonable utility methods

---

### 3. stash.h

#### Unused Functions

```cpp
// Update an existing block in the stash
void update(BlockId block_id, LeafId new_leaf, std::span<const Byte> new_data);

// Merge another stash into this one
void merge(Stash&& other);

// Insert multiple blocks at once
void insert_all(std::vector<Block>&& blocks);
```

**Location:** `include/oram/stash.h`

**Purpose:** Additional stash manipulation methods

**Why unused:**
- `update()`: Current algorithms use `insert()` which overwrites if exists
- `merge()`: Not needed for single-instance ORAM
- `insert_all()`: Current algorithms insert one at a time

**Recommendation:**
- Remove `update()` (redundant with insert)
- Keep `merge()` and `insert_all()` for potential batch operations

---

### 4. bucket_codec.h

#### Unused Functions

```cpp
// Get the size of a single encrypted slot
size_t encrypted_slot_size() const;
```

**Location:** `include/oram/bucket_codec.h`

**Purpose:** Calculate encrypted slot size for Ring ORAM

**Why unused:** Only used internally within `BucketCodec` implementation

**Recommendation:** Make it private or keep as public utility

---

### 5. server.h (OramServer)

#### Unused Functions

```cpp
// Run the server (standalone server mode)
void run();

// Handle a single client connection
void handle_client();

// Get total storage size
size_t storage_size() const;

// Get number of buckets
size_t num_buckets() const;

// Get bucket size
size_t bucket_size() const;
```

**Location:** `include/oram/server.h`

**Purpose:** Server management and introspection

**Why unused:**
- `run()` / `handle_client()`: Only for standalone server process (not used in tests)
- `storage_size()` / `num_buckets()` / `bucket_size()`: Introspection methods never needed

**Recommendation:**
- Keep `run()` and `handle_client()` for future network server mode
- Remove `storage_size()`, `num_buckets()`, `bucket_size()` unless needed for monitoring

---

### 6. tree.h (TreeUtil)

#### Unused Functions

```cpp
// Get first leaf in subtree rooted at node_id
static LeafId subtree_first_leaf(NodeId node_id, size_t num_leaves);

// Get last leaf in subtree rooted at node_id
static LeafId subtree_last_leaf(NodeId node_id, size_t num_leaves);

// Get parent of node
static NodeId parent(NodeId node_id);

// Get left child of node
static NodeId left_child(NodeId node_id);

// Get right child of node
static NodeId right_child(NodeId node_id);

// Check if node is a leaf
static bool is_leaf(NodeId node_id, size_t num_leaves);

// Convert leaf ID to node ID
static NodeId leaf_to_node(LeafId leaf_id, size_t num_leaves);

// Convert node ID to leaf ID (returns INVALID_LEAF_ID if not a leaf)
static LeafId node_to_leaf(NodeId node_id, size_t num_leaves);

// Get total number of nodes in tree with given depth
static size_t total_nodes(size_t tree_depth);

// Get number of leaves in tree with given depth
static size_t num_leaves(size_t tree_depth);
```

**Location:** `include/oram/tree.h`

**Purpose:** Binary tree navigation utilities

**Why unused:** Current algorithms only use:
- `node_on_path()` - check if node is on path to leaf
- `path_to_leaf()` - get all nodes on path
- `leaf_in_subtree()` - check containment
- `can_place_at_node()` - check if block can be placed at node
- `level_of()` - get node depth

**Recommendation:**
- Keep all functions - they're useful tree utilities that may be needed later
- Alternatively, move to a separate `tree_utils_extra.h` if want to reduce main API surface

---

### 7. protocol.h

#### Unused Functions

Multiple message serialization methods are declared but never called:

```cpp
// Serialize methods (only deserialize is used on server side)
std::vector<Byte> ReadPathRequest::serialize() const;
std::vector<Byte> WritePathRequest::serialize_header() const;
std::vector<Byte> ReadSlotRequest::serialize() const;
std::vector<Byte> WriteSlotRequest::serialize_header() const;
std::vector<Byte> PathDataResponse::serialize_header() const;
std::vector<Byte> BucketsDataResponse::serialize_header() const;

// Other message types
ReadBucketsRequest
WriteBucketsRequest
ReadSlotRequest
WriteSlotRequest
SlotData
```

**Location:** `include/oram/protocol.h`, `src/server.cpp`

**Purpose:** Network protocol message serialization

**Why unused:**
- Server-side only deserializes requests (client sends, server receives)
- Client-side serialization is done inline in client code
- Some message types partially implemented

**Recommendation:**
- Keep all protocol methods - they define the complete protocol even if not all used yet
- This is infrastructure for future network mode completion

---

## Recommendations

### Safe to Remove (High Confidence)
1. **`AeadNonce::from_counter()`** - Superseded by DeterministicOramRng
2. **`PositionMap::random_leaf()`** - Superseded by OramRng interface
3. **`Stash::update()`** - Redundant with insert()

### Consider Removing (Medium Confidence)
4. **`OramServer::storage_size()`** - Not needed unless adding monitoring
5. **`OramServer::num_buckets()`** - Not needed unless adding monitoring
6. **`OramServer::bucket_size()`** - Not needed unless adding monitoring

### Keep for Future Use (Low Confidence to Remove)
7. **`PositionMap::size()` / `clear()`** - Reasonable utility methods
8. **`Stash::merge()` / `insert_all()`** - Useful for batch operations
9. **`BucketCodec::encrypted_slot_size()`** - Useful public query method
10. **`OramServer::run()` / `handle_client()`** - Needed for standalone server
11. **All TreeUtil functions** - Complete tree navigation API
12. **All protocol.h serialize methods** - Complete network protocol definition

---

## Notes

- This analysis was done by searching all `.cpp` files in `src/` and `tests/`
- Functions marked "unused" are never **called**, but may still serve as API documentation
- Some "unused" functions are part of complete APIs (e.g., TreeUtil, Protocol) and removing them would make the API incomplete
- Some functions are infrastructure for features not yet fully implemented (e.g., standalone server mode)

---

## Future Considerations

When deciding whether to remove unused functions, consider:

1. **API Completeness**: Some functions complete a logical API (e.g., tree navigation)
2. **Future Features**: Some support planned but not-yet-implemented features (e.g., server mode)
3. **Maintenance Cost**: How much does keeping the function cost? (compilation time, API surface)
4. **YAGNI Principle**: "You Aren't Gonna Need It" - if not used now, maybe won't be used ever

For a production library, consider:
- Remove clearly redundant/superseded functions
- Keep complete API surfaces (e.g., TreeUtil)
- Keep infrastructure for planned features (e.g., Protocol)
- Document what's used vs. what's for future use
