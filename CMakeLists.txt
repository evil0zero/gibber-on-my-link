cmake_minimum_required(VERSION 3.16)
project(gibber_on_my_link VERSION 0.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(gibber_codec STATIC cpp/src/gibber.cpp)
target_include_directories(gibber_codec PUBLIC cpp/include)
if(MSVC)
    target_compile_options(gibber_codec PRIVATE /W4)
else()
    target_compile_options(gibber_codec PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(gibber_cpp cpp/src/main.cpp)
target_link_libraries(gibber_cpp PRIVATE gibber_codec)

enable_testing()
add_executable(test_gibber_cpp cpp/tests/test_cpp.cpp)
target_link_libraries(test_gibber_cpp PRIVATE gibber_codec)
add_test(NAME cpp_roundtrip COMMAND test_gibber_cpp)
