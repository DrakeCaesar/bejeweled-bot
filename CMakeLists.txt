cmake_minimum_required(VERSION 3.20)

project(bejeweled-bot)

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

# Use static runtime library when building with static vcpkg triplets
if(VCPKG_TARGET_TRIPLET MATCHES "static" AND MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    message(STATUS "Using static runtime library (/MT) for static vcpkg triplet")
endif()

find_package(OpenCV CONFIG REQUIRED)

include_directories(${OpenCV_INCLUDE_DIRS})

# Glob all source files recursively (re-run CMake on changes)
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")

add_executable(bejeweled-bot ${SOURCES})

target_link_libraries(bejeweled-bot PRIVATE ${OpenCV_LIBS})
set_target_properties(bejeweled-bot PROPERTIES
    # Set PDB file name to match executable
    COMPILE_PDB_NAME "bejeweled-bot"
    COMPILE_PDB_NAME_DEBUG "bejeweled-bot"
    COMPILE_PDB_NAME_RELEASE "bejeweled-bot"
    COMPILE_PDB_NAME_RELWITHDEBINFO "bejeweled-bot"
    INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
    INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE
)
target_include_directories(bejeweled-bot PRIVATE
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/src
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_SIMULATE_ID MATCHES "MSVC")
    target_compile_options(bejeweled-bot PRIVATE
        /MP${num_jobs}
        /arch:AVX2
        $<$<CONFIG:Release>:/O2 /Ob2 /fp:fast /GL /DNDEBUG>
        $<$<CONFIG:RelWithDebInfo>:/O2 /Ob0 /fp:fast /DNDEBUG /Zi /Oy->
        $<$<CONFIG:Debug>:/ZI /Od>
    )
    target_link_options(bejeweled-bot PRIVATE
        $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>
        $<$<CONFIG:RelWithDebInfo>:/DEBUG /OPT:REF /OPT:ICF>
        $<$<CONFIG:Debug>:/DEBUG>
    )
elseif(MSVC)
    target_compile_options(bejeweled-bot PRIVATE
        /MP${num_jobs}
        $<$<CONFIG:Release>:/O2 /Ob2 /fp:fast /GL /arch:AVX2 /DNDEBUG>
        $<$<CONFIG:RelWithDebInfo>:/O2 /Ob0 /fp:fast /arch:AVX2 /DNDEBUG /Zi /Oy->
        $<$<CONFIG:Debug>:/ZI /Od>
    )
    target_link_options(bejeweled-bot PRIVATE
        $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>
        $<$<CONFIG:RelWithDebInfo>:/DEBUG /OPT:REF /OPT:ICF>
        $<$<CONFIG:Debug>:/DEBUG>
    )
endif()