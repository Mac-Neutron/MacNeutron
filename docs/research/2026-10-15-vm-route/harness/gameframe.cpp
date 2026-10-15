// A game-shaped frame loop, the same source for x86-64 Linux (under FEX), x86-64 Windows (under MacNeutron's Wine and
// FEX) and native arm64: a job system of 8 workers fed through a lock-free MPMC queue, per-chunk entity integration
// (SIMD-friendly float math), a parallel spatial-hash broadphase with contended atomics, and a render thread fed
// through a single-producer ring of 64-byte commands. Prints `frames <n> total <s> ms_per_frame <ms> check <hash>`.
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>
#if defined(__x86_64__)
#include <immintrin.h>
#define PAUSE() _mm_pause()
#else
#define PAUSE() asm volatile("yield")
#endif

static constexpr int WORKERS = 8, ENTITIES = 262144, CHUNK = 1024, FRAMES = 300, CELLS = 65536;
static constexpr int COMMANDS = 20000, RING = 4096;

struct alignas(64) Slot { std::atomic<uint64_t> seq; int job; };
struct Queue {  // Vyukov's bounded MPMC queue
  static constexpr uint64_t N = 1024;
  Slot slots[N];
  alignas(64) std::atomic<uint64_t> head{0};
  alignas(64) std::atomic<uint64_t> tail{0};
  Queue() { for (uint64_t i = 0; i < N; i++) slots[i].seq.store(i, std::memory_order_relaxed); }
  bool push(int job) {
    uint64_t pos = tail.load(std::memory_order_relaxed);
    for (;;) {
      Slot &s = slots[pos & (N - 1)];
      int64_t d = (int64_t)s.seq.load(std::memory_order_acquire) - (int64_t)pos;
      if (d == 0) { if (tail.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) { s.job = job; s.seq.store(pos + 1, std::memory_order_release); return true; } }
      else if (d < 0) return false;
      else pos = tail.load(std::memory_order_relaxed);
    }
  }
  bool pop(int &job) {
    uint64_t pos = head.load(std::memory_order_relaxed);
    for (;;) {
      Slot &s = slots[pos & (N - 1)];
      int64_t d = (int64_t)s.seq.load(std::memory_order_acquire) - (int64_t)(pos + 1);
      if (d == 0) { if (head.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) { job = s.job; s.seq.store(pos + N, std::memory_order_release); return true; } }
      else if (d < 0) return false;
      else pos = head.load(std::memory_order_relaxed);
    }
  }
};

static float px[ENTITIES], py[ENTITIES], pz[ENTITIES], vx[ENTITIES], vy[ENTITIES], vz[ENTITIES];
alignas(64) static std::atomic<uint32_t> cell_count[CELLS];
static uint32_t cell_of[ENTITIES];
alignas(64) static Queue queue;
alignas(64) static std::atomic<int> pending{0};
alignas(64) static std::atomic<bool> quit{false};
alignas(64) static char pad_after_quit[64];

