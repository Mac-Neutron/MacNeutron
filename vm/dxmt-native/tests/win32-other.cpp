// win32-test's second translation unit: an event made in the first, set from here, wakes the first's wait, as a
// test program's event set by DXMT's fence (another binary, the same header).
#include <win32_native.h>

void set_from_another_object(HANDLE event) { SetEvent(event); }
