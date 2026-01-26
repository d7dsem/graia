#pragma once
#include <cstdint>
#include <filesystem>
using STD_PATH = std::filesystem::path;

inline std::string now_timestamp_ymd_hms()
{
    auto now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    tm = *std::localtime(&now); // single-thread writer => ok
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d_%H-%M-%S");
    return oss.str();
}

static constexpr size_t HW_PAGE_SZ   = 4096;
static constexpr size_t PACKET_SZ    = 7184;   // fixed packet size
static constexpr size_t BUFF_PKT_LEN = 512;    // 512 * 7184 = 3.5078125 MiB (fits 1..4 MiB sweet spot)
static constexpr size_t BUFF_SZ      = PACKET_SZ * BUFF_PKT_LEN;
static constexpr size_t FILE_BUF_SZ  = 4ull * 1024ull * 1024ull; // stdio setvbuf target (Windows path)

inline size_t align_down(size_t value, size_t align)
{
    return value - (value % align);
}

struct CapturePrm
{
    STD_PATH out_dir = "graia-captured";
    int32_t mch = 0; // for filename
    int64_t freq_hz = 0; // for filename
    size_t file_size_bytes = 0; // must be aligned to PACKET_SZ (7184), for O_DIRECT also to BUFF_SZ (3.5078125 MiB)
    size_t capture_limit = 0; // 0 = unlimited, until free disk space allowed
    std::string interface; // e.g. "enp3s0fp0"
    std::string ip; // e.g. "192.168.30.3"
    uint16_t port = 0; // e.g. 38054
    bool sock_raw = false; // default: bin
};

struct CaptureResult
{
    uint64_t total_captured = 0; // bytes
    uint32_t files_created = 0; // count
    double elapsed_seconds = 0.0; // seconds
    double capture_rate_mbps = 0.0; // MiB/s
    std::string summary() const
    {
        std::ostringstream oss;
        oss << "Capture Summary:\n"
            << "  Total Captured: " << total_captured << " bytes\n"
            << "  Files Created: " << files_created << "\n"
            << "  Elapsed Time: " << elapsed_seconds << " seconds\n"
            << "  Capture Rate: " << capture_rate_mbps << " Mbps\n";
        return oss.str();
    }
};

class IPacketWriter {
public:
    virtual ~IPacketWriter() = default;
    virtual void write(const void* p_pkt) = 0;
    virtual void flush() = 0;
    virtual uint64_t total_written_u64() const = 0;
    virtual uint32_t files_count() const = 0;
};


IPacketWriter* create_rotating_writer(const CapturePrm& prm);

int capture(const CapturePrm prm, CaptureResult& result);
