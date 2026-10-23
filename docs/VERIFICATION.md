# SiGe / Windows increment: verification

Date: 2026-10-23. Baseline: `efcdb86` (`Add NGene Classic source`).

## Scope

Non-SiGe frontend sources, configuration entries and buffer branches were removed.
SiGe IDs 1 and 6, sampling/IF values and converter implementations were retained.
Unsupported frontend IDs and old frontend tables are rejected explicitly.

Acquisition, tracking, PVT, navigation/utilityGnss, warm-start and SIMD processing
sources were compared with the baseline and are unchanged. OSNMA and CHIMERA each
have one include change: the glibc-private `bits/pthreadtypes.h` is replaced with
the public `pthread.h`. Their algorithms and structures are unchanged.

Changes outside these algorithms cover build configuration, frontend selection,
USB resource handling, the GUI file-to-socket OS adapter, and the missing-HOME flag
in path discovery. Source changes do not establish complete receiver equivalence
on a new OS; GNSS capture/replay validation remains necessary.

## Windows build

Built and linked the complete `ngene.exe` on Windows x64 with Cygwin GCC 14.4.0,
CMake 4.4.4, OpenSSL 3.5.9, libusb 1.0.21 and ncurses 6.5.20240427. Dependencies
were installed in the workspace's ignored `build/windows-tools/cygwin` directory.
The required bundled libfec Reed-Solomon implementations were compiled from source.

The target uses the Cygwin POSIX runtime, GNU C99 and x86 SSE2, with optimization
disabled. It is not a standalone MSVC/MinGW executable. Legacy compiler warnings,
including unchecked allocations and deprecated OpenSSL APIs, are still visible.
No claim of warning-free build, ESA compliance, real-time performance or validated
live navigation is made by this increment.

## Passed checks

| Check | Result |
| --- | --- |
| Complete Windows executable compile and link | Passed |
| `ngene.exe --help`, `--version` | Passed |
| SiGe sample mapping and buffer-boundary test | Passed, 425984 conversion cases |
| USB callback status / short-block / null-handle tests | Passed |
| Cygwin GUI file-copy payload / explicit offset / EOF test | Passed |
| libusb initialization and device enumeration | Passed, 6 devices enumerated |
| Receiver startup with SiGe configuration and no matching device | Controlled exit 1 with absent-device message |
| Unsupported frontend ID 0 | Controlled exit 1 |
| Legacy ten-frontend table | Controlled exit 1 |
| `git diff HEAD --check` | Passed |

CTest executes the three isolated tests when `NGENE_BUILD_PLATFORM_CHECKS=ON`.
The USB error test uses the production callback/handle code with test logging
and copy stand-ins. It does not simulate Windows USB scheduling or cancellation.

## Hardware still required

The USB probe found no device with VID/PID `16c0:072f` (exit 2). Consequently,
claiming SiGe interface 2, Cypress start/stop requests, asynchronous streaming,
unplug recovery and sustained sample throughput have not been tested with SiGe
hardware. The archive supplies no USB identity for SiGe V3; its configuration
remains file input unless the actual device identity and encoding are verified.

No known GNSS sample capture was supplied for an end-to-end replay/navigation
comparison. Tests verify the retained sample conversion maps, not a position fix.
See [Windows instructions](WINDOWS.md) for the hardware probe and build commands.
