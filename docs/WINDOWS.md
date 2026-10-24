# Windows x64: SiGe preservation build

This increment removes non-SiGe frontends and builds the classic receiver on
Windows using Cygwin x64 GCC. Cygwin supplies the existing POSIX APIs through
`cygwin1.dll`; Linux and WSL are not required. MSVC and native MinGW builds are
rejected explicitly because their ABIs and missing POSIX APIs require a separate
port. Acquisition, tracking, navigation, PVT and SIMD algorithm sources are retained.

## Install and build

Install [Cygwin x64](https://www.cygwin.com/install.html) with these packages:

`gcc-core cmake make pkg-config libncurses-devel libssl-devel`

The Cygwin build compiles the bundled libusb 1.0.30 Windows backend from source.
It uses WinUSB RAW_IO for continuous sampler reads; the old Cygwin libusb 1.0.21
DLL is no longer used. See [dependency notes](../libraries/libusb/README.md).

From PowerShell in the repository:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-windows.ps1 -CygwinRoot C:\cygwin64
```

The executable is `build/windows-cygwin/ngene.exe`. To install example configuration
and the executable into the Cygwin user's `$HOME/.ngene`, add `-Install`. Installation
updates the example configuration files at that destination; keep your custom
configuration separately before installing again. The installer does not modify PATH
or shell startup files.

In the current workspace, the verification toolchain was installed locally in
`build/windows-tools/cygwin`. To reuse it, pass
`-CygwinRoot "$PWD\build\windows-tools\cygwin"` to the PowerShell build command.

Alternatively, in the Cygwin terminal, from this repository:

```sh
bash scripts/build-cygwin.sh --install
$HOME/.ngene/bin/ngene.exe --help
$HOME/.ngene/bin/ngene.exe -t
```

Run the executable in a Cygwin terminal with the Cygwin libraries on PATH. Required
runtime DLLs can be inspected with `cygcheck build/windows-cygwin/ngene.exe`.
Never mix native Windows, MSYS2 and Cygwin copies of OpenSSL/libusb/ncurses.

The build deliberately uses `-O0` and disables vectorization. The historical inline
assembly does not describe all XMM register clobbers to GCC. Optimization and real-time
throughput require separate validation; this increment provides a conservative build
for compatibility checks and recorded input. No real-time throughput guarantee is made.

## SiGe configuration

For the native I/Q experiment, see [native input configuration and verification](NATIVE_IQ.md).
It reuses the current marker-tagged sampler stream and bypasses real-IF conditioning.

Historical frontend IDs are preserved: `1` is SiGe, `6` is SiGe V3. The frontend table
contains only those entries. Older tables containing ten frontends are rejected, as
are unknown selected IDs; the receiver no longer silently selects a fallback device.

The original parameters remain unchanged:

| ID | Sampling rate | IF | VID:PID | Input |
| --- | --- | --- | --- | --- |
| 1 | 16367600 Hz | 4130400 Hz | 16c0:072f | USB or file |
| 6 | 16368000 Hz | 4092000 Hz | 0000:0000, unset in the original archive | File; USB requires a verified firmware configuration |

Use the original SiGe selection, ID `1`, for the connected `16c0:072f` sampler:
16367600 Hz sampling rate and 4130400 Hz IF. The user identifies the physical board
as V3, but its captured samples use the classic SiGe format. Bit 1 alternates on
every byte, and an independent GPS correlation of the capture finds satellite
peaks with the original SiGe converter. The V3 converter produces noise-level
results on the same capture. Selecting ID `6` from the board name alone therefore
chose the wrong sample conversion and IF. Its original unset VID/PID is retained.
Firmware and a firmware loader are not included.

At startup NGene prints the selected frontend name, ID, sampling rate and IF.
For this device the line must read `SIGE (ID 1), fs 16367600 Hz, IF 4130400 Hz`.
The configuration is read from the Cygwin user's `$HOME/.ngene/configuration`,
independently of the executable's directory. In the Cygwin terminal,
`cygpath -w "$HOME/.ngene"` prints its Windows location.

USB configuration 1, interface 2, alternate setting 0 and bulk IN endpoint `0x86`
are preserved. Start and stop both use vendor request `0x01`, with `wValue=1`
and `wValue=0`, respectively. The old stop request `0x00` stalled on the connected
device; the corrected stop command was accepted. The SiGe sample conversion
functions themselves are unchanged.

The historical queue of 128 bulk transfers is restored on Windows. The bundled
libusb 1.0.30 backend uses an I/O completion port and does not require the old
32-transfer workaround for a wait-handle limit. Transfer size and intermediate
buffer capacity are unchanged. The USB worker starts streaming after setting its
CPU affinity and priority, so samples do not arrive before that worker is ready.

On WinUSB endpoints NGene enables RAW_IO before submitting transfers, checking
the endpoint packet size and maximum RAW_IO transfer length. Without RAW_IO the
connected sampler stopped after a few kilobytes. A later isolated RAW_IO run
with 128 transfers received 482,721,792 bytes during a bounded 30-second run,
with zero reported transfer errors. This checks the transport in isolation;
full receiver throughput, sample continuity and navigation need separate validation.

The frontend thread also avoids an extra `nanosleep` after event processing;
Windows timer rounding can starve the USB queue under receiver load. An earlier
15-second run had no timeout messages, but the user's later run had recurring
timeouts and no acquired satellites. That short run did not establish a working
receiver. The producer now uses `memcpy` instead of the legacy SIMD `ArrayCopy`,
whose global XMM backup was shared with processing in the receiver thread. The
sample converters and acquisition/tracking algorithms remain unchanged.

The rebuilt Windows executable confirmed acquisition and entered tracking on
GPS 12, 13, 25 and 32 during a replay of real samples with those PRNs selected.
The capture lasts about one second, so sustained lock and live USB reception
still require a longer run; see the exact conditions in [verification results](VERIFICATION.md).

USB shutdown now sets a mutex-protected request before waking buffer waiters,
so a full buffer cannot trap the producer during thread join. Signal requests
are also checked during buffer waits and by the frontend thread. A timeout
restart resets the transfer read index to zero to preserve batch order. Failed
USB startup is propagated to the frontend status instead of leaving the receiver
waiting on transfers that were never started.

## USB checks on Windows

The intermediate buffer remains 512 blocks (4 MiB / approximately 256 ms at the
default SiGe settings). Increasing it would only postpone a sustained processing
overrun. When it fills, NGene now logs that condition once per USB start and
continues dispatching libusb events between buffer waits. Already completed
blocks are serviced without waiting for another USB event. Timeout messages
include the failed block, received length, buffer-wait count and maximum producer
and consumer service gaps since opening the device. The gaps include input/event
waits and restarts; they are scheduling diagnostics, not CPU execution times.

For an isolated replay throughput measurement, configure a separate build with
`-DNGENE_RX_PROFILING=ON`. It writes `profilingResults.csv` in the working directory
and reports processed samples per second and the maximum processing iteration
(including input waits). Use text mode (`-t`) to avoid the terminal shutdown pause.
FFT and compression timings are nested inside acquisition and must not be added
to acquisition totals. Replay throughput does not validate live USB scheduling
or sustained tracking beyond the duration of the recording.

The [libusb Windows documentation](https://github.com/libusb/libusb/wiki/Windows)
describes the required driver. A vendor driver that works with the original Windows
application does not necessarily expose the device to libusb. WinUSB is the recommended
backend; bind it only to the intended SiGe device/interface, using its verified VID/PID.
Replacing that binding can affect other software using the same hardware.

The frontend uses `libusb_handle_events_timeout`, not libusb poll descriptors (which
are unsupported on Windows). It queries the active configuration before requesting
a change, releases enumeration references, checks initialization failures and waits
for asynchronous cancellation callbacks before freeing transfer buffers. Partial
blocks are reported as errors rather than delivering uninitialized sample tails.

Run the diagnostic from Cygwin:

```sh
build/windows-cygwin/ngene_usb_probe.exe
```

This prints the loaded libusb version, enumerates devices, and attempts to open
`16c0:072f` and claim interface 2. It sends no start/stop vendor requests. Exit 0
means access succeeded, 1 means an API/access error, and 2 means the selected device
was absent. A different verified identity can be passed as two hexadecimal arguments.
Enumeration alone does not validate streaming, firmware, sample order or sustained rate.

The probe also prints the manufacturer, product and device release. During a
follow-up check, the user identified the board as SiGe V3, while its loaded USB
firmware reported `16c0:072f`, `SiGe Semiconductor`, `SE4110L`, release `0102`.
The user confirmed that this is the correct loaded firmware. Its accepted
start/stop protocol is used; NGene does not send the unsupported V3 mode/AGC
commands found in drivers for other firmware variants. RAW_IO permits sustained
isolated transfers without replacing the device firmware. The sample format,
rather than the product name, determines which existing SiGe converter to select.

## Remaining platform differences

- pthreads, conditions, semaphores, sockets, getopt, large file offsets, aligned
  allocation, monotonic clocks and ncurses come from Cygwin.
- Linux `sendfile` has a Cygwin file-to-socket adapter using `pread` and `write`,
  including interrupted calls and short writes. The GUI wire format is retained.
- Home/configuration discovery uses Cygwin `HOME`, not a hardcoded Windows path.
  Use Cygwin paths in configuration, e.g. `/cygdrive/c/data/sige.bin`.
- Thread affinity and FIFO scheduling remain best-effort OS requests; failure is
  logged by the existing implementation. Windows scheduling and timer behavior
  differ from Linux and need measurement with the hardware.
- The unused `sys/msg.h` include remains available through Cygwin; this receiver's
  active frontend synchronization uses pthread mutexes and conditions.
- libfec's required Reed-Solomon sources are rebuilt for the target. The Linux `.a`
  and `.so` files in the historical archive are not used by CMake.

Check the sample configuration paths for your workstation before acquisition.
The sample data files in configuration can refer to paths from the historical host.

## Automated checks

```sh
ctest --test-dir build/windows-cygwin --output-on-failure
```

These check the unchanged SiGe sample mappings and buffer boundaries over all byte
values, sixteen alignments, four SiGe phases and lengths spanning SIMD boundaries;
USB callback statuses, partial-block rejection and null handles; full-buffer
shutdown and transfer order after timeout restart using simulated USB I/O; and GUI file copy
payload, explicit offset and end-of-file behavior. The hardware probe is separate
and is not an automated streaming test. See [verification results](VERIFICATION.md).
