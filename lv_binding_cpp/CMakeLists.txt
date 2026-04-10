cmake_minimum_required(VERSION 3.10)

set(CMAKE_TRY_COMPILE_TARGET_TYPE "STATIC_LIBRARY")

project(lv_binding_cpp VERSION 0.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

file(GLOB_RECURSE SOURCES ./lv_cpp/*.cpp ./lv_cpp/custom/drivers/*.c)
add_library(lv_binding_cpp STATIC ${SOURCES})

add_subdirectory(lvgl)
add_subdirectory(lv_drivers)

target_include_directories(lvgl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(lv_drivers PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(lv_binding_cpp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# `lvgl_examples` 在 `lv_binding_cpp/lvgl/CMakeLists.txt` 中可能只在非 MICROPYTHON 分支创建。
if(TARGET lvgl_examples)
    target_include_directories(lvgl_examples PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()

target_link_libraries(lv_binding_cpp lvgl lv_drivers)


