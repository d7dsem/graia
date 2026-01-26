// file_reader.hpp
#pragma once
#include "stream_reader.hpp"
#include <cstdio>
#include <string>
#include <filesystem>

class I_STREAM_READER {
public:
    virtual ~I_STREAM_READER() = default;
    
    // Read up to chunk_size bytes into buffer
    // Returns: actual bytes read (0 <= result <= chunk_size)
    // Throws: std::runtime_error on fatal errors
    virtual size_t read_into(uint8_t* buff_ptr) = 0;
    
    // Get configured chunk size for this reader
    virtual size_t get_chunk_size() const noexcept = 0;

    // Get reader type name for logging/debugging
    virtual std::string get_type() const noexcept 
    {
        return "<UNK>";
    };
};