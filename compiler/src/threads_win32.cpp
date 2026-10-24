// threads_win32.cpp - the Windows API side of threads.h, used only by
// compilers whose C++ library has no std::thread (for example old MinGW).
// On every other compiler this file compiles to nothing.
#include "threads.h"

#ifdef CS_WIN32_THREADS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace cs {

mutex::mutex() : impl_(new CRITICAL_SECTION) { InitializeCriticalSection(static_cast<CRITICAL_SECTION*>(impl_)); }
mutex::~mutex() {
    DeleteCriticalSection(static_cast<CRITICAL_SECTION*>(impl_));
    delete static_cast<CRITICAL_SECTION*>(impl_);
}
void mutex::lock() { EnterCriticalSection(static_cast<CRITICAL_SECTION*>(impl_)); }
void mutex::unlock() { LeaveCriticalSection(static_cast<CRITICAL_SECTION*>(impl_)); }

void sleepMs(unsigned ms) { Sleep(ms); }

static DWORD WINAPI runJob(LPVOID p) {
    std::function<void()>* job = static_cast<std::function<void()>*>(p);
    (*job)();
    delete job;
    return 0;
}

void thread::start(std::function<void()>* job) {
    h_ = CreateThread(nullptr, 16 * 1024 * 1024, &runJob, job, 0, nullptr);
    if (!h_) { (*job)(); delete job; }   // could not start a thread: do the work here
}
thread::~thread() { if (h_) CloseHandle(static_cast<HANDLE>(h_)); }
void thread::join() {
    if (h_) { WaitForSingleObject(static_cast<HANDLE>(h_), INFINITE); CloseHandle(static_cast<HANDLE>(h_)); h_ = nullptr; }
}

unsigned hardwareThreads() {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (unsigned)si.dwNumberOfProcessors;
}

}  // namespace cs
#endif
