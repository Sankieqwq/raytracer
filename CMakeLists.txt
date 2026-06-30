cmake_minimum_required(VERSION 3.10)
project(Raytracer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(raytracer src/main.cpp)

target_include_directories(raytracer PRIVATE include)
target_include_directories(raytracer SYSTEM PRIVATE third_party)

target_compile_options(raytracer PRIVATE -O2 -Wall -Wextra)

enable_testing()

add_executable(raytracer_regression_tests tests/regression_tests.cpp)
target_include_directories(raytracer_regression_tests PRIVATE include)
target_include_directories(raytracer_regression_tests SYSTEM PRIVATE third_party)
target_compile_options(raytracer_regression_tests PRIVATE
    -Wall
    -Wextra
    -fsanitize=address,undefined
    -fno-omit-frame-pointer)
target_link_options(raytracer_regression_tests PRIVATE -fsanitize=address,undefined)

add_test(NAME raytracer_regression_tests COMMAND raytracer_regression_tests)
set_tests_properties(raytracer_regression_tests PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
