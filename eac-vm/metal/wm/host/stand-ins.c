/* Stand-ins for the two Wine libraries MacNeutron's winemetal.so links (@rpath/ntdll.so and @rpath/winemac.so), so
 * the VM host can load it. Built with -DMN_NTDLL as ntdll.so: winemetal imports only NtSetEvent, which signals a guest
 * Win32 event here, through the hook the executor sets. Built without it as winemac.so, empty: winemetal reaches
 * winemac's functions with dlsym, finds none, and its window calls do nothing (presentation is milestone 4c). */
#ifdef MN_NTDLL
#include <stdint.h>

static void (*_Atomic hook)(uint64_t nt_event);

void mn_ntdll_set_hook(void (*h)(uint64_t nt_event)) { hook = h; }

unsigned int NtSetEvent(void *handle, int *prev_state) {
    void (*h)(uint64_t) = hook;
    if (prev_state) *prev_state = 0;
    if (h) h((uint64_t)(uintptr_t)handle);
    return 0;
}
#endif