static void integrate(int chunk) {
  const float dt = 1.0f / 60.0f;
  for (int i = chunk * CHUNK; i < (chunk + 1) * CHUNK; i++) {
    float ax = -px[i] * 0.01f, ay = -9.8f - py[i] * 0.01f, az = -pz[i] * 0.01f;
    vx[i] = (vx[i] + ax * dt) * 0.999f, vy[i] = (vy[i] + ay * dt) * 0.999f, vz[i] = (vz[i] + az * dt) * 0.999f;
    px[i] += vx[i] * dt, py[i] += vy[i] * dt, pz[i] += vz[i] * dt;
    if (py[i] < -100.0f) { py[i] = -100.0f; vy[i] = -vy[i] * 0.5f; }
  }
}
#ifdef NO_CRT_MATH  // floor without a C runtime call (under MacNeutron, ucrtbase is native ARM64EC: each call crosses)
static inline int ifloor(float f) { int i = (int)f; return i - (f < (float)i); }
#else
static inline int ifloor(float f) { return (int)floorf(f); }
#endif
static void broadphase(int chunk) {
  for (int i = chunk * CHUNK; i < (chunk + 1) * CHUNK; i++) {
    uint32_t cx = (uint32_t)ifloor(px[i] * 0.25f) & 255, cz = (uint32_t)ifloor(pz[i] * 0.25f) & 255;
    uint32_t c = cx * 256 + cz;
    cell_of[i] = c;
    cell_count[c].fetch_add(1, std::memory_order_relaxed);
  }
}
static void worker() {
  int job;
  while (!quit.load(std::memory_order_relaxed)) {
    if (!queue.pop(job)) { PAUSE(); continue; }
    if (job & 0x10000) broadphase(job & 0xffff); else integrate(job);
    pending.fetch_sub(1, std::memory_order_acq_rel);
  }
}
static void run_phase(int flag) {
  int chunks = ENTITIES / CHUNK;
  pending.store(chunks, std::memory_order_relaxed);
  for (int c = 0; c < chunks; c++) while (!queue.push(c | flag)) PAUSE();
  while (pending.load(std::memory_order_acquire) != 0) PAUSE();
}

struct alignas(64) Command { uint32_t kind, entity, cell, pad; float x, y, z, w; uint64_t extra[4]; };
static struct {
  alignas(64) std::atomic<uint64_t> head{0};
  alignas(64) std::atomic<uint64_t> tail{0};
  Command slot[RING];
} ring;
alignas(64) static std::atomic<uint64_t> render_sum{0};
static void render_thread(uint64_t total) {
  uint64_t head = 0, sum = 0;
  for (uint64_t i = 0; i < total; i++) {
    while (head == i) head = ring.head.load(std::memory_order_acquire);
    const Command &c = ring.slot[i % RING];
    sum += c.kind + c.entity + c.cell + (uint64_t)(c.x * 16.0f) + c.extra[0];
    ring.tail.store(i + 1, std::memory_order_release);
  }
  render_sum.store(sum);
}

int main() {
  for (int i = 0; i < ENTITIES; i++) {
    px[i] = (float)(i % 512) - 256.0f, py[i] = (float)(i % 97), pz[i] = (float)(i / 512) - 256.0f;
    vx[i] = (float)(i % 7) - 3.0f, vy[i] = 0.0f, vz[i] = (float)(i % 5) - 2.0f;
  }
  std::vector<std::thread> workers;
  for (int i = 0; i < WORKERS; i++) workers.emplace_back(worker);
  uint64_t total = (uint64_t)FRAMES * COMMANDS;
  std::thread render(render_thread, total);
  auto t0 = std::chrono::steady_clock::now();
  uint64_t produced = 0, tail = 0;
  for (int f = 0; f < FRAMES; f++) {
    for (auto &c : cell_count) c.store(0, std::memory_order_relaxed);
    run_phase(0);
    run_phase(0x10000);
    for (int k = 0; k < COMMANDS; k++, produced++) {
      while (produced - tail == RING) tail = ring.tail.load(std::memory_order_acquire);
      int e = (k * 13 + f) % ENTITIES;
      Command &c = ring.slot[produced % RING];
      c.kind = k & 7, c.entity = e, c.cell = cell_of[e], c.x = px[e], c.y = py[e], c.z = pz[e], c.w = 1.0f;
      c.extra[0] = cell_count[cell_of[e]].load(std::memory_order_relaxed);
      ring.head.store(produced + 1, std::memory_order_release);
    }
  }
  render.join();
  double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  quit = true;
  for (auto &w : workers) w.join();
  double check = 0;
  for (int i = 0; i < ENTITIES; i++) check += px[i] + py[i] + pz[i];
  printf("frames %d total %.4f ms_per_frame %.3f check %.1f render %llu\n", FRAMES, s, s * 1000 / FRAMES, check,
         (unsigned long long)render_sum.load());
}
