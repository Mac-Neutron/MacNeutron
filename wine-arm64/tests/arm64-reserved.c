// map_reserved_area's top-down search inside a reserved area (Wine patch 0042): the low 4 GiB of an arm64 process is
// the macOS soft pagezero, reserved, so an allocation bounded there is placed by that search. A 64 KiB reservation
// asked for top-down in a 48 KiB window must fail or land inside the window; the search took the area's own start for
// its lower bound and placed it below the window. Then a 128 KiB window, which fits it, must place it inside.
#include <windows.h>
#include <stdio.h>

static void *alloc(ULONG_PTR lowest, ULONG_PTR highest) {
  MEM_ADDRESS_REQUIREMENTS req = {(void *)lowest, (void *)highest, 0};
  MEM_EXTENDED_PARAMETER param = {0};
  param.Type = MemExtendedParameterAddressRequirements;
  param.Pointer = &req;
  return VirtualAlloc2(NULL, NULL, 0x10000, MEM_RESERVE | MEM_TOP_DOWN, PAGE_READWRITE, &param, 1);
}

int main(void) {
  const ULONG_PTR low = 0x30000000;
  int fail = 0;
  setvbuf(stdout, NULL, _IONBF, 0);
  BYTE *p = alloc(low, low + 0xc000 - 1);
  if (p && ((ULONG_PTR)p < low || (ULONG_PTR)p + 0x10000 > low + 0xc000)) {
    printf("FAIL arm64-reserved: 64 KiB in a 48 KiB window at %p, outside it\n", p);
    fail = 1;
  } else {
    printf("ok   arm64-reserved: 64 KiB in a 48 KiB window: %s\n", p ? "inside" : "refused");
  }
  if (p) VirtualFree(p, 0, MEM_RELEASE);
  p = alloc(low, low + 0x20000 - 1);
  if (!p || (ULONG_PTR)p < low || (ULONG_PTR)p + 0x10000 > low + 0x20000) {
    printf("FAIL arm64-reserved: 64 KiB in a 128 KiB window at %p (error %lu)\n", p, GetLastError());
    fail = 1;
  } else {
    printf("ok   arm64-reserved: 64 KiB in a 128 KiB window at %p\n", p);
  }
  if (p) VirtualFree(p, 0, MEM_RELEASE);
  if (!fail) printf("PASS arm64-reserved\n");
  return fail;
}
