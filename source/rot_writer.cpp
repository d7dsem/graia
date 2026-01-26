// rot_writer.cpp
// Cross-platform rotating packet writer:
// - Unified interface: write(const void* p_pkt) where p_pkt points to PACKET_SZ bytes
// - Aggregation buffer sized as PACKET_SZ * BUFF_PKT_LEN and page-multiple
// - Linux: O_DIRECT for full BUFF_SZ writes; tail is written WITHOUT O_DIRECT on final close
// - Windows: stdio + setvbuf + aggregation
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

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#endif

class RotatingWriter : public IPacketWriter
{
public:
    static_assert(BUFF_SZ % HW_PAGE_SZ == 0, "BUFF_SZ must be HW_PAGE_SZ-multiple");
    static_assert(BUFF_SZ % PACKET_SZ  == 0, "BUFF_SZ must be PACKET_SZ-multiple");
    static_assert(FILE_BUF_SZ % HW_PAGE_SZ == 0, "FILE_BUF_SZ should be HW_PAGE_SZ-multiple");

private:
    bool     is_raw = false;
    uint32_t _mch   = 0;
    STD_PATH _out_dir;
    int64_t  _freq_hz = 0; 
    size_t   _file_size_bytes = 0;

    std::string ts;
    uint32_t seq = 0;
    uint32_t finalized_files = 0;

    size_t bytes_in_file = 0;
    size_t total_written = 0;

    STD_PATH final_path;
    STD_PATH tmp_path;

    // aggregation buffer (packet-level)
    size_t agg_used = 0; // bytes used (always multiple of PACKET_SZ)
#ifdef _WIN32
    std::vector<char> agg_buf;
    std::vector<char> file_buf;
    char* fbuf = nullptr;
    FILE* pf = nullptr;
#else
    // Linux: page-aligned buffer for O_DIRECT full-chunk writes
    unsigned char* agg_buf = nullptr;
    int fd = -1;
#endif

private:
    [[noreturn]] static inline void fail(const std::string& msg)
    {
        throw std::runtime_error(msg);
    }

    STD_PATH make_final_path(const STD_PATH& out_dir, int64_t freq_hz,
                             const std::string& ts_, uint32_t seq_) const
    {
        std::ostringstream name;
        name << "graia_stream_mch" << _mch << "_" << freq_hz << "_" << ts_ << "_"
             << std::setw(4) << std::setfill('0') << seq_
             << (is_raw ? ".raw" : ".bin");
        return out_dir / name.str();
    }

    static inline void ensure_dir_or_fail(const STD_PATH& dir)
    {
        if (dir.empty()) fail("RotatingWriter: out_dir not specified");
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (ec) fail("RotatingWriter: create_directories failed: " + ec.message());
        if (!std::filesystem::is_directory(dir))
            fail("RotatingWriter: out_dir not a directory: " + dir.string());
    }

    // Unified: rename tmp -> final (both platforms)
    inline void finalize_tmp_to_final()
    {
        std::error_code ec;
        std::filesystem::rename(tmp_path, final_path, ec);
        if (ec) {
            fail("RotatingWriter: rename failed: " + tmp_path.string() +
                 " -> " + final_path.string() + " ec=" + ec.message());
        }
    }

    // Unified: close current file and open next (both platforms)
    inline void commit_and_open_next()
    {
        // Contract: aggregation buffer MUST be empty (agg_used == 0)
        // Caller ensures this before rotation
        if (agg_used != 0) {
            fail("RotatingWriter: internal error: rotate with non-empty agg buffer");
        }

#ifdef _WIN32
        if (0 != std::fflush(pf)) {
            std::fclose(pf);
            pf = nullptr;
            fail("RotatingWriter: fflush failed: " + std::string(std::strerror(errno)));
        }
        std::fclose(pf);
        pf = nullptr;
#else
        if (fd >= 0) {
            if (::close(fd) != 0) {
                const int e = errno;
                fd = -1;
                fail("RotatingWriter: close failed err=" + std::string(std::strerror(e)));
            }
            fd = -1;
        }
#endif

        finalize_tmp_to_final();
        finalized_files++;
        ++seq;
        open_active();
    }

#ifndef _WIN32
    static inline void* aligned_alloc_or_fail(size_t alignment, size_t size)
    {
        void* p = nullptr;
        const int rc = ::posix_memalign(&p, alignment, size);
        if (rc != 0 || !p) {
            fail("RotatingWriter: posix_memalign failed rc=" + std::to_string(rc));
        }
        return p;
    }
#endif

#ifdef _WIN32
    inline void open_active()
    {
        final_path = make_final_path(_out_dir, _freq_hz, ts, seq);
        tmp_path = final_path;
        tmp_path += ".tmp";

        const auto path_str = tmp_path.string();
        pf = std::fopen(path_str.c_str(), "wb");
        if (!pf) {
            fail("RotatingWriter: fopen failed: " + tmp_path.string() +
                 " err=" + std::string(std::strerror(errno)));
        }

        if (0 != std::setvbuf(pf, fbuf, _IOFBF, file_buf.size())) {
            std::fclose(pf);
            pf = nullptr;
            fail("RotatingWriter: setvbuf failed");
        }

        bytes_in_file = 0;
    }

