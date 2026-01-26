#pragma ones
#include <filesystem>
#include <string>
#include <cstdint>
#include <ostream>
#include <chrono>


using STD_PATH = std::filesystem::path;
using OSS = std::ostringstream;

using tick_t = std::chrono::steady_clock::time_point;
using duration_t = std::chrono::steady_clock::duration;
#define SEC_FROM_DUR(duration) (std::chrono::duration_cast<std::chrono::milliseconds>((duration)).count()/1000.0)
#define DUR_FROM_SEC(duration_sec) (duration_t(static_cast<long long>((duration_sec) * 1e9)))

namespace GRAIA{


}  // namespace GRAIA