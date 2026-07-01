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

find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME golden_default
        COMMAND sh -c "$<TARGET_FILE:raytracer> --scene tests/golden/scenes/golden_default.json --threads 1 --out /tmp/golden_default.png && ${Python3_EXECUTABLE} tests/golden/compare_images.py --actual /tmp/golden_default.png --expected tests/golden/images/golden_default.png")
    set_tests_properties(golden_default PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

    add_test(NAME golden_mirror_glass_water
        COMMAND sh -c "$<TARGET_FILE:raytracer> --scene tests/golden/scenes/golden_mirror_glass_water.json --threads 1 --out /tmp/golden_mirror_glass_water.png && ${Python3_EXECUTABLE} tests/golden/compare_images.py --actual /tmp/golden_mirror_glass_water.png --expected tests/golden/images/golden_mirror_glass_water.png")
    set_tests_properties(golden_mirror_glass_water PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

    add_test(NAME golden_khronos_compare_metallic
        COMMAND sh -c "$<TARGET_FILE:raytracer> --scene tests/golden/scenes/golden_khronos_compare_metallic.json --threads 1 --out /tmp/golden_khronos_compare_metallic.png && ${Python3_EXECUTABLE} tests/golden/compare_images.py --actual /tmp/golden_khronos_compare_metallic.png --expected tests/golden/images/golden_khronos_compare_metallic.png")
    set_tests_properties(golden_khronos_compare_metallic PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

    add_test(NAME golden_khronos_attenuation_test
        COMMAND sh -c "$<TARGET_FILE:raytracer> --scene tests/golden/scenes/golden_khronos_attenuation_test.json --threads 1 --out /tmp/golden_khronos_attenuation_test.png && ${Python3_EXECUTABLE} tests/golden/compare_images.py --actual /tmp/golden_khronos_attenuation_test.png --expected tests/golden/images/golden_khronos_attenuation_test.png")
    set_tests_properties(golden_khronos_attenuation_test PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

    add_test(NAME golden_obj_mtl_test
        COMMAND sh -c "$<TARGET_FILE:raytracer> --scene tests/golden/scenes/golden_obj_mtl_test.json --threads 1 --out /tmp/golden_obj_mtl_test.png && ${Python3_EXECUTABLE} tests/golden/compare_images.py --actual /tmp/golden_obj_mtl_test.png --expected tests/golden/images/golden_obj_mtl_test.png")
    set_tests_properties(golden_obj_mtl_test PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

    add_test(NAME golden_glass_emissive
        COMMAND sh -c "$<TARGET_FILE:raytracer> --scene tests/golden/scenes/golden_glass_emissive.json --threads 1 --out /tmp/golden_glass_emissive.png && ${Python3_EXECUTABLE} tests/golden/compare_images.py --actual /tmp/golden_glass_emissive.png --expected tests/golden/images/golden_glass_emissive.png")
    set_tests_properties(golden_glass_emissive PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

    add_test(NAME golden_bunny_test
        COMMAND sh -c "$<TARGET_FILE:raytracer> --scene tests/golden/scenes/golden_bunny_test.json --threads 1 --out /tmp/golden_bunny_test.png && ${Python3_EXECUTABLE} tests/golden/compare_images.py --actual /tmp/golden_bunny_test.png --expected tests/golden/images/golden_bunny_test.png")
    set_tests_properties(golden_bunny_test PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

    add_test(NAME golden_no_normal_obj
        COMMAND sh -c "$<TARGET_FILE:raytracer> --scene tests/golden/scenes/golden_no_normal_obj.json --threads 1 --out /tmp/golden_no_normal_obj.png && ${Python3_EXECUTABLE} tests/golden/compare_images.py --actual /tmp/golden_no_normal_obj.png --expected tests/golden/images/golden_no_normal_obj.png")
    set_tests_properties(golden_no_normal_obj PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

    add_test(NAME golden_mtl_extended
        COMMAND sh -c "$<TARGET_FILE:raytracer> --scene tests/golden/scenes/golden_mtl_extended.json --threads 1 --out /tmp/golden_mtl_extended.png && ${Python3_EXECUTABLE} tests/golden/compare_images.py --actual /tmp/golden_mtl_extended.png --expected tests/golden/images/golden_mtl_extended.png")
    set_tests_properties(golden_mtl_extended PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

    add_test(NAME golden_studio_materials
        COMMAND sh -c "$<TARGET_FILE:raytracer> --scene tests/golden/scenes/golden_studio_materials.json --threads 1 --out /tmp/golden_studio_materials.png && ${Python3_EXECUTABLE} tests/golden/compare_images.py --actual /tmp/golden_studio_materials.png --expected tests/golden/images/golden_studio_materials.png")
    set_tests_properties(golden_studio_materials PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

    add_test(NAME golden_khronos_compare_roughness
        COMMAND sh -c "$<TARGET_FILE:raytracer> --scene tests/golden/scenes/golden_khronos_compare_roughness.json --threads 1 --out /tmp/golden_khronos_compare_roughness.png && ${Python3_EXECUTABLE} tests/golden/compare_images.py --actual /tmp/golden_khronos_compare_roughness.png --expected tests/golden/images/golden_khronos_compare_roughness.png")
    set_tests_properties(golden_khronos_compare_roughness PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

    add_test(NAME golden_khronos_transmission_roughness_test
        COMMAND sh -c "$<TARGET_FILE:raytracer> --scene tests/golden/scenes/golden_khronos_transmission_roughness_test.json --threads 1 --out /tmp/golden_khronos_transmission_roughness_test.png && ${Python3_EXECUTABLE} tests/golden/compare_images.py --actual /tmp/golden_khronos_transmission_roughness_test.png --expected tests/golden/images/golden_khronos_transmission_roughness_test.png")
    set_tests_properties(golden_khronos_transmission_roughness_test PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()