    inline void drain_to_disk(const char* p, size_t n)
    {
        // Contract: n is PACKET_SZ-multiple.
        while (n) {
            size_t cap = _file_size_bytes - bytes_in_file;
            if (cap == 0) {
                commit_and_open_next();
                cap = _file_size_bytes;
            }

            const size_t chunk = (n <= cap) ? n : cap;

            const size_t wr = std::fwrite(p, 1, chunk, pf);
            if (wr != chunk) {
                fail("RotatingWriter: fwrite short write (" +
                     std::to_string(wr) + "/" + std::to_string(chunk) +
                     ") err=" + std::string(std::strerror(errno)));
            }

            p += chunk;
            n -= chunk;

            bytes_in_file += chunk;
            total_written += chunk;
        }
    }
#else


    inline void open_active()
    {
        final_path = make_final_path(_out_dir, _freq_hz, ts, seq);
        tmp_path = final_path;
        tmp_path += ".tmp";

        fd = ::open(tmp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_DIRECT, 0644);
        if (fd < 0) {
            fail("RotatingWriter: open(O_DIRECT) failed: " + tmp_path.string() +
                 " err=" + std::string(std::strerror(errno)));
        }
        bytes_in_file = 0;
    }

    inline void write_full_direct(const void* p, size_t n)
    {
        // n must be HW_PAGE aligned and file offset aligned; we only call with BUFF_SZ.
        const unsigned char* s = static_cast<const unsigned char*>(p);
        size_t left = n;
        while (left) {
            ssize_t wr = ::write(fd, s, left);
            if (wr < 0) {
                if (errno == EINTR) continue;
                fail("RotatingWriter: write(O_DIRECT) failed err=" + std::string(std::strerror(errno)));
            }
            if (wr == 0) {
                fail("RotatingWriter: write(O_DIRECT) returned 0");
            }
            s += static_cast<size_t>(wr);
            left -= static_cast<size_t>(wr);
            bytes_in_file += static_cast<size_t>(wr);
            total_written += static_cast<size_t>(wr);
        }
    }

    inline void flush_full_chunks_direct()
    {
        // Flush only when we have full BUFF_SZ; partial is NOT written with O_DIRECT.
        if (agg_used != BUFF_SZ) return;

        // If next BUFF_SZ would cross file boundary, rotate BEFORE writing to keep chunk unsplit.
        if (bytes_in_file + BUFF_SZ > _file_size_bytes) {
            commit_and_open_next();
        }

        write_full_direct(agg_buf, BUFF_SZ);
        agg_used = 0;

        if (bytes_in_file == _file_size_bytes) {
            commit_and_open_next();
        }
    }

