// file_reader.hpp
#pragma once
#include "stream_reader.hpp"
#include <cstdio>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;
using STD_PATH = std::filesystem::path;

#ifdef _WIN32
    #define fseek64 _fseeki64
#else
    #define fseek64 fseeko64
#endif

class FileReader : public I_STREAM_READER {
private:
    STD_PATH path;
    size_t chunk_sz;
    size_t offs;  // Initial offset, reserved for future use
    FILE* pf;
    size_t fsz;
    size_t chunk_count;
public:
    FileReader(const std::string& file_path, size_t chunk_size, size_t offset = 0)
        : path(file_path), chunk_sz(chunk_size), offs(offset)
    {
        pf = fopen(path.string().c_str(), "rb");
        if (!pf) {
            throw std::runtime_error("[FileReader] Failed to open file: " + path.string());
        }
        fsz = fs::file_size(path);
        chunk_count = (fsz + chunk_sz - 1) / chunk_sz;
        jump_to(offs);
    }
    
    ~FileReader() override { close(); }
    
    size_t read_into(uint8_t* buff_ptr) override {
        size_t rd = fread(buff_ptr, 1, chunk_sz, pf);
        if (rd < chunk_sz && ferror(pf)) {
            throw std::runtime_error("[FileReader] Read error");
        }
        return rd;
    }
    
    size_t get_chunk_size() const noexcept override { return chunk_sz; }
    std::string get_type() const noexcept override { return "file reader: " + path.string(); }

    void jump_to(size_t offset) {
        if (fseek64(pf, offset, SEEK_SET) != 0) {
            throw std::runtime_error("[FileReader] Failed to seek to offset");
        }
    }
    
    size_t get_size() const noexcept { return fsz; }
    size_t get_chunk_count() const noexcept { return chunk_count; }
    STD_PATH get_file_path() const noexcept { return path; }
    void close() {
        if (pf) {
            fclose(pf);
            pf = NULL;
        }
    }
};