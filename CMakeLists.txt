cmake_minimum_required(VERSION 3.20)

project(bejeweled-bot)

# Force vcpkg triplet for MinGW if using MinGW
if(MINGW OR (CMAKE_CXX_COMPILER_ID MATCHES "GNU" AND WIN32))
    set(VCPKG_TARGET_TRIPLET "x64-mingw-dynamic" CACHE STRING "" FORCE)
    message(STATUS "MinGW detected, forcing vcpkg triplet to x64-mingw-dynamic")
endif()

include(ProcessorCount)
ProcessorCount(num_jobs)

if(num_jobs EQUAL 0)
    set(num_jobs 1)
endif()

message("Number of available processors: ${num_jobs}")
set(CMAKE_BUILD_PARALLEL_LEVEL ${num_jobs})

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Export compile commands for clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Prevent Windows headers from defining min/max macros
add_compile_definitions(NOMINMAX)

find_package(OpenCV CONFIG REQUIRED)

include_directories(${OpenCV_INCLUDE_DIRS})

# Glob all source files recursively (re-run CMake on changes)
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")

add_executable(bejeweled-bot ${SOURCES})

target_link_libraries(bejeweled-bot PRIVATE ${OpenCV_LIBS})
set_target_properties(bejeweled-bot PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_SOURCE_DIR}"
)
target_include_directories(bejeweled-bot PRIVATE
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/src
)

# Performance optimizations
if(MSVC)
    target_compile_options(bejeweled-bot PRIVATE
        /MP${num_jobs}
        $<$<CONFIG:Release>:/O2 /Ob2 /DNDEBUG>
    )
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(bejeweled-bot PRIVATE
        $<$<CONFIG:Release>:-O3 -march=native -ffast-math>
        $<$<CONFIG:Debug>:-O0>
    )
endif()
