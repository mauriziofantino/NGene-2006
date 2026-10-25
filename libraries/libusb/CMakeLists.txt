# The Windows sampler needs WinUSB RAW_IO, introduced in libusb 1.0.30.
# These settings mirror upstream configure.ac for Windows without hotplug.
set(_usb_time_header "/* MSVC uses timeval from winsock. */")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/config.h" "${_usb_time_header}\n" [=[
#define PLATFORM_WINDOWS 1
#define HAVE_STRUCT_TIMESPEC 1
#define ENABLE_LOGGING 1
#define DEFAULT_VISIBILITY __attribute__((visibility("default")))
#define PRINTF_FORMAT(a,b) __attribute__((format(printf,a,b)))
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
]=])

add_library(ngene_usb STATIC
    libusb-1.0/core.c libusb-1.0/descriptor.c libusb-1.0/hotplug.c
    libusb-1.0/io.c libusb-1.0/strerror.c libusb-1.0/sync.c
    libusb-1.0/os/events_windows.c libusb-1.0/os/threads_windows.c
    libusb-1.0/os/windows_common.c libusb-1.0/os/windows_usbdk.c
    libusb-1.0/os/windows_winusb.c)
target_include_directories(ngene_usb BEFORE PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/libusb-1.0")
set_target_properties(ngene_usb PROPERTIES C_STANDARD 11)
# Upstream uses Windows min/max macros internally. The receiver keeps NOMINMAX.
target_compile_options(ngene_usb PRIVATE /O2 /UNOMINMAX /FIwinsock2.h)
