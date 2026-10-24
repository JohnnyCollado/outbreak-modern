// Force-included into PCSX2's common/Linux/LnxHostSys.cpp on Android (CMakeLists.txt): bionic has no POSIX
// shared memory (shm_open/shm_unlink). HostSys::CreateSharedMemory only needs an anonymous file descriptor it can
// ftruncate and mmap in this process, which memfd gives; android_common.cpp implements both on top of it.
#pragma once

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif
int shm_open(const char* name, int oflag, mode_t mode);
int shm_unlink(const char* name);
#ifdef __cplusplus
}
#endif
