#pragma once

/**
 * ORAM Library - Main Include Header
 *
 * This header includes all public API headers for the ORAM library.
 *
 * Supported ORAM algorithms:
 * - Path ORAM (Stefanov et al., 2013)
 * - Ring ORAM (Ren et al., 2015)
 *
 * Example usage:
 *
 *   #include <oram/oram_lib.h>
 *
 *   // Create Path ORAM with local server
 *   oram::PathOram oram;
 *   oram::OramConfig config;
 *   config.num_blocks = 1024;
 *   config.block_size = 4096;
 *   config.Z = 4;
 *   config.use_local_server = true;
 *   oram.init(config);
 *
 *   // Write data
 *   std::vector<uint8_t> data(4096, 0x42);
 *   oram.write(42, data);
 *
 *   // Read data
 *   auto read_data = oram.read(42);
 */

#include "common.h"
#include "crypto.h"
#include "position_map.h"
#include "stash.h"
#include "bucket_codec.h"
#include "tree.h"
#include "protocol.h"
#include "server.h"
#include "oram.h"
#include "path_oram.h"
#include "ring_oram.h"
