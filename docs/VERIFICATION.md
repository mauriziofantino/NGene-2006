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
CMake 4.4.4, OpenSSL 3.5.9 and ncurses 6.5.20240427. The initial checks used
libusb 1.0.21; the Cygwin target now builds bundled libusb 1.0.30. Dependencies
were installed in the workspace's ignored `build/windows-tools/cygwin` directory.
The required bundled libfec Reed-Solomon implementations were compiled from source.

The target uses the Cygwin POSIX runtime, GNU C99 and x86 SSE2, with optimization
disabled for the receiver; the USB dependency is compiled with `-O2` and GNU C11.
It is not a standalone MSVC/MinGW executable. Legacy compiler warnings,
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
| Simulated USB full-buffer shutdown / empty read / ordered timeout restart | Passed |
| USB producer never calls the receiver's shared SIMD copy | Passed |
| Captured SiGe replay, four preselected GPS PRNs | Acquisition confirmed; tracking starts on GPS 12, 13, 25, 32 |
| Isolated sampler, RAW_IO and 128 pending transfers, bounded 30-second run | 482,721,792 bytes; zero reported transfer errors |
| Cygwin GUI file-copy payload / explicit offset / EOF test | Passed |
| libusb initialization and device enumeration | Passed, 6 devices enumerated |
| Receiver startup with SiGe configuration and no matching device | Controlled exit 1 with absent-device message |
| Unsupported frontend ID 0 | Controlled exit 1 |
| Legacy ten-frontend table | Controlled exit 1 |
| `git diff HEAD --check` | Passed |

CTest executes four isolated tests when `NGENE_BUILD_PLATFORM_CHECKS=ON` on Cygwin.
The USB error test uses the production callback/handle code with test logging
and copy stand-ins. It does not simulate Windows USB scheduling or cancellation.

The added `usb_stream_shutdown_restart` regression check exercises the production
USB buffer code with link wrappers for hardware I/O. It checks shutdown while the
producer waits on a full buffer, preservation of unread data, empty reads during
shutdown, transfer order after restarting from a timeout at a nonzero index, and
that the producer never calls the receiver's shared-register SIMD copy.
CTest limits this check to ten seconds so a deadlock is reported as a failure.
Its cancellation wrapper completes immediately; it does not model Windows USB
scheduling or cancellation. The isolated real-device checks below cover transport,
not complete acquisition or tracking.

The same regression check now validates the control request fields: start uses
`0x40/0x01/1/0`, stop uses `0x40/0x01/0/0` (type/request/value/index).

## Follow-up hardware diagnosis

The connected device was subsequently accessible: interface 2 was claimed using
libusb 1.0.21. Its descriptors reported `16c0:072f`, manufacturer
`SiGe Semiconductor`, product `SE4110L`, and device release `0102`; the user
identified the physical board as SiGe V3 and confirmed using FXLoad.

A bounded, isolated synchronous read received 2048 bytes before a one-second
timeout. The historical stop request `0x00`, value 1, returned
`LIBUSB_ERROR_PIPE`; request `0x01`, value 0, succeeded. NGene now sends this
corrected stop command and logs control errors while still draining transfers.
The V3 AGC and mode requests from the
[GNSS-SDRLIB driver](https://github.com/taroz/GNSS-SDRLIB/blob/master/src/rcv/gn3s/gn3s.cpp)
were rejected by this loaded firmware and are not used by NGene. The user
confirmed that the loaded firmware is correct for this board.

An isolated asynchronous comparison with libusb 1.0.30 reproduced the stall
without RAW_IO: one completed 8192-byte transfer followed by timeouts. Enabling
WinUSB RAW_IO with the same transfer queue and accepted control protocol delivered
5844 complete transfers (47,874,048 bytes), with zero transfer errors, during a
short run of approximately three seconds. This identifies a WinUSB queue-path
problem in that isolated comparison. NGene now enables RAW_IO before
submitting reads and validates transfer-size constraints. The existing firmware
is retained. Selecting V3 (ID 6) from the physical board name was subsequently
shown to be incorrect for the sample stream produced by this firmware.

Removing the extra `nanosleep` after event processing gave a 15-second run without
USB timeout messages, using frontend ID 6 and 17 configured channels. The user's
later run still showed repeated five-second USB timeouts and no satellite
acquisition. The earlier short run was insufficient evidence of a working receiver.
Its log is in the ignored `build/windows-rawio-live.log`.

The queue has now been restored from 32 to the historical 128 pending transfers.
The bundled libusb 1.0.30 Windows backend uses an I/O completion port; it does not
have the per-transfer wait-handle limit that motivated the smaller queue. An
isolated RAW_IO capture with 128 transfers received 482,721,792 bytes during a
bounded 30-second run, with zero reported transfer errors. This result does not
establish uninterrupted sample timing or full receiver performance.

Sample inspection found bit 1 alternating exactly on each byte, matching the
classic SiGe stream. An independent GPS correlation of the capture using the
unchanged classic SiGe mapping found peak-to-mean ratios of approximately 65 for
PRN 12, 52.6 for PRN 13, 23.6 for PRN 24, 46.7 for PRN 25 and 37.7 for PRN 32.
Using the V3 mapping on the same capture gave a maximum around 4.6, consistent
with noise. The active configuration is therefore restored to the original
frontend ID 1, fs 16367600 Hz, IF 4130400 Hz and VID/PID `16c0:072f`.
The example V3 entry retains its original unset VID/PID. These correlations
support the converter selection; they are not NGene tracking or position results.

Streaming now starts inside the USB worker after its CPU affinity and priority
setup. The producer also uses `memcpy` instead of `ArrayCopy`, avoiding concurrent
access to the legacy SIMD routines' global XMM register backup from the USB and
receiver threads. The original SiGe converter, FFT, acquisition, tracking and
channel-manager sources remain unchanged.

## Classic SiGe replay after correction

The corrected Windows executable was rebuilt and all four CTest checks passed.
It was then run on the first 16,777,216 unmodified bytes recorded from the sampler
(about 1.025 seconds). With the active automatic acquisition settings, NGene found
GPS 12, 13, 24 and 25 and started Doppler refinement before reaching EOF.

A second diagnostic replay selected GPS 12, 13, 25 and 32 explicitly, with 1 ms
coherent integration and one noncoherent sum. NGene completed refinement and
confirmation and reported `tracking starts` for all four, at sample indices
3,066,436; 3,074,080; 3,073,324; and 3,073,576 respectively. This used the actual
Windows receiver and its unchanged classic SiGe conversion and DSP, rather than
the independent correlator. Logs are in the ignored
`build/windows-classic-replay.log` and `build/windows-classic-replay-targeted.log`.
The temporary input and PRN settings were restored byte-for-byte afterward.

The short replay establishes acquisition confirmation and entry into tracking;
it does not establish sustained PLL lock, decoded navigation or a position fix.
The subsequent live run could not start: both NGene and the USB probe reported
that `16c0:072f` was absent. The active user configuration remains USB input,
frontend ID 1, with its existing channel settings retained.

## Remaining hardware validation

The initial USB probe found no device, but the follow-up checks above exercised
the connected sampler. Full receiver throughput with the corrected classic SiGe
selection, sustained tracking, unplug recovery, sample continuity and live
navigation still need validation.

The new capture supports selection of the original sample format and NGene's
entry into tracking in the bounded replay above. A longer live or recorded run
is still needed to validate navigation and sustained tracking.
See [Windows instructions](WINDOWS.md) for the hardware probe and build commands.
