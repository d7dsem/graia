cmake_minimum_required(VERSION 3.15)
project(graia LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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