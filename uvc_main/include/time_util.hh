#pragma once

#include <chrono>

namespace my_app {

inline double mono_elapsed_ms(const std::chrono::steady_clock::time_point& t0) {
  using std::chrono::duration;
  using std::chrono::steady_clock;
  return duration<double, std::milli>(steady_clock::now() - t0).count();
}

inline double mono_elapsed_ms_between(const std::chrono::steady_clock::time_point& t0,
                                        const std::chrono::steady_clock::time_point& t1) {
  using std::chrono::duration;
  return duration<double, std::milli>(t1 - t0).count();
}

}  // namespace my_app
