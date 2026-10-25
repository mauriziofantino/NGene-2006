cmake_minimum_required(VERSION 3.20)
project(ngene VERSION 1.0 LANGUAGES C)

if(NOT WIN32 OR CYGWIN OR MINGW OR NOT MSVC OR NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "This package builds only native Windows x64 with Visual Studio clang-cl. Run build.cmd.")
endif()
set(NGENE_NATIVE_WINDOWS ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    message(FATAL_ERROR "The classic SIMD implementation requires x86_64.")
endif()
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
# The historical SIMD macros do not declare register clobbers. Keep optimization
# off in this preservation build; enabling it needs a separate DSP validation.
add_compile_options(/Od /W3 /clang:-fno-vectorize /clang:-fno-slp-vectorize)
add_compile_definitions(NGENE_NATIVE_WINDOWS WIN32_LEAN_AND_MEAN NOMINMAX
    _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE _USE_MATH_DEFINES)
include(platform/windows/native_dependencies.cmake)

include_directories(
    ${PROJECT_SOURCE_DIR}/secure
    ${PROJECT_SOURCE_DIR}/frontEnd
    ${PROJECT_SOURCE_DIR}/warmStart
    ${PROJECT_SOURCE_DIR}/acquisition
    ${PROJECT_SOURCE_DIR}/tracking
    ${PROJECT_SOURCE_DIR}/pvt
    ${PROJECT_SOURCE_DIR}/utility
    ${PROJECT_SOURCE_DIR}/utilityGnss
    ${PROJECT_SOURCE_DIR}/osnma
    ${PROJECT_SOURCE_DIR}/chimera
    ${PROJECT_SOURCE_DIR}/utilityGnss/NeQuick
    ${PROJECT_SOURCE_DIR}/guiSupport
    ${PROJECT_SOURCE_DIR}/receiver
    ${PROJECT_SOURCE_DIR}/asmFolder
    ${PROJECT_SOURCE_DIR}/libraries/libfec  # Correct inclusion of libfec
)

add_subdirectory(libraries/libusb)
add_library(PkgConfig::USB ALIAS ngene_usb)
include(platform/windows/hackrf.cmake)

# Intrinsic FFT kernels have complete compiler/ABI contracts and can be optimized
# independently. Historical inline-assembly receiver modules retain /Od.
add_library(ngene_fft STATIC asmFolder/fft.c asmFolder/fft_sse2.c)
target_include_directories(ngene_fft PUBLIC "${PROJECT_SOURCE_DIR}/libraries/libusb")
target_link_libraries(ngene_fft PUBLIC ngene_windows)
target_compile_options(ngene_fft PRIVATE /O2)

# Receiver sources. Signal-processing algorithms retain their existing code.
add_executable(ngene
    receiver/ngene.c
    receiver/manageConfiguration.c
    receiver/signalHandler.c
    receiver/manageLogs.c
    receiver/ngeneInitialization.c
    receiver/channelManager.c
    frontEnd/circularBuffer.c
    frontEnd/frontend.c
    frontEnd/hackrfFrontend.c
    frontEnd/frontendThreadFunctions.c
    frontEnd/megaBuffer.c
    frontEnd/usbInterface.c
    frontEnd/manageUsbFrontEndConfiguration.c
    acquisition/acquisition.c
    acquisition/coarseAcquisition.c
    acquisition/confirmation.c
    acquisition/manageAcqConfiguration.c
    acquisition/nonCoherentSumsManager.c
    acquisition/refinement.c
    tracking/tracking.c
    tracking/galileoTracking.c
    tracking/gpsTracking.c
    tracking/manageTrkConfiguration.c
    tracking/trackingDiscriminatorsLoops.c
    pvt/pvt.c
    pvt/pvtChecks.c
    pvt/carrierSmoothing.c
    pvt/dop.c
    pvt/kalmanFilter.c
    pvt/leastSquares.c
    pvt/managePvtConfiguration.c
    pvt/pvtUtility.c
    pvt/receiverTime.c
    utility/additionalLogs.c
    utility/checkFolder.c
    utility/ioFunctions.c
    utility/matrices.c
    utility/nmeaLog.c
    utility/rinexLog.c
    utility/samplesHistogram.c
    utility/argvParser.c
    utility/closeManager.c
    utility/mathFunctions.c
    utility/ncursesApi.c
    utility/profilingSupport.c
    utility/sampleCompression.c
    utility/samplesSpectrum.c
    osnma/base64.c
    osnma/cryptoFunctions.c
    osnma/dsmData.c
    osnma/macLookupTable.c
    osnma/mackList.c
    osnma/osnmaManagement.c
    osnma/osnmaUtilities.c
    osnma/osnmaValidations.c
    chimera/chimeraCryptoFunctions.c
    utilityGnss/cno.c
    utilityGnss/egnosCorrections.c
    utilityGnss/egnosIonoCorrections.c
    utilityGnss/egnosMessageParser.c
    utilityGnss/fnavMessageParser.c
    utilityGnss/galileoMessageParser.c
    utilityGnss/gpsMessageParser.c
    utilityGnss/ifenLog.c
    utilityGnss/manageNavMsgConfiguration.c
    utilityGnss/navigationMessage.c
    utilityGnss/navigationMessageParser.c
    utilityGnss/prnList.c
    utilityGnss/reedSolomon.c
    utilityGnss/timeConversions.c
    utilityGnss/viterbi.c
    utilityGnss/NeQuick/ionoCalcModip.c
    utilityGnss/NeQuick/neqCalcTopsideNe.c
    utilityGnss/NeQuick/neqGetRayProperties.c
    utilityGnss/NeQuick/nequick.c
    utilityGnss/NeQuick/neqCalcBottomsideNe.c
    utilityGnss/NeQuick/neqGetNeOnSlantRay.c
    utilityGnss/NeQuick/neqIntegrate.c
    utilityGnss/NeQuick/nequickComputation.c
    utilityGnss/NeQuick/neqCalcEpstParams.c
    utilityGnss/NeQuick/neqGetNeOnVertRay.c
    utilityGnss/NeQuick/neqUtils.c
    guiSupport/commModule.c
    guiSupport/guiInterfaceFunctions.c
    guiSupport/threadutils.c
    asmFolder/arrays.c
    asmFolder/data_shift.c
    asmFolder/carrierTable.c
    asmFolder/complexInput.c
    asmFolder/prnTable.c
    asmFolder/sige.c
    asmFolder/sigeV3.c
    warmStart/manageWarmStartConfiguration.c
    warmStart/warmStart.c
)

# Build the bundled Reed-Solomon code used by NGene from source. Never link the
# Linux archive shipped in the old source tree into the Windows executable.
add_subdirectory(libraries/libfec)
target_link_libraries(ngene PRIVATE ngene_fec PkgConfig::SSL PkgConfig::USB
    PkgConfig::NCURSES Threads::Threads)
target_link_libraries(ngene PRIVATE ngene_windows ngene_fft ngene_hackrf)

option(NGENE_RX_PROFILING "Measure receiver processing stages and replay throughput" OFF)
if(NGENE_RX_PROFILING)
    target_compile_definitions(ngene PRIVATE RX_PROFILING)
endif()

install(TARGETS ngene RUNTIME DESTINATION .)
install(DIRECTORY sampleConfigurationFiles/ DESTINATION .)
install(FILES libraries/libhackrf-LICENSE.txt DESTINATION licenses)

option(NGENE_BUILD_PLATFORM_CHECKS "Build USB/platform diagnostic tools" OFF)
if(NGENE_BUILD_PLATFORM_CHECKS)
    enable_testing()
    add_executable(ngene_check_hackrf tools/check_hackrf.c frontEnd/hackrfFrontend.c)
    target_include_directories(ngene_check_hackrf PRIVATE "${_hackrf_generated}")
    target_link_libraries(ngene_check_hackrf PRIVATE ngene_windows PkgConfig::USB)
    add_test(NAME hackrf_rx_contract COMMAND ngene_check_hackrf)
    set_tests_properties(hackrf_rx_contract PROPERTIES TIMEOUT 15)
    add_executable(ngene_check_fft tools/check_fft.c)
    target_link_libraries(ngene_check_fft PRIVATE ngene_fft)
    target_compile_options(ngene_check_fft PRIVATE /O2)
    add_test(NAME fft_evo_classic_contract COMMAND ngene_check_fft)
    set_tests_properties(fft_evo_classic_contract PROPERTIES TIMEOUT 60)
    add_executable(ngene_usb_probe tools/usb_probe.c)
    target_link_libraries(ngene_usb_probe PRIVATE PkgConfig::USB)
    add_executable(ngene_check_sige tools/check_sige_samples.c asmFolder/sige.c asmFolder/sigeV3.c)
    add_test(NAME sige_sample_preservation COMMAND ngene_check_sige)
    add_executable(ngene_check_native_iq tools/check_native_iq.c
        asmFolder/complexInput.c asmFolder/arrays.c asmFolder/carrierTable.c asmFolder/sige.c
        asmFolder/sigeV3.c frontEnd/circularBuffer.c)
    target_link_libraries(ngene_check_native_iq PRIVATE PkgConfig::USB)
    add_test(NAME sige_native_iq COMMAND ngene_check_native_iq)
    add_executable(ngene_check_usb tools/check_usb_errors.c frontEnd/usbInterface.c)
    target_link_libraries(ngene_check_usb PRIVATE PkgConfig::USB Threads::Threads)
    add_test(NAME usb_error_paths COMMAND ngene_check_usb)
    add_executable(ngene_check_usb_stream tools/check_usb_stream.c frontEnd/usbInterface.c)
    target_link_libraries(ngene_check_usb_stream PRIVATE PkgConfig::USB Threads::Threads)
    target_compile_definitions(ngene_check_usb_stream PRIVATE NGENE_USB_TEST_IO)
    add_test(NAME usb_stream_shutdown_restart COMMAND ngene_check_usb_stream)
    set_tests_properties(usb_stream_shutdown_restart PROPERTIES TIMEOUT 10)
    if(NGENE_NATIVE_WINDOWS)
        add_executable(ngene_check_windows tools/check_native_windows.c)
        target_link_libraries(ngene_check_windows PRIVATE ngene_windows)
        add_test(NAME native_windows_platform COMMAND ngene_check_windows)
        set_tests_properties(native_windows_platform PROPERTIES TIMEOUT 10)
        foreach(check ngene_usb_probe ngene_check_sige ngene_check_native_iq ngene_check_usb ngene_check_usb_stream)
            target_link_libraries(${check} PRIVATE ngene_windows)
        endforeach()
    endif()
endif()
