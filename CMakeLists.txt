cmake_minimum_required(VERSION 3.20)

project(bejeweled-bot)

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV CONFIG REQUIRED)

include_directories(${OpenCV_INCLUDE_DIRS})

add_executable(bejeweled-bot
  src/main.cpp
  src/window_capture.cpp
  src/gem_detector.cpp
)

target_link_libraries(bejeweled-bot PRIVATE ${OpenCV_LIBS})
target_include_directories(bejeweled-bot PRIVATE
  ${OpenCV_INCLUDE_DIRS}
  ${CMAKE_SOURCE_DIR}/src
)
