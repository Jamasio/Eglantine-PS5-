// SPDX-License-Identifier: GPL-3.0-or-later
// When the app crashes: a report in the logs folder, then the app starts again and the launcher
// says where the report is.
//
// A fatal signal (a bad memory access, an illegal instruction, abort) reaches a handler that
// writes logs/crash-YYYYMMDD-HHMMSS.txt: what failed and where, the registers, the app's code
// addresses found on the stack (the calls that led there), the thread, what was running and the
// memory left. It uses system calls and its own buffers only: the heap and the C library's
// streams may be what broke. A thread that has waited since startup then starts the app again
// (src/lifecycle.c). When that does not happen within a few seconds the signal takes its usual
// course, and the system handles the crash as it did before this existed.
//
// The report holds addresses, never memory contents. tools/symbolize-crash.py turns them into
// function names with the build's llvm-pie.elf.
//
// Not covered: a crash before Install. Without EDEN_SIGNAL_STACK=1 also a stack overflow (the
// handler needs the thread's own stack) and a fault inside a fault handler (the signal is masked
// there): the kernel ends the app with no handler run, which the console shows as CE-108255-1.
//
// EDEN_SIGNAL_STACK=1 (env.txt): every thread that names itself gets 32 KiB of its own for signals,
// under a guard page, in the flexible pool (48 KiB in all a thread; a run has about 50 threads, and
// fibers share their thread's), and a handler may fault again. Off unless asked for: nothing here
// has run on the console yet.
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <pthread.h>
#include <string>
#include <string_view>

namespace Eden::Crash {
// Threads that named themselves (the SetCurrentThreadName hook, performance.cpp), for the
// report's "thread" line. Lock-free: the handler reads it on a thread that may hold any lock.
struct NamedThread {
    std::atomic<std::uintptr_t> thread{0};
    char name[24]{};
};
inline constexpr unsigned kNamedThreads = 192;
inline NamedThread named_threads[kNamedThreads];
inline std::atomic<unsigned> named_thread_count{0};

inline void NameThread(const char* name) noexcept {
    const auto self = (std::uintptr_t)pthread_self();
    const unsigned count = named_thread_count.load(std::memory_order_relaxed);
    NamedThread* entry = nullptr;
    // A finished thread's handle is given to a later thread, which takes over its entry.
    for (unsigned i = 0; i < kNamedThreads && i < count && !entry; ++i)
        if (named_threads[i].thread.load(std::memory_order_relaxed) == self) entry = &named_threads[i];
    // A full table starts over with its oldest entries.
    if (!entry) entry = &named_threads[named_thread_count.fetch_add(1, std::memory_order_relaxed) % kNamedThreads];
    entry->thread.store(0, std::memory_order_relaxed);
    std::size_t length = 0;
    for (; name && name[length] && length + 1 < sizeof(entry->name); ++length) entry->name[length] = name[length];
    entry->name[length] = '\0';
    entry->thread.store(self, std::memory_order_release);
}

// How the launcher is told of a report: its launch error is this, followed by the report's path
// (eglantineps5/eden_services.cpp).
inline constexpr std::string_view kNotice = "crash-report:";

// What the previous run left behind.
struct Last {
    std::string report;      // the report's path; empty when that run did not crash
    bool restarted = false;  // the handler started this process
};
// At startup, once the previous run's logs are set aside (stderr.prev.log, heap.prev.log): when
// that run ended with a report the launcher has not announced, those logs and Eden's own log
// move beside it, and the oldest of more than five reports go.
Last TakeLast(const std::string& logs_folder, const std::string& eden_log);

// Once, on the main thread, when stderr and stdout go to their log files.
void Install(const std::string& logs_folder, const char* version, bool restarted);

// After env.txt, on the main thread (env.txt is read after Install): EDEN_SIGNAL_STACK=1 takes
// effect here. Before a game starts: the handlers installed from now on carry the flags.
void UseSignalStacks() noexcept;
// The calling thread's signal stack: true when it has one. Nothing while the switch is off.
// RegisterWorker calls it (through Fastmem::thread_signal_stack), so a thread that never names
// itself keeps the old behaviour.
bool ThreadSignalStack() noexcept;
// EDEN_CRASH_TEST (env.txt): a crash on purpose, right after UseSignalStacks and before the launcher,
// to see on a console what each kind ends as:
//   stack    a fault with the stack pointer in a guard page (a stack overflow)
//   nested   a fault inside the crash handler
// With the switch on each writes a report; without it they show what the kernel does instead.
void RunCrashTest() noexcept;

// What is running, one line for the report. A game the user started also ends the watch for a
// crash right after a restart (which closes the app instead of starting it over and over).
void SetSession(const std::string& text, bool game) noexcept;

// The app found an error it cannot continue from (an exception nothing caught): the report says
// why, then the same course as for a signal. Does not return.
[[noreturn]] void Fail(const char* reason) noexcept;

// For the checks (tools/check-crash-report.py).
bool Readable(const void* address, std::size_t bytes) noexcept;
} // namespace Eden::Crash
