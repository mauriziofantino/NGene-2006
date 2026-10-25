set(NGENE_NATIVE_DEPS_DIR "${PROJECT_SOURCE_DIR}/build/native-deps" CACHE PATH "Microsoft ABI dependency prefix")
set(NGENE_NATIVE_SOURCES_DIR "${PROJECT_SOURCE_DIR}/build/native-sources" CACHE PATH "Pinned dependency source archives")
set(_pthread "${NGENE_NATIVE_SOURCES_DIR}/pthreads4w")
set(_pdc "${NGENE_NATIVE_SOURCES_DIR}/PDCursesMod-4.5.4")
if(NOT EXISTS "${_pthread}/pthread.c" OR NOT EXISTS "${NGENE_NATIVE_DEPS_DIR}/lib/libcrypto.lib")
    message(FATAL_ERROR "Run scripts/build-native-windows.ps1 to download and compile native dependencies.")
endif()

add_library(ngene_pthreads STATIC "${_pthread}/pthread.c")
target_include_directories(ngene_pthreads PUBLIC "${_pthread}")
target_compile_definitions(ngene_pthreads PUBLIC __PTW32_STATIC_LIB PRIVATE
    HAVE_CONFIG_H __PTW32_BUILD __PTW32_CLEANUP_C __PTW32_BUILD_INLINED)
target_compile_options(ngene_pthreads PRIVATE /O2)
add_library(Threads::Threads ALIAS ngene_pthreads)

file(GLOB _pdc_core CONFIGURE_DEPENDS "${_pdc}/pdcurses/*.c")
file(GLOB _pdc_console CONFIGURE_DEPENDS "${_pdc}/wincon/*.c")
add_library(ngene_pdcurses STATIC ${_pdc_core} ${_pdc_console})
target_include_directories(ngene_pdcurses PUBLIC "${_pdc}" PRIVATE "${_pdc}/wincon")
target_compile_options(ngene_pdcurses PRIVATE /O2)
target_link_libraries(ngene_pdcurses PUBLIC winmm user32 advapi32)
add_library(PkgConfig::NCURSES ALIAS ngene_pdcurses)

add_library(ngene_openssl INTERFACE)
target_include_directories(ngene_openssl INTERFACE "${NGENE_NATIVE_DEPS_DIR}/include")
target_link_libraries(ngene_openssl INTERFACE
    "${NGENE_NATIVE_DEPS_DIR}/lib/libssl.lib" "${NGENE_NATIVE_DEPS_DIR}/lib/libcrypto.lib"
    ws2_32 crypt32 advapi32 user32 gdi32)
add_library(PkgConfig::SSL ALIAS ngene_openssl)

add_library(ngene_windows STATIC "${PROJECT_SOURCE_DIR}/platform/windows/native_compat.c")
target_include_directories(ngene_windows PUBLIC "${PROJECT_SOURCE_DIR}/platform/windows/include"
    "${PROJECT_SOURCE_DIR}/platform/windows" "${_pdc}" "${NGENE_NATIVE_DEPS_DIR}/include")
target_compile_options(ngene_windows PUBLIC "/FI${PROJECT_SOURCE_DIR}/platform/windows/native_compat.h")
target_link_libraries(ngene_windows PUBLIC ngene_pthreads ws2_32)
