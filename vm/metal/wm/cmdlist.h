/* Winemetal command lists across the guest boundary (milestone 4b-1, slots 36-38): the guest walks the encoder's
 * linked list and sends it flat, one region: {u32 count, u32 0}, then each node (its type's size, padded to 8, next
 * zeroed, a payload pointer replaced by its offset in the region or ~0 for none), then the payloads. The host rebuilds
 * the list in its own memory, translating each node's handles. A flat list can't loop, and the count is bounded. */
#ifndef MN_WM_CMDLIST_H
#define MN_WM_CMDLIST_H
#include <stdint.h>

#define MN_WM_MAX_NODES 65536u

struct mn_wm_node {
    uint16_t size;      /* 0: not a node type */
    uint8_t nhandles;
    uint8_t handles[4]; /* offsets of obj_handle_t fields */
    uint8_t payload;    /* offset of a pointer to out-of-line data, 0 for none */
    uint8_t count;      /* the offset of its element count */
    uint8_t count_width;
    uint8_t elem;       /* bytes per element */
};
/* The node table of slot 36 (blit), 37 (compute) or 38 (render), and its number of types. */
const struct mn_wm_node *mn_wm_nodes(unsigned slot, unsigned *ntypes);
/* A node's payload bytes (0 if it has none), read from the node itself. */
uint64_t mn_wm_node_payload(const struct mn_wm_node *t, const uint8_t *node);

#endif
