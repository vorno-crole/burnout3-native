// uncaught.h -- musicexit (2026-11-23): an uncaught C++ exception reported before the process ends. A test build on
// Windows ended at exit with only libstdc++'s "terminate called after throwing an instance of 'std::system_error' /
// what(): Invalid argument" in its run log: abort() does not pass Windows' unhandled-exception filter (the crash report
// of tools/native/crash_report.h), so no thread and no frames. A std::terminate handler says which exception, on which
// thread, from where.
//   uncaught_exception_text()     the exception std::terminate is ending the process for: its type (demangled), what()
//                                 and, for a std::system_error, its code ("no active exception": a joinable std::thread
//                                 destroyed, or std::terminate called directly); host-testable from inside a catch
//   module_of(addr, name, cap)    Windows: the module an address is in (its file name) and the offset from its base
//   write_frames(f)               the calling thread's frames: Windows "  #k <module>+0x<offset>" (addr2line -e <the
//                                 unstripped build of the same commit> <image base + offset>); POSIX backtrace_symbols
//   install_uncaught_report(m)    std::set_terminate for this module (native_play.exe and b3render.dll each link their
//                                 own libstdc++, so each has its own handler): "crash: uncaught C++ exception in <m>: ...",
//                                 the thread, the frames, then exit 4. native_play's own handler is crash_report.h's (the
//                                 same lines plus the native chain, through the run log's flush).
#ifndef B3REPORT_UNCAUGHT_H
#define B3REPORT_UNCAUGHT_H

#include "b3report/thread_name.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <system_error>
#include <typeinfo>

#include <cxxabi.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <execinfo.h>
#include <unistd.h>
#endif

namespace b3port {

inline std::string demangled(const char* name) {
    if (!name) return "?";
    int status = 0;
    char* d = abi::__cxa_demangle(name, nullptr, nullptr, &status);
    std::string s = status == 0 && d ? d : name;
    std::free(d);
#ifdef _LIBCPP_VERSION   // macintel1: libc++'s ABI namespace ("std::__1::system_error"), named as libstdc++ does
    for (std::size_t at; (at = s.find("std::__1::")) != std::string::npos;) s.erase(at + 5, 5);
#endif
    return s;
}

inline std::string uncaught_exception_text() {
    const std::exception_ptr e = std::current_exception();
    if (!e) return "no active exception (a joinable std::thread destroyed, or std::terminate called directly)";
    const std::type_info* t = abi::__cxa_current_exception_type();
    std::string s = t ? demangled(t->name()) : std::string("?");
    try {
        std::rethrow_exception(e);
    } catch (const std::system_error& x) {
        s += ": " + std::string(x.what()) + " (" + x.code().category().name() + " " + std::to_string(x.code().value()) + ")";
    } catch (const std::exception& x) {
        s += ": " + std::string(x.what());
    } catch (...) {
        s += " (not a std::exception)";
    }
    return s;
}

#ifdef _WIN32
inline std::uintptr_t module_of(const void* addr, char* name, std::size_t cap) {
    HMODULE m = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCWSTR>(addr), &m) || !m) {
        std::snprintf(name, cap, "?");
        return reinterpret_cast<std::uintptr_t>(addr);
    }
    wchar_t w[MAX_PATH];
    const DWORD len = GetModuleFileNameW(m, w, MAX_PATH);
    DWORD from = 0;
    for (DWORD i = 0; i < len; ++i) if (w[i] == L'\\' || w[i] == L'/') from = i + 1;
    std::size_t o = 0;
    for (DWORD i = from; i < len && o + 1 < cap; ++i) name[o++] = w[i] < 0x80 ? char(w[i]) : '?';
    name[o] = 0;
    if (o == 0) std::snprintf(name, cap, "?");
    return reinterpret_cast<std::uintptr_t>(addr) - reinterpret_cast<std::uintptr_t>(m);
}
#endif

inline void write_frames(std::FILE* f) {
    std::fflush(f);
#ifdef _WIN32
    void* bt[62];
    const unsigned short n = CaptureStackBackTrace(0, 62, bt, nullptr);
    for (unsigned short k = 0; k < n; ++k) {
        char mod[MAX_PATH];
        const std::uintptr_t off = module_of(bt[k], mod, sizeof mod);
        std::fprintf(f, "  #%u %s+0x%llx\n", k, mod, static_cast<unsigned long long>(off));
    }
#else
    void* bt[64];
    const int n = backtrace(bt, 64);
    backtrace_symbols_fd(bt, n, fileno(f));
#endif
    std::fflush(f);
}

namespace detail {
inline const char*& uncaught_module() {
    static const char* m = "?";
    return m;
}
[[noreturn]] inline void uncaught_report() {
    static thread_local bool inside = false;   // a second terminate from the report itself: end now
    if (inside) std::_Exit(4);
    inside = true;
    const std::string what = uncaught_exception_text();
    const char* named = named_here();
    std::fprintf(stdout, "crash: uncaught C++ exception in %s: %s\n", uncaught_module(), what.c_str());
    if (*named) std::fprintf(stdout, "crash: thread \"%s\"\n", named);
    else std::fprintf(stdout, "crash: thread \"%s\" (the OS's name)\n", this_thread_name().c_str());
    write_frames(stdout);
#ifdef _WIN32
    Sleep(300);   // native_play's run log readers (tee mode) copy the lines before the process ends
#endif
    std::_Exit(4);
}
}  // namespace detail

inline void install_uncaught_report(const char* module) {
    detail::uncaught_module() = module;
    std::set_terminate(detail::uncaught_report);
}

}  // namespace b3port

#endif
