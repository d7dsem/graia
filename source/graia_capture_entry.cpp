#include "graia.hpp"

#include <vector>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <memory>
#include <algorithm>
#include <stdexcept>



size_t parse_size_string(const std::string& str)
{
    // Parses size strings like "1K", "10M", "2G" into byte counts.
    size_t len = str.length();
    if (len == 0) {
        throw std::runtime_error("Invalid size string: empty");
    }

    char suffix = str[len - 1];
    size_t multiplier = 1;
    std::string number_part = str;

    if (suffix == 'K' || suffix == 'k') {
        multiplier = 1024ull;
        number_part = str.substr(0, len - 1);
    } else if (suffix == 'M' || suffix == 'm') {
        multiplier = 1024ull * 1024ull;
        number_part = str.substr(0, len - 1);
    } else if (suffix == 'G' || suffix == 'g') {
        multiplier = 1024ull * 1024ull * 1024ull;
        number_part = str.substr(0, len - 1);
    }

    size_t number = std::stoull(number_part);
    return number * multiplier;
}
// Сommand-line options structure. 
// graia_capture_tst -m 0 -f 428000000 --output-dir /data/captured --file-size 1G --src enp3s0fp0:192.168.30.3:50000 --limit 10G
struct CliOptions{
    uint32_t mch;
    int64_t freq_hz;
    std::string src_interface;
    STD_PATH output_dir;
    std::string file_sz_str;
    std::string limit_str;

    size_t file_size_bytes;
    size_t capture_limit;

    void parse(int argc, char* argv[])
    {
        if (argc < 2) {
            throw std::runtime_error("Usage: graia_capture -m <mch> -f <freq_hz> --src <interface:ip:port> [--output-dir <dir>] [--file-size <size>] [--limit <size>]");
        }

        // defaults
        output_dir = "graia-captured";
        file_sz_str = "16M";
        limit_str = "60M";

        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];

            if (arg == "-m" || arg == "--mch") {
                if (i + 1 >= argc) throw std::runtime_error("Missing value for " + arg);
                mch = static_cast<uint32_t>(std::stoul(argv[++i]));
            }
            else if (arg == "-f" || arg == "--freq") {
                if (i + 1 >= argc) throw std::runtime_error("Missing value for " + arg);
                freq_hz = std::stoll(argv[++i]);
            }
            else if (arg == "--src") {
                if (i + 1 >= argc) throw std::runtime_error("Missing value for " + arg);
                src_interface = argv[++i];
            }
            else if (arg == "--output-dir") {
                if (i + 1 >= argc) throw std::runtime_error("Missing value for " + arg);
                output_dir = argv[++i];
            }
            else if (arg == "--file-size") {
                if (i + 1 >= argc) throw std::runtime_error("Missing value for " + arg);
                file_sz_str = argv[++i];
            }
            else if (arg == "--limit") {
                if (i + 1 >= argc) throw std::runtime_error("Missing value for " + arg);
                limit_str = argv[++i];
            }
            else {
                throw std::runtime_error("Unknown argument: " + arg);
            }
        }

        // validate required
        if (src_interface.empty()) {
            throw std::runtime_error("Missing required argument: --src");
        }

        // parse sizes
        file_size_bytes = parse_size_string(file_sz_str);
        capture_limit = parse_size_string(limit_str);
    }

    void print() const
    {
        // String representation logic here
        std::cout << "MCH: " << mch << "\n";
        std::cout << "Frequency (Hz): " << freq_hz << "\n";
        std::cout << "Source Interface: " << src_interface << "\n";
        std::cout << "Output Directory: " << output_dir << "\n";
        std::cout << "File Size " << file_sz_str <<  ". Bytes: requested=" << file_size_bytes << " aligned=" << _aligned_file_size_bytes() << "\n";
        std::cout << "Capture Limit " << limit_str << " Bytes: requested=" << capture_limit << " aligned=" << _aligned_capture_limit() << "\n";
    }

    size_t _aligned_file_size_bytes() const
    {
        // CRITICAL: File size MUST be BUFF_SZ-aligned for rotation logic
        // (see RotatingWriter constructor comment for details)
        if (file_size_bytes % BUFF_SZ != 0) {
            std::cerr << "Warning: file size not aligned to BUFF_SZ, aligning down.\n";
            return align_down(file_size_bytes, BUFF_SZ);
        }
        return file_size_bytes;
    }

    size_t _aligned_capture_limit() const
    {
        // Capture limit should be BUFF_SZ-aligned to avoid partial chunks at limit
        // (not critical, but cleaner - last file will be complete BUFF_SZ multiple)
        if (capture_limit != 0 && capture_limit % BUFF_SZ != 0) {
            std::cerr << "Warning: capture limit not aligned to BUFF_SZ, aligning down.\n";
            return align_down(capture_limit, BUFF_SZ);
        }
        return capture_limit;
    }

    uint16_t port() const
    {
        // Extract port from src_interface string
        size_t colon_pos = src_interface.rfind(':');
        if (colon_pos == std::string::npos) {
            throw std::runtime_error("Invalid source interface format");
        }
        return static_cast<uint16_t>(std::stoi(src_interface.substr(colon_pos + 1)));
    }   

    std::string ip_address() const
    {
        // Extract IP address from src_interface string
        size_t first_colon = src_interface.find(':');
        size_t last_colon = src_interface.rfind(':');
        if (first_colon == std::string::npos || last_colon == std::string::npos || first_colon == last_colon) {
            throw std::runtime_error("Invalid source interface format");
        }
        return src_interface.substr(first_colon + 1, last_colon - first_colon - 1);
    }

    std::string interface_name() const
    {
        // Extract interface name from src_interface string
        size_t colon_pos = src_interface.find(':');
        if (colon_pos == std::string::npos) {
            throw std::runtime_error("Invalid source interface format");
        }
        return src_interface.substr(0, colon_pos);
    }
};


int main(int argc, char* argv[])
{
    CliOptions options;
    try{
        options.parse(argc, argv);
    }
    catch(const std::exception& e){
        std::cerr << "Cli Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    options.print();

    CapturePrm capture_prm;
    capture_prm.mch = static_cast<int32_t>(options.mch);
    capture_prm.freq_hz = options.freq_hz;
    capture_prm.out_dir = options.output_dir;
    capture_prm.file_size_bytes = options._aligned_file_size_bytes();
    capture_prm.capture_limit = options._aligned_capture_limit();
    capture_prm.sock_raw = false; // using or not raw socket for capture (now only regular UDP)
    capture_prm.interface = options.interface_name();
    capture_prm.ip = options.ip_address();
    capture_prm.port = options.port();

    CaptureResult capture_result;
    try{
        int ret = capture(capture_prm, capture_result);
        if(ret != 0){
            std::cerr << "Capture failed with code: " << ret << std::endl;
            return EXIT_FAILURE;
        }
        std::cout << capture_result.summary() << std::endl;
    }
    catch(const std::exception& e){
        std::cerr << "Capture Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}