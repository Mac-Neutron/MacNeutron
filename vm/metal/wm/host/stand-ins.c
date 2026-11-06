/* The stand-in for ntdll.so, one of the two Wine libraries MacNeutron's winemetal.so links (@rpath/ntdll.so; winemac.m
 * is the other), so the VM host can load it: winemetal imports only NtSetEvent, which signals a guest Win32 event here,
 * through the hook the executor sets. */
#include <stdint.h>

static void (*_Atomic hook)(uint64_t nt_event);

void mn_ntdll_set_hook(void (*h)(uint64_t nt_event)) { hook = h; }

unsigned int NtSetEvent(void *handle, int *prev_state) {
    void (*h)(uint64_t) = hook;
    if (prev_state) *prev_state = 0;
    if (h) h((uint64_t)(uintptr_t)handle);
    return 0;
}
