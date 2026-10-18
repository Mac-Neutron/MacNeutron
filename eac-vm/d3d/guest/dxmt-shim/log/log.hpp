// mnd3d.dll's stand-in for DXMT's logger (M3 Task 3): DXMT's Win32 window code, built into it (build-guest.sh), says
// its errors on stderr, as the proxies do, and writes no log file.
#pragma once
#include <cstdio>
#include <string>

namespace dxmt {
struct Logger {
  static void err(const std::string &s) { std::fprintf(stderr, "mn-d3d: wsi: %s\n", s.c_str()); }
  static void warn(const std::string &s) { err(s); }
  static void info(const std::string &) {}
  static void debug(const std::string &) {}
};
} // namespace dxmt
