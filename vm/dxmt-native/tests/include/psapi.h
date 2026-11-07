// psapi's GetProcessMemoryInfo for DXMT's test programs built natively (d3d12_timestamp's working-set line): the
// task's resident size.
#pragma once
#include <windows.h>
#include <mach/mach.h>

typedef struct PROCESS_MEMORY_COUNTERS {
  DWORD cb;
  DWORD PageFaultCount;
  SIZE_T PeakWorkingSetSize, WorkingSetSize, QuotaPeakPagedPoolUsage, QuotaPagedPoolUsage;
  SIZE_T QuotaPeakNonPagedPoolUsage, QuotaNonPagedPoolUsage, PagefileUsage, PeakPagefileUsage;
} PROCESS_MEMORY_COUNTERS;

static inline BOOL GetProcessMemoryInfo(HANDLE, PROCESS_MEMORY_COUNTERS *counters, DWORD cb) {
  mach_task_basic_info_data_t info;
  mach_msg_type_number_t n = MACH_TASK_BASIC_INFO_COUNT;
  if (!counters || cb < sizeof *counters ||
      task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &n) != KERN_SUCCESS)
    return FALSE;
  memset(counters, 0, sizeof *counters);
  counters->cb = sizeof *counters;
  counters->WorkingSetSize = info.resident_size;
  counters->PeakWorkingSetSize = info.resident_size_max;
  return TRUE;
}
