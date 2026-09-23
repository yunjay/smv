#ifdef SMV_TRACY
#include "smv_tracy.h"

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#else
#include <unistd.h>
#endif

/* ------------------ SmvTracyFread ------------------------ */

size_t SmvTracyFread(void *ptr, size_t size, size_t count, FILE *stream){
  size_t n;

  SMVZONE("io/read");
  n = fread(ptr, size, count, stream);
  SMVZONE_END();
  return n;
}

/* ------------------ SmvTracyRss ------------------------ */

unsigned long long SmvTracyRss(void){
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS pmc;

  if(GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)) == 0)return 0;
  return pmc.WorkingSetSize;
#elif defined(__APPLE__)
  mach_task_basic_info_data_t info;
  mach_msg_type_number_t n = MACH_TASK_BASIC_INFO_COUNT;

  if(task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &n) != KERN_SUCCESS)return 0;
  return info.resident_size;
#else
  FILE *stream;
  unsigned long long size = 0, resident = 0;
  int nread;

  stream = fopen("/proc/self/statm", "r");
  if(stream == NULL)return 0;
  nread = fscanf(stream, "%llu %llu", &size, &resident);
  fclose(stream);
  if(nread != 2)return 0;
  return resident*(unsigned long long)sysconf(_SC_PAGESIZE);
#endif
}
#endif
