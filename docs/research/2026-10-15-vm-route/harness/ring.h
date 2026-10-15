// The shared ring both sides use: guest (or a host thread, for the baseline) produces 64-byte commands; a host thread
// consumes them. Command kind 1 is a fence: the consumer publishes its value in fence_done.
#pragma once
#include <stdint.h>
#define VB_RING 4096
#define VB_SHM_SIZE (64ull << 20)
struct vb_cmd { uint32_t kind, a; uint64_t value; uint64_t payload[6]; };
struct vb_shm {
  _Alignas(64) volatile uint64_t head;        // producer
  _Alignas(64) volatile uint64_t tail;        // consumer
  _Alignas(64) volatile uint64_t fence_done;  // consumer
  _Alignas(64) volatile uint64_t sum;         // consumer, so the work stays live
  _Alignas(64) volatile uint64_t stop;
  _Alignas(4096) struct vb_cmd slot[VB_RING];
};
