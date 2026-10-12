// 64-bit atomic test helpers (DXIL SM 6.8 plan, Tasks 14a-14c). Q(j) has random bits in both halves; V adds to both
// halves, so a torn add shows. A thread-owned sequence of op kind k = i % 12 (0 Add, 1 And, 2 Or, 3 Xor, 4 IMin,
// 5 IMax, 6 UMin, 7 UMax, 8 Exchange, 9 CompareExchange, 10 CompareStore, 11 Add across the carry) on a zeroed
// location: Exchange x0; three steps with a, b, c whose olds are o1-o3; Exchange 0, whose old o4 is the final value.
// CompareExchange: match, no match, match. CompareStore: match, no match, then Exchange c (o3 = a); o1 = o2 = 0.
// expect.py's own64() replays it.
static const uint64_t V = 0x100000001;
uint64_t Q(uint j) { return (uint64_t)InWord(j) << 32 | InWord(j + 256); }
void Put64(uint3 id, uint s, uint64_t v) { Put(id, s, (uint)v); Put(id, s + 1, (uint)(v >> 32)); }
#define OPERANDS(i) uint k = (i) % 12; uint64_t x0 = Q(i), a = Q((i) + 64), b = Q((i) + 128), c = Q((i) + 192), \
    o1 = 0, o2 = 0, o3 = 0, o4 = 0; if (k == 11) { x0 = 0xffffffff; a = 1; b = 0xffffffff; c = V; }
// On an lvalue L (structured, typed, texture or groupshared) of element type T.
#define STEPS(F, L, T) { T o_; InterlockedExchange(L, (T)x0, o_); F(L, (T)a, o_); o1 = o_; F(L, (T)b, o_); o2 = o_; \
    F(L, (T)c, o_); o3 = o_; InterlockedExchange(L, (T)0, o_); o4 = o_; }
// Kind k on L (uint64_t); the signed kinds on LS (int64_t: the same bytes, or a groupshared twin).
#define OWN(L, LS) switch (k) { \
    case 0: case 11: STEPS(InterlockedAdd, L, uint64_t) break; case 1: STEPS(InterlockedAnd, L, uint64_t) break; \
    case 2: STEPS(InterlockedOr, L, uint64_t) break;           case 3: STEPS(InterlockedXor, L, uint64_t) break; \
    case 4: STEPS(InterlockedMin, LS, int64_t) break;          case 5: STEPS(InterlockedMax, LS, int64_t) break; \
    case 6: STEPS(InterlockedMin, L, uint64_t) break;          case 7: STEPS(InterlockedMax, L, uint64_t) break; \
    case 8: STEPS(InterlockedExchange, L, uint64_t) break; \
    case 9: InterlockedExchange(L, x0, o1); InterlockedCompareExchange(L, x0, a, o1); \
            InterlockedCompareExchange(L, x0, b, o2); InterlockedCompareExchange(L, a, c, o3); \
            InterlockedExchange(L, (uint64_t)0, o4); break; \
    default: InterlockedExchange(L, x0, o1); InterlockedCompareStore(L, x0, a); InterlockedCompareStore(L, x0, b); \
             InterlockedExchange(L, c, o3); InterlockedExchange(L, (uint64_t)0, o4); o1 = 0; }
// The same on Raw64's qword at byte `off`, through its methods (InterlockedMin64 with an int64_t value is the signed one).
#define RSTEPS(F, T) { T o_; uint64_t u_; Raw64.InterlockedExchange64(off, x0, u_); Raw64.F(off, (T)a, o_); o1 = o_; \
    Raw64.F(off, (T)b, o_); o2 = o_; Raw64.F(off, (T)c, o_); o3 = o_; Raw64.InterlockedExchange64(off, (uint64_t)0, u_); \
    o4 = u_; }
#define OWNRAW switch (k) { \
    case 0: case 11: RSTEPS(InterlockedAdd64, uint64_t) break; case 1: RSTEPS(InterlockedAnd64, uint64_t) break; \
    case 2: RSTEPS(InterlockedOr64, uint64_t) break;           case 3: RSTEPS(InterlockedXor64, uint64_t) break; \
    case 4: RSTEPS(InterlockedMin64, int64_t) break;           case 5: RSTEPS(InterlockedMax64, int64_t) break; \
    case 6: RSTEPS(InterlockedMin64, uint64_t) break;          case 7: RSTEPS(InterlockedMax64, uint64_t) break; \
    case 8: RSTEPS(InterlockedExchange64, uint64_t) break; \
    case 9: Raw64.InterlockedExchange64(off, x0, o1); Raw64.InterlockedCompareExchange64(off, x0, a, o1); \
            Raw64.InterlockedCompareExchange64(off, x0, b, o2); Raw64.InterlockedCompareExchange64(off, a, c, o3); \
            Raw64.InterlockedExchange64(off, (uint64_t)0, o4); break; \
    default: Raw64.InterlockedExchange64(off, x0, o1); Raw64.InterlockedCompareStore64(off, x0, a); \
             Raw64.InterlockedCompareStore64(off, x0, b); Raw64.InterlockedExchange64(off, c, o3); \
             Raw64.InterlockedExchange64(off, (uint64_t)0, o4); o1 = 0; }
