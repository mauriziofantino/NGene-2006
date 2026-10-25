# Pinned upstream source, compiled with the same native Microsoft ABI and libusb
# as the receiver. No external DLL, precompiled SDK or additional USB stack.
set(_hackrf "${NGENE_NATIVE_SOURCES_DIR}/hackrf-2026.01.3/host/libhackrf/src")
if(NOT EXISTS "${_hackrf}/hackrf.c")
    message(FATAL_ERROR "Run build.cmd to download the pinned libhackrf sources.")
endif()
set(_hackrf_generated "${CMAKE_CURRENT_BINARY_DIR}/generated/hackrf-static")
file(MAKE_DIRECTORY "${_hackrf_generated}")
# Upstream unconditionally marks Windows declarations dllexport. A static build
# needs neither export nor import annotations. Keep the upstream license/API.
file(READ "${_hackrf}/hackrf.h" _hackrf_header)
string(REPLACE "__declspec(dllexport)" "" _hackrf_header "${_hackrf_header}")
string(REPLACE "__declspec(dllimport)" "" _hackrf_header "${_hackrf_header}")
file(WRITE "${_hackrf_generated}/hackrf.h" "${_hackrf_header}")
configure_file("${_hackrf}/hackrf.c" "${_hackrf_generated}/hackrf.c" COPYONLY)
add_library(ngene_hackrf STATIC "${_hackrf_generated}/hackrf.c")
target_include_directories(ngene_hackrf PUBLIC "${_hackrf_generated}"
    PRIVATE "${PROJECT_SOURCE_DIR}/libraries/libusb/libusb-1.0")
target_compile_definitions(ngene_hackrf PRIVATE LIBRARY_VERSION="0.10" LIBRARY_RELEASE="v2026.01.3")
target_compile_options(ngene_hackrf PRIVATE /O2)
target_link_libraries(ngene_hackrf PUBLIC ngene_usb ngene_windows)
