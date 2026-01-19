#pragma once

#include "common.h"
#include <vector>

namespace oram {

/**
 * Tree utilities for ORAM bucket indexing.
 *
 * The ORAM tree is a complete binary tree with:
 * - Root at node 0 (level 0)
 * - For a node i: left child = 2i+1, right child = 2i+2, parent = (i-1)/2
 * - Leaves are at level L (depth L)
 * - Leaf indices are from 0 to 2^L - 1
 * - Node IDs are from 0 to 2^(L+1) - 2
 *
 * Level numbering: root is level 0, leaves are level L.
 */
class TreeUtil {
public:
    /**
     * Get the node ID at a specific level on the path from root to leaf.
     *
     * Formula: node_id = (2^level - 1) + (leaf_id >> (depth - level))
     *
     * Explanation:
     * - (2^level - 1) gives the first node ID at that level
     *   (level 0: 0, level 1: 1, level 2: 3, ...)
     * - (leaf_id >> (depth - level)) determines which node at that level
     *   by shifting away the lower bits that don't matter yet
     *
     * @param leaf_id The leaf ID (0 to num_leaves-1)
     * @param level The level in the tree (0 = root, L = leaf)
     * @param depth The tree depth L
     * @return The node ID at the given level on the path
     */
    static NodeId node_on_path(LeafId leaf_id, size_t level, size_t depth) {
        return ((1ULL << level) - 1) + (leaf_id >> (depth - level));
    }

    /**
     * Get all node IDs on the path from root to leaf.
     *
     * @param leaf_id The leaf ID
     * @param depth The tree depth L
     * @return Vector of node IDs from root (level 0) to leaf (level L)
     */
    static std::vector<NodeId> path_to_leaf(LeafId leaf_id, size_t depth) {
        std::vector<NodeId> path;
        path.reserve(depth + 1);
        for (size_t level = 0; level <= depth; level++) {
            path.push_back(node_on_path(leaf_id, level, depth));
        }
        return path;
    }

    /**
     * Get the level of a node in the tree.
     *
     * @param node_id The node ID
     * @return The level (0 for root)
     */
    static size_t level_of(NodeId node_id) {
        // Level 0: node 0
        // Level 1: nodes 1, 2
        // Level 2: nodes 3, 4, 5, 6
        // Level l: nodes from 2^l - 1 to 2^(l+1) - 2
        //
        // For a node at level l, the node ID range is [2^l - 1, 2^(l+1) - 2]
        // We can compute the level as: floor(log2(node_id + 1))
        size_t level = 0;
        NodeId n = node_id + 1;
        while (n > 1) {
            n >>= 1;
            level++;
        }
        return level;
    }

    /**
     * Get the first leaf ID in the subtree rooted at a given node.
     *
     * @param node_id The node ID
     * @param depth The tree depth L
     * @return The first (leftmost) leaf ID in the subtree
     */
    static LeafId subtree_first_leaf(NodeId node_id, size_t depth) {
        size_t level = level_of(node_id);
        size_t levels_to_leaf = depth - level;

        // Traverse down left side
        NodeId node = node_id;
        for (size_t i = 0; i < levels_to_leaf; i++) {
            node = 2 * node + 1;  // Go left
        }
        // Now 'node' is the leftmost leaf node ID in this subtree
        // Convert to leaf ID (0-indexed from left)
        return node - ((1ULL << depth) - 1);
    }

    /**
     * Get the last leaf ID in the subtree rooted at a given node.
     *
     * @param node_id The node ID
     * @param depth The tree depth L
     * @return The last (rightmost) leaf ID in the subtree
     */
    static LeafId subtree_last_leaf(NodeId node_id, size_t depth) {
        size_t level = level_of(node_id);
        size_t levels_to_leaf = depth - level;

        // Traverse down right side
        NodeId node = node_id;
        for (size_t i = 0; i < levels_to_leaf; i++) {
            node = 2 * node + 2;  // Go right
        }
        // Convert to leaf ID
        return node - ((1ULL << depth) - 1);
    }

    /**
     * Check if a leaf is in the subtree rooted at a given node.
     *
     * @param leaf_id The leaf ID
     * @param node_id The node ID
     * @param depth The tree depth L
     * @return true if the leaf is in the subtree
     */
    static bool leaf_in_subtree(LeafId leaf_id, NodeId node_id, size_t depth) {
        LeafId first = subtree_first_leaf(node_id, depth);
        LeafId last = subtree_last_leaf(node_id, depth);
        return leaf_id >= first && leaf_id <= last;
    }

    /**
     * Check if a block with given assigned leaf can be placed at a given node.
     *
     * A block can be placed at a node if and only if the node is on the path
     * from root to the block's assigned leaf.
     *
     * @param block_leaf The block's assigned leaf
     * @param node_id The node where we want to place the block
     * @param depth The tree depth L
     * @return true if placement is valid
     */
    static bool can_place_at_node(LeafId block_leaf, NodeId node_id, size_t depth) {
        // A block can be placed at node_id if node_id is on the path to block_leaf
        // Equivalently: block_leaf is in the subtree rooted at node_id
        return leaf_in_subtree(block_leaf, node_id, depth);
    }

    /**
     * Get the parent node ID.
     *
     * @param node_id The node ID
     * @return The parent node ID, or INVALID_NODE_ID for root
     */
    static NodeId parent(NodeId node_id) {
        if (node_id == 0) return INVALID_NODE_ID;
        return (node_id - 1) / 2;
    }

    /**
     * Get left child node ID.
     */
    static NodeId left_child(NodeId node_id) {
        return 2 * node_id + 1;
    }

    /**
     * Get right child node ID.
     */
    static NodeId right_child(NodeId node_id) {
        return 2 * node_id + 2;
    }

    /**
     * Check if a node is a leaf.
     *
     * @param node_id The node ID
     * @param depth The tree depth L
     * @return true if the node is a leaf
     */
    static bool is_leaf(NodeId node_id, size_t depth) {
        NodeId first_leaf = (1ULL << depth) - 1;
        NodeId last_leaf = (1ULL << (depth + 1)) - 2;
        return node_id >= first_leaf && node_id <= last_leaf;
    }

    /**
     * Convert a leaf ID to its node ID.
     *
     * @param leaf_id The leaf ID (0 to 2^L - 1)
     * @param depth The tree depth L
     * @return The corresponding node ID
     */
    static NodeId leaf_to_node(LeafId leaf_id, size_t depth) {
        return (1ULL << depth) - 1 + leaf_id;
    }

    /**
     * Convert a node ID to leaf ID (only valid for leaf nodes).
     *
     * @param node_id The node ID
     * @param depth The tree depth L
     * @return The corresponding leaf ID
     */
    static LeafId node_to_leaf(NodeId node_id, size_t depth) {
        return node_id - ((1ULL << depth) - 1);
    }

    /**
     * Get total number of nodes in a tree of given depth.
     *
     * @param depth The tree depth L
     * @return Total number of nodes (2^(L+1) - 1)
     */
    static size_t total_nodes(size_t depth) {
        return (1ULL << (depth + 1)) - 1;
    }

    /**
     * Get number of leaves in a tree of given depth.
     *
     * @param depth The tree depth L
     * @return Number of leaves (2^L)
     */
    static size_t num_leaves(size_t depth) {
        return 1ULL << depth;
    }
};

} // namespace oram
