cmake_minimum_required(VERSION 3.12.4)
project(lvgl C CXX)

set(LVGL_PRO_PROJECT_DIR
    ""
    CACHE PATH "Path to LVGL Pro Project folder")

# Main include files of the project
include_directories(${PROJECT_SOURCE_DIR}/main/inc)

# Define options for LVGL with default values (OFF)
option(LV_USE_DRAW_SDL "Use SDL draw unit" OFF)
option(LV_USE_LIBPNG "Use libpng to decode PNG" OFF)
option(LV_USE_LIBJPEG_TURBO "Use libjpeg turbo to decode JPEG" OFF)
option(LV_USE_FFMPEG "Use libffmpeg to display video using lv_ffmpeg" OFF)
option(LV_USE_FREETYPE "Use freetype library" OFF)

# Set C and C++ standards
set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set the output path for the executable
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
set(WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

# Find and include SDL2 library
find_package(SDL2 REQUIRED)

# Add compile definitions based on the selected options
add_compile_definitions($<$<BOOL:${LV_USE_DRAW_SDL}>:LV_USE_DRAW_SDL=1>)
add_compile_definitions($<$<BOOL:${LV_USE_LIBPNG}>:LV_USE_LIBPNG=1>)
add_compile_definitions($<$<BOOL:${LV_USE_LIBJPEG_TURBO}>:LV_USE_LIBJPEG_TURBO=1>)
add_compile_definitions($<$<BOOL:${LV_USE_FFMPEG}>:LV_USE_FFMPEG=1>)
add_compile_definitions($<$<BOOL:${LV_USE_FREETYPE}>:USE_LV_LIB_FREETYPE=1>)

# Add LVGL subdirectory
add_subdirectory(lv_binding_cpp)
# 必须先于 lv_binding_cpp，否则 #include "lv_conf.h" 会命中 lv_binding_cpp/lv_conf.h（无 FS/PNG），hal 里 LV_USE_FS_STDIO 为假、图片永远无法解码
target_include_directories(lvgl BEFORE PUBLIC ${PROJECT_SOURCE_DIR} ${SDL2_INCLUDE_DIRS})

set(MAIN_SOURCES src/mouse_cursor_icon.cpp src/hal/hal.c)
set(MAIN_LIBS "")
set(MAIN_INCLUDES "")
list(APPEND MAIN_SOURCES src/main.cpp)
if(EXISTS "${CMAKE_SOURCE_DIR}/src/widgets_bin.cpp")
  list(APPEND MAIN_SOURCES src/widgets_bin.cpp)
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/src/widget.cpp")
  list(APPEND MAIN_SOURCES src/widget.cpp)
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/app/main_win.cpp")
  list(APPEND MAIN_SOURCES app/main_win.cpp)
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/app/set_window.cpp")
  list(APPEND MAIN_SOURCES app/set_window.cpp)
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/src/win.cpp")
  list(APPEND MAIN_SOURCES src/win.cpp)
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/src/mrker.cpp")
  list(APPEND MAIN_SOURCES src/mrker.cpp)
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/src/translation_bin.cpp")
  list(APPEND MAIN_SOURCES src/translation_bin.cpp)
endif()

# Conditionally include and link libpng if LV_USE_LIBPNG is enabled
if(LV_USE_LIBPNG)
    find_package(PNG REQUIRED)
    target_include_directories(.
    ./lv_binding_cpp
    ./lv_binding_cpp/lvgl
    ./hal
     PUBLIC ${PNG_INCLUDE_DIRS})
    list(APPEND MAIN_LIBS ${PNG_LIBRARIES})
endif()

# Conditionally include and link libjpeg-turbo if LV_USE_LIBJPEG_TURBO is enabled
if(LV_USE_LIBJPEG_TURBO)
    find_package(JPEG REQUIRED)
    target_include_directories(lvgl PUBLIC ${JPEG_INCLUDE_DIRS})
    list(APPEND MAIN_LIBS ${JPEG_LIBRARIES})
endif()

# Conditionally include and link FFmpeg libraries if LV_USE_FFMPEG is enabled
if(LV_USE_FFMPEG)
    list(APPEND MAIN_LIBS avformat avcodec avutil swscale z)
endif()

# Conditionally include and link FreeType if LV_USE_FREETYPE is enabled
if(LV_USE_FREETYPE)
    find_package(Freetype REQUIRED)
    target_include_directories(lvgl PUBLIC ${FREETYPE_INCLUDE_DIRS})
    list(APPEND MAIN_LIBS ${FREETYPE_LIBRARIES})
endif()

if(LVGL_PRO_PROJECT_DIR)
  if(EXISTS "${LVGL_PRO_PROJECT_DIR}/CMakeLists.txt")
    add_subdirectory(${LVGL_PRO_PROJECT_DIR} ui)
    list(APPEND MAIN_LIBS lib-ui)

    target_compile_definitions(lib-ui PUBLIC LV_LVGL_H_INCLUDE_SIMPLE)
    target_include_directories(lib-ui PUBLIC
      ${LVGL_PRO_PROJECT_DIR}  # local headers like ui.h, generated files, etc.
      ${CMAKE_SOURCE_DIR}/lvgl # include "lvgl.h" works
    )
    message(STATUS "LVGL Pro Project enabled from: ${LVGL_PRO_PROJECT_DIR}")
  else()
    message(
      WARNING
        "LVGL_PRO_PROJECT_DIR specified but no CMakeLists.txt found at: ${LVGL_PRO_PROJECT_DIR}"
    )
  endif()
endif()

add_executable(main ${MAIN_SOURCES})
target_compile_definitions(main PRIVATE LV_CONF_INCLUDE_SIMPLE)
if(MAIN_INCLUDES)
  target_include_directories(main PRIVATE ${MAIN_INCLUDES})
endif()
target_link_libraries(main PRIVATE lvgl lv_drivers lv_binding_cpp SDL2 ${MAIN_LIBS})

# tools/ 下的小工具（可选：文件存在才构建，避免缺文件导致配置失败）
if(EXISTS "${CMAKE_SOURCE_DIR}/tools/parse_main_widgets_json.cpp")
  add_executable(parse_main_widgets_json "${CMAKE_SOURCE_DIR}/tools/parse_main_widgets_json.cpp")
  target_compile_features(parse_main_widgets_json PRIVATE cxx_std_17)
  set_target_properties(
    parse_main_widgets_json PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                                 "${CMAKE_SOURCE_DIR}/bin")
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/tools/json_to_widgets_bin.cpp")
  add_executable(json_to_widgets_bin "${CMAKE_SOURCE_DIR}/tools/json_to_widgets_bin.cpp")
  target_compile_features(json_to_widgets_bin PRIVATE cxx_std_17)
  set_target_properties(
    json_to_widgets_bin PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                             "${CMAKE_SOURCE_DIR}/bin")
endif()

# tools/ 下的 CSV -> BIN 编译工具（替代 JSON 方式）
add_executable(csv_to_widgets_bin "${CMAKE_SOURCE_DIR}/tools/csv_to_widgets_bin.cpp")
target_compile_features(csv_to_widgets_bin PRIVATE cxx_std_17)
set_target_properties(
  csv_to_widgets_bin PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                          "${CMAKE_SOURCE_DIR}/bin")

# tools/ 下的翻译表 CSV -> BIN 工具（对应 tr.c/tr.h 的 translation.bin）
add_executable(csv_to_translation_bin "${CMAKE_SOURCE_DIR}/tools/csv_to_translation_bin.cpp")
target_compile_features(csv_to_translation_bin PRIVATE cxx_std_17)
set_target_properties(
  csv_to_translation_bin PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                               "${CMAKE_SOURCE_DIR}/bin")

# On Windows, GUI applications do not show a console by default,
# which hides log output. This ensures a console is available for logging.
if (WIN32)
    if (MSVC)
        target_link_options(main PRIVATE "/SUBSYSTEM:CONSOLE")
    else()
        target_link_options(main PRIVATE "-mconsole")
    endif()
endif()

# Apply additional compile options if the build type is Debug
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Debug mode enabled")

    target_compile_options(lvgl PRIVATE
        -pedantic-errors
        -Wall
        -Wclobbered
        -Wdeprecated
        -Wdouble-promotion
        -Wempty-body
        -Wextra
        -Wformat-security
        -Wmaybe-uninitialized
        # -Wmissing-prototypes
        -Wpointer-arith
        -Wmultichar
        -Wno-pedantic # ignored for now, we convert functions to pointers for properties table.
        -Wreturn-type
        -Wshadow
        -Wshift-negative-value
        -Wsizeof-pointer-memaccess
        -Wtype-limits
        -Wundef
        -Wuninitialized
        -Wunreachable-code
        -Wfloat-conversion
        -Wstrict-aliasing
    )

if (ASAN)
    message(STATUS "AddressSanitizer enabled")

    # Add AddressSanitizer flags
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=address")
else()
    message(STATUS "AddressSanitizer disabled")
endif()
endif()
