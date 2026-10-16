/* Milestone 4a Task 2 probe in the guest: the virtio-gpu node, a dxmt context, a mapped blob the host reads and
 * answers through, and a fence. One "ok" line per step; "FAIL <step>: <why>" and exit 1 at the first miss. */
#include <drm/virtgpu_drm.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include "../proto.h"

static int fail(const char *step, const char *why) {
    printf("FAIL %s: %s\n", step, why);
    return 1;
}

static uint64_t param(int fd, uint64_t which) {
    uint64_t v = 0;
    struct drm_virtgpu_getparam gp = { .param = which, .value = (uintptr_t)&v };
    return ioctl(fd, DRM_IOCTL_VIRTGPU_GETPARAM, &gp) ? 0 : v;
}

int main(void) {
    int fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
    if (fd < 0) return fail("renderD128", strerror(errno));
    puts("renderD128 ok");

    if (!param(fd, VIRTGPU_PARAM_CONTEXT_INIT) || !param(fd, VIRTGPU_PARAM_RESOURCE_BLOB) ||
        !param(fd, VIRTGPU_PARAM_HOST_VISIBLE))
        return fail("context_init", "the device lacks CONTEXT_INIT, RESOURCE_BLOB or HOST_VISIBLE");
    if (!(param(fd, VIRTGPU_PARAM_SUPPORTED_CAPSET_IDs) & (1ull << MN_CAPSET_DXMT)))
        { printf("FAIL capset %d: not offered\n", MN_CAPSET_DXMT); return 1; }
    struct drm_virtgpu_context_set_param ps[] = {
        { VIRTGPU_CONTEXT_PARAM_CAPSET_ID, MN_CAPSET_DXMT },
        { VIRTGPU_CONTEXT_PARAM_NUM_RINGS, 64 },
    };
    struct drm_virtgpu_context_init ci = { .num_params = 2, .ctx_set_params = (uintptr_t)ps };
    if (ioctl(fd, DRM_IOCTL_VIRTGPU_CONTEXT_INIT, &ci)) return fail("context_init", strerror(errno));
    puts("context_init ok");
    printf("capset %d ok\n", MN_CAPSET_DXMT);

    struct drm_virtgpu_resource_create_blob cb = {
        .blob_mem = VIRTGPU_BLOB_MEM_HOST3D, .blob_flags = VIRTGPU_BLOB_FLAG_USE_MAPPABLE, .size = MN_PAGE, .blob_id = 1,
    };
    if (ioctl(fd, DRM_IOCTL_VIRTGPU_RESOURCE_CREATE_BLOB, &cb)) return fail("blob", strerror(errno));
    struct drm_virtgpu_map m = { .handle = cb.bo_handle };
    if (ioctl(fd, DRM_IOCTL_VIRTGPU_MAP, &m)) return fail("blob", strerror(errno));
    volatile uint64_t *p = mmap(NULL, MN_PAGE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)m.offset);
    if (p == MAP_FAILED) return fail("blob", strerror(errno));
    puts("blob ok");

    *p = MN_PROBE_MAGIC;
    struct mn_ctrl c = { .type = MN_CTRL_PEEK, .ring_blob = 1, .arg = 0 };
    struct drm_virtgpu_execbuffer eb = {
        .flags = VIRTGPU_EXECBUF_RING_IDX | VIRTGPU_EXECBUF_FENCE_FD_OUT,
        .size = sizeof c, .command = (uintptr_t)&c, .fence_fd = -1, .ring_idx = 1,
    };
    if (ioctl(fd, DRM_IOCTL_VIRTGPU_EXECBUFFER, &eb)) return fail("fence", strerror(errno));
    struct pollfd pf = { .fd = eb.fence_fd, .events = POLLIN };
    if (poll(&pf, 1, 1000) != 1) return fail("fence", "not signalled within 1 s");
    if (*p != MN_PROBE_MAGIC + 1) return fail("host saw guest bytes", "no answer in the blob");
    puts("host saw guest bytes ok");
    puts("fence ok");
    return 0;
}
