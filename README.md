# NGene Classic

NGene Classic is the original C codebase of the NGene software GNSS receiver. This
repository preserves the classic implementation, including its historical receiver
architecture. It supports the SiGe front-end chain (including SiGe V3 recordings)
and native HackRF reception with a matching digital comparison profile.

The project is being maintained to refine its coding style and to identify and remove
bugs introduced by past changes. Changes should preserve the behavior and compatibility
of the classic receiver unless a specific correction requires otherwise.

## Source layout

- `receiver/` contains receiver startup, configuration, and channel management.
- `acquisition/`, `tracking/`, and `pvt/` implement signal acquisition, tracking, and
  position, velocity, and time processing.
- `utilityGnss/` contains GNSS-specific navigation and signal utilities.
- `frontEnd/` and `asmFolder/` contain front-end interfaces and optimized signal code.
- `osnma/` and `chimera/` contain authentication-related code.
- `configuration/` contains the active receiver settings beside the executable.
- `otherData/` contains the active supporting data; output goes into `logs/`.
- `sampleConfigurationFiles/` contains the templates used on the first build.
- `libraries/` contains bundled dependencies.

## Build

The project supports **native Windows x64** (Visual Studio clang-cl, Microsoft SDK
and runtime) and **Linux x86_64** (GCC or Clang). Both use the same receiver and SIMD
sources. Windows does not require Cygwin, MinGW or WSL.

On Windows, install Visual Studio's C++ x64, Windows SDK, Clang and CMake
components once. From PowerShell in this folder:

```powershell
.\build.cmd          # Compile, prepare dependencies and run tests
.\clean.cmd          # Clean compiled outputs without rebuilding
.\build.cmd clean    # Clean compiled objects, then rebuild and run tests
.\run.cmd            # Start the receiver with its configured input (USB by default)
```

The Windows build downloads and compiles its dependencies automatically. The
executable is **`ngene.exe` in this folder**.

On Linux (Debian/Ubuntu), install the development packages once, then build:

```sh
sudo apt update
sudo apt install build-essential cmake pkg-config libusb-1.0-0-dev libssl-dev libncurses-dev libhackrf-dev
sh build.sh          # Compile, run tests and prepare configuration/data
sh clean.sh          # Clean Linux compiled outputs without rebuilding
sh build.sh clean    # Clean, rebuild and run tests
sh run.sh            # Start the receiver
```

The Linux executable is **`ngene` in this folder**; intermediate files stay in
`build/linux/`, separately from `build/windows-native/`. See [Linux instructions](docs/LINUX.md).

On both platforms the main configuration is always
**`configuration/GNSS_RX.conf` beside the executable**. No environment variables or
user home directories select a different configuration. Builds preserve existing
settings; `sampleConfigurationFiles/` is used only to fill missing files.

To compare the two live frontends without editing the selected configuration:

```powershell
.\run.cmd --frontend sige
.\run.cmd --frontend hackrf
```

HackRF uses 8,183,800 complex pairs/s. The supplied profile tunes exactly to GPS
L1 (1,575,420,000 Hz); its signed NCO IF is derived from the configured RF centre.
The ring keeps the original signed CI8 pairs, without digital gain or clipping.
The separate HackRF SIMD path subtracts component DC and rotates with Q15 sine
and cosine coefficients. Products retain Q6 fractions in int16 intermediates;
correlations accumulate in int32 and acquisition powers in uint64.
Tuning, gains and antenna power are in `configuration/HackRF.conf`.
Live/replay findings and Doppler conventions are in [HACKRF.md](docs/HACKRF.md).

To process the existing workspace recording without changing USB settings:

```powershell
.\run.cmd --input recordings\native-ab.raw
```

For textual output, add `-t -v`. The recording is a local test artifact and is not
included in a fresh source checkout. See [Windows instructions](docs/WINDOWS.md)
for configuration details and [verification](docs/VERIFICATION.md) for checks.

The first [EVO FFT migration](docs/FFT_MIGRATION.md) ports the optimized SSE2
codelets and scheduling while retaining Classic's gains and Q14 coefficients.
Both build scripts include its numerical and concurrency regression check.

## Coding conventions and status

The adjacent NGene evolution project documents the supplied ESA BSSC(2000)1 Issue 1
reference and project-specific C conventions in `docs/service/CODING_STANDARD.md`.
Those conventions include English Doxygen documentation, naming rules, and formatting
guidance. NGene Classic retains legacy identifiers and comments, so this repository has
not yet completed a full migration or a formal ESA compliance audit. Changes to public
interfaces should account for existing callers and compatibility.

## Licensing

Third-party components retain their respective notices. Confirm the applicable rights
for the NGene Classic source before redistribution; this repository does not assign a
new license to the project.
