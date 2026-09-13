cmake_minimum_required(VERSION 3.25)
project(FIADA VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "FIADA's current Skia/Win32 frontend is Windows-only")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SKIA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.deps/skia108/ucrt64"
    CACHE PATH "Root of the local MSYS2 UCRT64 Skia package")
set(FIADA_STATIC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.deps/static-runtime"
    CACHE PATH "Root of the local static image codec libraries")

if(NOT EXISTS "${SKIA_ROOT}/lib/libskia.a")
  message(FATAL_ERROR "Local Skia was not found. Run scripts/bootstrap-windows.ps1.")
endif()
foreach(codec libjpeg.a libpng16.a libwebpdemux.a libwebpmux.a libwebp.a libsharpyuv.a libzs.a)
  if(NOT EXISTS "${FIADA_STATIC_ROOT}/lib/${codec}")
    message(FATAL_ERROR "Missing static codec ${codec}. Run scripts/bootstrap-windows.ps1.")
  endif()
endforeach()

add_executable(FIADA WIN32 src/main.cpp src/game.cpp src/game.hpp src/trained_policy.hpp)
target_include_directories(FIADA PRIVATE "${SKIA_ROOT}/include/skia")
target_compile_definitions(FIADA PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(FIADA PRIVATE
  -std=c++2c
  -Wall -Wextra -Wpedantic
  $<$<CONFIG:Release>:-O3;-ffunction-sections;-fdata-sections>)
target_link_options(FIADA PRIVATE
  $<$<CONFIG:Release>:-Wl,--gc-sections,--no-insert-timestamp;-s>)
target_link_libraries(FIADA PRIVATE
  "${SKIA_ROOT}/lib/libskia.a"
  "${FIADA_STATIC_ROOT}/lib/libjpeg.a"
  "${FIADA_STATIC_ROOT}/lib/libpng16.a"
  "${FIADA_STATIC_ROOT}/lib/libwebpdemux.a"
  "${FIADA_STATIC_ROOT}/lib/libwebpmux.a"
  "${FIADA_STATIC_ROOT}/lib/libwebp.a"
  "${FIADA_STATIC_ROOT}/lib/libsharpyuv.a"
  "${FIADA_STATIC_ROOT}/lib/libzs.a"
  gdi32 user32 comdlg32 shell32 ole32 oleaut32 uuid usp10 opengl32 bcrypt)

add_executable(FIADA_train src/training_main.cpp src/game.cpp src/game.hpp src/trained_policy.hpp)
target_include_directories(FIADA_train PRIVATE "${SKIA_ROOT}/include/skia")
target_compile_definitions(FIADA_train PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(FIADA_train PRIVATE -std=c++2c -O3)
target_link_libraries(FIADA_train PRIVATE
  "${SKIA_ROOT}/lib/libskia.a"
  "${FIADA_STATIC_ROOT}/lib/libjpeg.a" "${FIADA_STATIC_ROOT}/lib/libpng16.a"
  "${FIADA_STATIC_ROOT}/lib/libwebpdemux.a" "${FIADA_STATIC_ROOT}/lib/libwebpmux.a"
  "${FIADA_STATIC_ROOT}/lib/libwebp.a" "${FIADA_STATIC_ROOT}/lib/libsharpyuv.a"
  "${FIADA_STATIC_ROOT}/lib/libzs.a"
  gdi32 user32 comdlg32 shell32 ole32 oleaut32 uuid usp10 opengl32 bcrypt)

add_custom_command(TARGET FIADA POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_SOURCE_DIR}/assets/png"
          "$<TARGET_FILE_DIR:FIADA>/assets/png"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_SOURCE_DIR}/assets/policy"
          "$<TARGET_FILE_DIR:FIADA>/assets/policy")
