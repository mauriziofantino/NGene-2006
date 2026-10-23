cmake_minimum_required(VERSION 3.20)
project(ngene VERSION 1.0 LANGUAGES C)

# Preserve the GNU inline assembly and POSIX data model of the classic receiver.
if(WIN32 AND NOT CYGWIN)
    message(FATAL_ERROR "Use Cygwin x64 GCC for Windows; see docs/WINDOWS.md. MSVC/MinGW are not supported by this increment.")
endif()
if(NOT CMAKE_C_COMPILER_ID STREQUAL "GNU" OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8
   OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    message(FATAL_ERROR "The classic SIMD implementation requires x86_64 GCC.")
endif()
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
# The historical SIMD macros do not declare register clobbers. Keep optimization
# off in this preservation build; enabling it needs a separate DSP validation.
add_compile_options(-O0 -msse2 -mfpmath=sse -fno-tree-vectorize -Wall -Wextra)
add_compile_definitions(_GNU_SOURCE _FILE_OFFSET_BITS=64)
if(CYGWIN)
    # libusb 1.0.21 includes windows.h: keep RPC's boolean out of the GUI ABI.
    add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX)
endif()
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

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

find_package(PkgConfig REQUIRED)
pkg_check_modules(SSL REQUIRED IMPORTED_TARGET openssl)

pkg_check_modules(USB REQUIRED IMPORTED_TARGET libusb-1.0)
pkg_check_modules(NCURSES REQUIRED IMPORTED_TARGET ncurses)

# Add all necessary source files from Makefile.am
add_executable(ngene
    receiver/ngene.c
    receiver/manageConfiguration.c
    receiver/signalHandler.c
    receiver/manageLogs.c
    receiver/ngeneInitialization.c
    receiver/channelManager.c
    frontEnd/circularBuffer.c
    frontEnd/frontend.c
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
    asmFolder/fft.c
    asmFolder/arrays.c
    asmFolder/data_shift.c
    asmFolder/carrierTable.c
    asmFolder/prnTable.c
    asmFolder/sige.c
    asmFolder/sigeV3.c
    warmStart/manageWarmStartConfiguration.c
    warmStart/warmStart.c
)

# Build the bundled Reed-Solomon code used by NGene from source. Never link the
# Linux archive shipped in the old source tree into the Windows executable.
add_library(ngene_fec STATIC
    libraries/libfec/init_rs_char.c
    libraries/libfec/encode_rs_char.c
    libraries/libfec/decode_rs_char.c)
target_include_directories(ngene_fec PUBLIC libraries/libfec)
target_link_libraries(ngene PRIVATE ngene_fec PkgConfig::SSL PkgConfig::USB
    PkgConfig::NCURSES Threads::Threads m)

install(TARGETS ngene RUNTIME DESTINATION bin)
install(DIRECTORY sampleConfigurationFiles/ DESTINATION .)

option(NGENE_BUILD_PLATFORM_CHECKS "Build USB/platform diagnostic tools" OFF)
if(NGENE_BUILD_PLATFORM_CHECKS)
    enable_testing()
    add_executable(ngene_usb_probe tools/usb_probe.c)
    target_link_libraries(ngene_usb_probe PRIVATE PkgConfig::USB)
    add_executable(ngene_check_sige tools/check_sige_samples.c asmFolder/sige.c asmFolder/sigeV3.c)
    add_test(NAME sige_sample_preservation COMMAND ngene_check_sige)
    add_executable(ngene_check_usb tools/check_usb_errors.c frontEnd/usbInterface.c)
    target_link_libraries(ngene_check_usb PRIVATE PkgConfig::USB Threads::Threads)
    add_test(NAME usb_error_paths COMMAND ngene_check_usb)
    if(CYGWIN)
        add_executable(ngene_check_sendfile tools/check_cygwin_sendfile.c)
        add_test(NAME cygwin_gui_file_copy COMMAND ngene_check_sendfile)
    endif()
endif()
