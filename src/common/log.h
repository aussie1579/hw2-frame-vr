#pragma once

// hw2vr.log next to the DLL. Thread-safe, line-buffered; verbose lines are gated by [debug] verbose in hw2vr.ini.
namespace hw2vr::log {

void init(const wchar_t* path);
void shutdown();
void set_verbose(bool on);
bool verbose();
void write(const char* fmt, ...);

}  // namespace hw2vr::log

#define HW2VR_LOG(...) ::hw2vr::log::write(__VA_ARGS__)
#define HW2VR_VLOG(...)                                       \
    do {                                                      \
        if (::hw2vr::log::verbose()) ::hw2vr::log::write(__VA_ARGS__); \
    } while (0)