    inline void write_tail_nodirect_and_close()
    {
        // Called only at final close; after this we do not continue O_DIRECT writing.
        if (agg_used == 0) return;

        // Close O_DIRECT fd first.
        if (fd >= 0) {
            ::close(fd);
            fd = -1;
        }

        // Open same tmp file without O_DIRECT, seek to current end (bytes_in_file).
        const int fd2 = ::open(tmp_path.c_str(), O_WRONLY, 0644);
        if (fd2 < 0) {
            fail("RotatingWriter: open(tail) failed: " + tmp_path.string() +
                 " err=" + std::string(std::strerror(errno)));
        }

        const off_t off = static_cast<off_t>(bytes_in_file);
        if (::lseek(fd2, off, SEEK_SET) != off) {
            const int e = errno;
            ::close(fd2);
            fail("RotatingWriter: lseek(tail) failed err=" + std::string(std::strerror(e)));
        }

        const unsigned char* s = agg_buf;
        size_t left = agg_used;
        while (left) {
            ssize_t wr = ::write(fd2, s, left);
            if (wr < 0) {
                if (errno == EINTR) continue;
                const int e = errno;
                ::close(fd2);
                fail("RotatingWriter: write(tail) failed err=" + std::string(std::strerror(e)));
            }
            if (wr == 0) {
                ::close(fd2);
                fail("RotatingWriter: write(tail) returned 0");
            }
            s += static_cast<size_t>(wr);
            left -= static_cast<size_t>(wr);
            bytes_in_file += static_cast<size_t>(wr);
            total_written += static_cast<size_t>(wr);
        }

        ::close(fd2);
        agg_used = 0;
    }
#endif

public:
    RotatingWriter(bool sock_raw, int32_t mch, const STD_PATH& out_dir,
                   int64_t freq_hz,
                   size_t file_size_bytes)
        : is_raw(sock_raw)
        , _mch(static_cast<uint32_t>(mch))
        , _out_dir(out_dir)
        , _freq_hz(freq_hz)
        , _file_size_bytes(file_size_bytes)
    {
        ensure_dir_or_fail(_out_dir);

        if (_file_size_bytes == 0)
            fail("RotatingWriter: file_size_bytes == 0");

        // PACKET_SZ alignment: minimal requirement for packet boundary integrity
        if ((_file_size_bytes % PACKET_SZ) != 0)
            fail("RotatingWriter: file_size_bytes not aligned to PACKET_SZ");

        // BUFF_SZ alignment: CRITICAL for rotation logic
        // Writer accumulates packets into BUFF_SZ chunks before writing to disk.
        // File rotation happens at exact file_size_bytes boundary.
        // If file_size is not BUFF_SZ-multiple, chunks will cross file boundary
        // causing rotation logic to break (cannot rotate mid-chunk).        
        if ((_file_size_bytes % BUFF_SZ) != 0)
            fail("RotatingWriter: file_size_bytes not aligned to BUFF_SZ");

        ts = now_timestamp_ymd_hms();
        seq = 1;

#ifdef _WIN32
        file_buf.resize(FILE_BUF_SZ);
        fbuf = file_buf.data();

        agg_buf.resize(BUFF_SZ);
        agg_used = 0;

        open_active();
#else
        agg_buf = static_cast<unsigned char*>(aligned_alloc_or_fail(HW_PAGE_SZ, BUFF_SZ));
        agg_used = 0;

        open_active();
#endif
    }

    ~RotatingWriter() override 
    {
        // best-effort, do not throw
        try {
#ifdef _WIN32
            if (pf) {
                flush();
                std::fflush(pf);
                std::fclose(pf);
                pf = nullptr;
                // Finalize only if file has data
                if (bytes_in_file > 0) {
                    finalize_tmp_to_final();
                    ++finalized_files;
                } else {
                    // Remove empty file
                    std::error_code ec;
                    std::filesystem::remove(tmp_path, ec);
                }
            }
#else
            if (fd >= 0) {
                // write tail without O_DIRECT (final only)
                write_tail_nodirect_and_close();

                // Finalize only if file has data
                if (bytes_in_file > 0) {
                    std::error_code ec;
                    std::filesystem::rename(tmp_path, final_path, ec);
                    ++finalized_files;
                } else {
                    // Remove empty file
                    std::error_code ec;
                    std::filesystem::remove(tmp_path, ec);
                }
            }
            if (agg_buf) {
                std::free(agg_buf);
                agg_buf = nullptr;
            }
#endif
        } catch (...) {
#ifndef _WIN32
            if (agg_buf) {
                std::free(agg_buf);
                agg_buf = nullptr;
            }
#endif
        }
    }

    RotatingWriter(const RotatingWriter&) = delete;
    RotatingWriter& operator=(const RotatingWriter&) = delete;

