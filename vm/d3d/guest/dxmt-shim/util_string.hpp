// mnd3d.dll's stand-in for DXMT's string helpers (M3 Task 3): what its Win32 window code uses.
#pragma once
#include <sstream>
#include <string>

namespace dxmt::str {
template <class... Args> std::string format(const Args &...args) {
  std::ostringstream s;
  (s << ... << args);
  return s.str();
}
} // namespace dxmt::str
