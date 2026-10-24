// ============================================================================
// threads.h - threads, mutex and condition variable for the batch thread pool
// ----------------------------------------------------------------------------
// Normally these are just std::thread, std::mutex, std::condition_variable.
// Old MinGW compilers (for example mingw.org GCC 6.3, "win32" thread model)
// ship a C++ library WITHOUT std::thread / std::mutex. For them we provide the
// same small interface on top of the Windows API (CreateThread,
// CRITICAL_SECTION), so the project builds on any lab machine.
// ============================================================================
#pragma once
#include <cstddef>   // defines __GLIBCXX__ / _GLIBCXX_HAS_GTHREADS when libstdc++ is used

#if defined(_WIN32) && (defined(CS_FORCE_WIN32_THREADS) || (defined(__GLIBCXX__) && !defined(_GLIBCXX_HAS_GTHREADS)))
#define CS_WIN32_THREADS 1
#endif

#ifndef CS_WIN32_THREADS
#include <condition_variable>
#include <mutex>
#include <thread>
namespace cs {
using std::condition_variable;
using std::lock_guard;
using std::mutex;
using std::thread;
using std::unique_lock;
inline unsigned hardwareThreads() { return std::thread::hardware_concurrency(); }
}  // namespace cs

#else  // ---------------- Windows API version for compilers without std::thread
// The Windows API is used only inside threads_win32.cpp: <windows.h> defines
// names such as INT, FLOAT and CHAR that clash with the parser's token names.
#include <functional>
#include <utility>

namespace cs {

class mutex {
public:
    mutex();
    ~mutex();
    mutex(const mutex&) = delete;
    mutex& operator=(const mutex&) = delete;
    void lock();
    void unlock();
private:
    void* impl_;   // a CRITICAL_SECTION
};

template <typename M>
class lock_guard {
public:
    explicit lock_guard(M& m) : m_(m) { m_.lock(); }
    ~lock_guard() { m_.unlock(); }
    lock_guard(const lock_guard&) = delete;
    lock_guard& operator=(const lock_guard&) = delete;
private:
    M& m_;
};

template <typename M>
class unique_lock {
public:
    explicit unique_lock(M& m) : m_(m), owns_(true) { m_.lock(); }
    ~unique_lock() { if (owns_) m_.unlock(); }
    unique_lock(const unique_lock&) = delete;
    unique_lock& operator=(const unique_lock&) = delete;
    void lock() { m_.lock(); owns_ = true; }
    void unlock() { m_.unlock(); owns_ = false; }
private:
    M& m_;
    bool owns_;
};

void sleepMs(unsigned ms);

// A simple condition variable: wait() releases the lock and checks the
// condition again every millisecond. Good enough for a batch of compilations.
class condition_variable {
public:
    template <typename Pred>
    void wait(unique_lock<mutex>& lk, Pred pred) {
        while (!pred()) { lk.unlock(); sleepMs(1); lk.lock(); }
    }
    void notify_all() {}
    void notify_one() {}
};

class thread {
public:
    thread() : h_(nullptr) {}
    template <typename F>
    explicit thread(F f) : h_(nullptr) { start(new std::function<void()>(f)); }
    thread(thread&& o) noexcept : h_(o.h_) { o.h_ = nullptr; }
    thread& operator=(thread&& o) noexcept { std::swap(h_, o.h_); return *this; }
    thread(const thread&) = delete;
    thread& operator=(const thread&) = delete;
    ~thread();
    void join();
private:
    void start(std::function<void()>* job);
    void* h_;   // a HANDLE
};

unsigned hardwareThreads();

}  // namespace cs
#endif
