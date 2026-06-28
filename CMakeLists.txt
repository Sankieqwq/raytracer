cmake_minimum_required(VERSION 3.10)
project(Raytracer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(raytracer src/main.cpp)

target_include_directories(raytracer PRIVATE include third_party)

target_compile_options(raytracer PRIVATE -O2 -Wall -Wextra)
