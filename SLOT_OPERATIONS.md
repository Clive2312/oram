# Slot-Level Operations for Ring ORAM

## Overview

Ring ORAM requires reading and writing individual slots from buckets, rather than always accessing entire buckets like Path ORAM does. To support this, I've added **slot-level encryption and access** capabilities to the bucket layer, server, and protocol.

## Key Changes

### 1. BucketCodec - Slot-Level Encryption

**Problem**: The original BucketCodec encrypted each bucket as a single AEAD ciphertext, meaning you had to decrypt the entire bucket to read any slot.

**Solution**: Added per-slot encryption methods:

```cpp
// Encrypt/decrypt a single slot
std::vector<Byte> encode_slot(const Block& block);
Block decode_slot(std::span<const Byte> ciphertext);
size_t encrypted_slot_size() const;

// Encode bucket with per-slot encryption (for Ring ORAM)
std::vector<Byte> encode_bucket_slotwise(const Bucket& bucket);
Bucket decode_bucket_slotwise(std::span<const Byte> ciphertext);
```

**Format**: When using `encode_bucket_slotwise()`, each slot is encrypted individually:
```
[encrypted_slot_0][encrypted_slot_1]...[encrypted_slot_Z-1]
```

Each encrypted slot: `IV || AEAD(block_id || leaf || data) || auth_tag`

### 2. Server - Slot-Level Storage Access

Added methods to read/write individual slots by offset:

```cpp
// OramServer
std::vector<Byte> read_slot(NodeId node_id, size_t slot_index,
                            size_t encrypted_slot_size) const;
void write_slot(NodeId node_id, size_t slot_index,
                std::span<const Byte> data);

// OramClient
std::vector<Byte> read_slot(NodeId node_id, size_t slot_index,
                            size_t encrypted_slot_size);
void write_slot(NodeId node_id, size_t slot_index,
                std::span<const Byte> data);
```

**Implementation**: These methods calculate the byte offset within the bucket storage:
```cpp
size_t offset = slot_index * encrypted_slot_size;
```

### 3. Protocol - New Message Types

Added protocol support for slot operations:

**Message Types**:
- `ReadSlot (0x08)` - Request to read a single slot
- `WriteSlot (0x09)` - Request to write a single slot
- `SlotData (0x14)` - Response containing slot data

**Request Structures**:
```cpp
struct ReadSlotRequest {
    NodeId node_id;
    uint32_t slot_index;
    uint32_t encrypted_slot_size;
};

struct WriteSlotRequest {
    NodeId node_id;
    uint32_t slot_index;
    // Followed by: encrypted_slot_data
};
```

## Usage for Ring ORAM

Ring ORAM can now efficiently read individual slots without fetching entire buckets:

```cpp
// Initialize server with slotwise encryption
oram::BucketCodec codec(aead, block_size, Z + S);

server.init([&](NodeId node_id) {
    oram::Bucket dummy_bucket(Z + S, block_size);
    return codec.encode_bucket_slotwise(dummy_bucket);  // Per-slot encryption
});

// Read individual slot from bucket (Ring ORAM offset-based access)
size_t encrypted_slot_size = codec.encrypted_slot_size();
auto encrypted_slot = server.read_slot(node_id, offset, encrypted_slot_size);
Block block = codec.decode_slot(encrypted_slot);

// Write individual slot back
auto new_encrypted_slot = codec.encode_slot(updated_block);
server.write_slot(node_id, offset, new_encrypted_slot);
```

## Path ORAM Compatibility

Path ORAM continues to work unchanged:
- Still uses `encode()` / `decode()` for whole-bucket encryption
- Still uses `read_bucket()` / `write_bucket()` for full bucket access
- All existing tests pass

Path ORAM could optionally be migrated to slotwise encoding by:
1. Using `encode_bucket_slotwise()` during initialization
2. Reading all slots individually if needed (less efficient, but possible)

## Security Properties

- **Per-slot AEAD**: Each slot has its own IV and authentication tag
- **Server remains oblivious**: Server never sees plaintext, only encrypted slots
- **No information leakage**: Reading one slot doesn't reveal other slots
- **Tamper detection**: Each slot is authenticated independently

## Implementation Details

**Serialization Format** (per slot):
```
block_id (8 bytes) || leaf (8 bytes) || data (block_size bytes)
```

**Encrypted Slot Size**:
```cpp
encrypted_slot_size = slot_size + IV_SIZE + TAG_SIZE
                    = (8 + 8 + block_size) + 12 + 16
                    = block_size + 44 bytes
```

**Bucket Storage** (slotwise):
```
For Z slots:
total_size = Z * encrypted_slot_size
```

## Testing

All existing tests pass, confirming backward compatibility. The slot operations are ready to use for Ring ORAM implementation (Step 3).
