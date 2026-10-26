# Linux uses distribution development packages; Windows keeps its own static
# source builds. Both frontends share the same system libusb on Linux.
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(USB REQUIRED IMPORTED_TARGET libusb-1.0)
pkg_check_modules(SSL REQUIRED IMPORTED_TARGET openssl)
pkg_check_modules(NCURSES REQUIRED IMPORTED_TARGET ncurses)
pkg_check_modules(HACKRF REQUIRED IMPORTED_TARGET libhackrf)

add_library(ngene_platform INTERFACE)
target_link_libraries(ngene_platform INTERFACE Threads::Threads m)
add_library(ngene_hackrf ALIAS PkgConfig::HACKRF)
