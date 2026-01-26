cmake_minimum_required(VERSION 3.20)
project(Graia CXX)

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

add_executable(graia_capture_tst
    source/graia_capture.cpp
    source/file_rotation.cpp
    source/splice_engine.cpp
)
target_link_libraries(graia_capture_tst PRIVATE
    data_stream
)

if(UNIX)
    # Threads for atomic/signal on Unix
    find_package(Threads REQUIRED)
    target_link_libraries(graia_capture_tst PRIVATE Threads::Threads)
endif()

# Fabric test (add when ready)