    // Unified packet API: p_pkt points to PACKET_SZ bytes.
    inline void write(const void* p_pkt) override 
    {
#ifdef _WIN32
        std::memcpy(agg_buf.data() + agg_used, p_pkt, PACKET_SZ);
        agg_used += PACKET_SZ;

        if (agg_used == BUFF_SZ) {
            // Check if we need to rotate BEFORE draining
            if (bytes_in_file + BUFF_SZ > _file_size_bytes) {
                // Can't fit BUFF_SZ in current file - rotate with empty buffer
                commit_and_open_next();
                // Now drain to new file
                drain_to_disk(agg_buf.data(), BUFF_SZ);
                agg_used = 0;
            } else {
                // Fits in current file
                drain_to_disk(agg_buf.data(), BUFF_SZ);
                agg_used = 0;
                // Check if file is exactly full after drain
                if (bytes_in_file == _file_size_bytes) {
                    commit_and_open_next();
                }
            }
        }
#else
        std::memcpy(agg_buf + agg_used, p_pkt, PACKET_SZ);
        agg_used += PACKET_SZ;

        if (agg_used == BUFF_SZ) {
            flush_full_chunks_direct();
        }
#endif
    }

    // Flush:
    // - Windows: writes any pending bytes (packet-multiple) via stdio
    // - Linux O_DIRECT: flushes ONLY full BUFF_SZ chunks; keeps partial in memory (no O_DIRECT partial writes)
    inline void flush() override 
    {
#ifdef _WIN32
        if (!pf) return;
        if (agg_used) {
            drain_to_disk(agg_buf.data(), agg_used);
            agg_used = 0;
        }
        if (0 != std::fflush(pf)) {
            fail("RotatingWriter: fflush failed: " + std::string(std::strerror(errno)));
        }
#else
        flush_full_chunks_direct();
        // partial tail stays buffered until it becomes BUFF_SZ or until destructor finalizes (no-direct tail write).
#endif
    }

    uint64_t total_written_u64() const override { return static_cast<uint64_t>(total_written); }
    uint64_t bytes_in_file_u64() const { return static_cast<uint64_t>(bytes_in_file); }
    uint32_t seq_u32() const { return seq; }
    uint32_t files_count() const override {
         return finalized_files; 
    }
};

IPacketWriter* create_rotating_writer(const CapturePrm& prm)
{
    return new RotatingWriter(
        prm.sock_raw,
        prm.mch,
        prm.out_dir,
        prm.freq_hz,
        prm.file_size_bytes
    );
}

// ===== Test =====
static void test_rotating_writer()
{
    bool sock_raw = false;
    int32_t mch = 0;
    STD_PATH out_dir = "output_directory";
    int64_t freq_hz = 421'000'000;

    std::unique_ptr<RotatingWriter> rwr;

    // Not aligned to packet size - should fail
    try {
        const size_t file_size_bytes = 10ull * 1024ull * 1024ull; // 10 MiB (not guaranteed packet-aligned)
        rwr = std::make_unique<RotatingWriter>(sock_raw, mch, out_dir, freq_hz, file_size_bytes);
        std::cerr << "ERROR: expected failure, but writer was created\n";
        rwr.reset();
    } catch (const std::runtime_error& e) {
        std::cerr << "Expected failure: " << e.what() << "\n";
    }

    // Aligned:
    // - Windows: must be PACKET_SZ-multiple (BUFF_SZ is ok)
    // - Linux O_DIRECT: must be BUFF_SZ-multiple (BUFF_SZ is ok)
    try {
        const size_t file_size_bytes = BUFF_SZ; // aligned for both modes
        rwr = std::make_unique<RotatingWriter>(sock_raw, mch, out_dir, freq_hz, file_size_bytes);
        std::cerr << "OK: writer created (aligned)\n";
    } catch (const std::runtime_error& e) {
        std::cerr << "ERROR: expected success, got: " << e.what() << "\n";
        return;
    }

    std::vector<char> packet(PACKET_SZ, 0);

    for (size_t i = 0; i < 1000; ++i) {
        std::fill(packet.begin(), packet.end(), static_cast<char>(i & 0xFF));
        rwr->write(packet.data());
    }

    // Windows: flush writes tail to disk.
    // Linux: flush writes only full BUFF_SZ; tail remains buffered and is written in destructor without O_DIRECT.
    rwr->flush();
    rwr.reset();
}

// ===== Main for test =====
// int main()
// {
//     test_rotating_writer();
//     return 0;
// }