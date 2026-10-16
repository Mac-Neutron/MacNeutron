/* 4a's test ops (proto.h), run by the executor for the guest's transport test. ring_state[0] holds the ring's SET. */
#include <string.h>
#include <time.h>
#include "executor.h"
#include "../proto.h"

int32_t mn_testops(uint32_t ctx_id, void *ring_state, uint32_t op, const void *p, uint32_t len, void *reply,
                   uint32_t cap, uint32_t *reply_len) {
    uint64_t *state = ring_state;
    switch (op) {
    case MN_OP_TEST_ECHO:
        if (len > cap) return MN_E2BIG;
        if (len) memcpy(reply, p, len);
        *reply_len = len;
        return 0;
    case MN_OP_TEST_SET:
        if (len != sizeof *state) return MN_EINVAL;
        memcpy(state, p, sizeof *state);
        return 0;
    case MN_OP_TEST_GET:
        if (cap < sizeof *state) return MN_E2BIG;
        memcpy(reply, state, sizeof *state);
        *reply_len = sizeof *state;
        return 0;
    case MN_OP_TEST_FILL: {
        struct mn_test_fill f;
        if (len != sizeof f) return MN_EINVAL;
        memcpy(&f, p, sizeof f);
        void *dst = mn_executor_blob(ctx_id, f.blob_id, f.offset, f.len);
        if (!dst) return MN_EINVAL;
        memset(dst, (int)f.byte, f.len);
        return 0;
    }
    case MN_OP_TEST_SUM: {
        struct mn_test_sum s;
        uint64_t sum = 0;
        if (len != sizeof s || cap < sizeof sum) return MN_EINVAL;
        memcpy(&s, p, sizeof s);
        const uint8_t *src = mn_executor_blob(ctx_id, s.blob_id, s.offset, s.len);
        if (!src) return MN_EINVAL;
        for (uint64_t i = 0; i < s.len; i++) sum += src[i];
        memcpy(reply, &sum, sizeof sum);
        *reply_len = sizeof sum;
        return 0;
    }
    case MN_OP_TEST_DELAY: {
        uint64_t ns;
        if (len != sizeof ns) return MN_EINVAL;
        memcpy(&ns, p, sizeof ns);
        for (uint64_t until = clock_gettime_nsec_np(CLOCK_UPTIME_RAW) + ns; clock_gettime_nsec_np(CLOCK_UPTIME_RAW) < until;) {}
        return 0;
    }
    default:
        return MN_ENOSYS;
    }
}
