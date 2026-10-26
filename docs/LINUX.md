# Linux x86_64

Linux builds with a C11-capable GCC or Clang, CMake 3.20 or later and the distribution's
development libraries. The Windows build remains native clang-cl and does not
require Linux, WSL, Cygwin or MinGW.

## First build

On Debian/Ubuntu, install once:

```sh
sudo apt update
sudo apt install build-essential cmake pkg-config libusb-1.0-0-dev \
    libssl-dev libncurses-dev libhackrf-dev
```

From the project folder:

```sh
sh build.sh           # Configure, compile, run checks and prepare local data
sh run.sh            # Start the receiver
```

The executable is **`ngene` in the project folder**. Configuration is always
**`configuration/GNSS_RX.conf` beside that executable**, supporting data is in
`otherData/`, and the default output folder is `logs/`. The build fills missing
files from `sampleConfigurationFiles/` without overwriting existing settings.
Neither `HOME`, `NGENE_HOME` nor `.ngene` selects another configuration folder.

```sh
sh run.sh --frontend sige
sh run.sh --frontend hackrf
sh run.sh --frontend sige --input recordings/native-ab.raw -t -v
```

Recordings are local artifacts, not included in a fresh checkout. Select the
frontend matching the recording. HackRF recordings also need the original RF
centre/rate settings in `configuration/HackRF.conf` and `Front_End.conf`.

## Clean and rebuild

```sh
sh clean.sh          # Clean Linux outputs only; do not rebuild
sh build.sh clean    # Clean, rebuild and run checks
```

Linux intermediates stay in `build/linux/`; Windows uses `build/windows-native/`.
Both builds can coexist. Cleaning preserves configurations, recordings, data and
the other platform's executable. To limit parallel compilation, use e.g.
`NGENE_BUILD_JOBS=4 sh build.sh`. To select Clang on the first configuration,
install `clang` and use `CC=clang sh build.sh` with a fresh build directory.

## CMake directly

For a separate installation, without copying the binary into the source folder:

```sh
cmake -S . -B build/linux -DCMAKE_BUILD_TYPE=Release -DNGENE_BUILD_PLATFORM_CHECKS=ON
cmake --build build/linux --parallel 8
ctest --test-dir build/linux --output-on-failure
cmake --install build/linux --prefix "$HOME/ngene-classic"
"$HOME/ngene-classic/ngene" -t -v
```

Install into a new directory: CMake's install command deploys the configuration
templates too. Use `sh build.sh` for normal rebuilds that preserve local settings.

The FFT and validated intrinsic input/carrier/PRN sources are optimized. Legacy
inline assembly retains `-O0`, as on Windows, because its register clobber
declarations are incomplete. Acquisition, tracking and PVT mathematics are shared.

The Linux CI workflow builds and runs the eight shared checks with GCC and Clang.
Local compilation checks and their limits are recorded in
[VERIFICATION.md](VERIFICATION.md).

## USB access

File replay needs no USB permissions. Live reception needs user access to the
device through libusb. Install your distribution's HackRF udev rules (usually
provided by the `hackrf` package). For SiGe, grant access to VID/PID `16c0:072f`
using a local udev rule. Firmware loading and the SiGe device's configured sample
format remain the same requirements as on Windows. The build/checks do not open
either radio; live reception must be verified separately with hardware attached.
