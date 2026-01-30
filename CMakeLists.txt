cmake_minimum_required(VERSION 3.20)

project(bejeweled-bot)

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Prevent Windows headers from defining min/max macros
add_compile_definitions(NOMINMAX)

find_package(OpenCV CONFIG REQUIRED)

include_directories(${OpenCV_INCLUDE_DIRS})

# Glob all source files recursively
file(GLOB_RECURSE SOURCES "src/*.cpp")

add_executable(bejeweled-bot ${SOURCES})

target_link_libraries(bejeweled-bot PRIVATE ${OpenCV_LIBS})
target_include_directories(bejeweled-bot PRIVATE
  ${OpenCV_INCLUDE_DIRS}
  ${CMAKE_SOURCE_DIR}/src
)
