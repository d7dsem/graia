cmake_minimum_required(VERSION 3.15)
project(graia LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# DataStream library (header-only: file_reader + sock_reader)
add_library(data_stream INTERFACE)
target_include_directories(data_stream INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/source/data-stream
)

# Executable
add_executable(graia_capture
    source/graia_capture_entry.cpp
    source/graia_capture.cpp
    source/rot_writer.cpp
)

# Windows: Winsock2
if(WIN32)
    target_link_libraries(graia_capture PRIVATE ws2_32)
endif()

# Linux: pthread (якщо потрібно в майбутньому)
if(UNIX AND NOT APPLE)
    # Threads for atomic/signal on Unix
    find_package(Threads REQUIRED)
    target_link_libraries(graia_capture PRIVATE pthread)
endif()

# Output directory
set_target_properties(graia_capture PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)


# Force colored output for Ninja + GCC/Clang
if(CMAKE_GENERATOR STREQUAL "Ninja")
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        add_compile_options(-fdiagnostics-color=always)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        add_compile_options(-fcolor-diagnostics)
    endif()
endif()