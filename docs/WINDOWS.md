# Windows x64: SiGe preservation build

This increment removes non-SiGe frontends and builds the classic receiver on
Windows using Cygwin x64 GCC. Cygwin supplies the existing POSIX APIs through
`cygwin1.dll`; Linux and WSL are not required. MSVC and native MinGW builds are
rejected explicitly because their ABIs and missing POSIX APIs require a separate
port. Acquisition, tracking, navigation, PVT and SIMD algorithm sources are retained.

## Install and build

Install [Cygwin x64](https://www.cygwin.com/install.html) with these packages:

`gcc-core cmake make pkg-config libusb1.0-devel libncurses-devel libssl-devel`

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

Historical frontend IDs are preserved: `1` is SiGe, `6` is SiGe V3. The frontend table
contains only those entries. Older tables containing ten frontends are rejected, as
are unknown selected IDs; the receiver no longer silently selects a fallback device.

The original parameters remain unchanged:

| ID | Sampling rate | IF | VID:PID | Input |
| --- | --- | --- | --- | --- |
| 1 | 16367600 Hz | 4130400 Hz | 16c0:072f | USB or file |
| 6 | 16368000 Hz | 4092000 Hz | 0000:0000 in archive | File by default |

The archive does not establish a USB identity for SiGe V3. Do not invent one or
substitute the ID of another device. For live GNSS3 hardware, confirm its USB
descriptors and sample encoding before selecting a variant. Neither firmware nor
a firmware loader is included in this increment.

USB configuration 1, interface 2, alternate setting 0, bulk IN endpoint `0x86`,
and the historical Cypress start/stop vendor requests are preserved. The SiGe
sample conversion functions themselves are unchanged.

## USB checks on Windows

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
USB callback statuses, partial-block rejection and null handles; and GUI file copy
payload, explicit offset and end-of-file behavior. The hardware probe is separate
and is not an automated streaming test. See [verification results](VERIFICATION.md).
