# NGene Classic

NGene Classic is the original C codebase of the NGene software GNSS receiver. This
repository preserves the classic implementation, including its historical receiver
architecture. This increment retains only the SiGe front-end chain, including the
SiGe V3 recorded-sample format.

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
- `configuration/` contains the active receiver settings beside `ngene.exe`.
- `otherData/` contains the active supporting data; output goes into `logs/`.
- `sampleConfigurationFiles/` contains the templates used on the first build.
- `libraries/` contains bundled dependencies.

## Build

This package builds only native Windows x64 with Visual Studio clang-cl, Microsoft
SDK and runtime. Install Visual Studio's C++ x64, Windows SDK, Clang and CMake
components once. From PowerShell in this folder:

```powershell
.\build.cmd          # Compile, prepare dependencies and run tests
.\clean.cmd          # Clean compiled outputs without rebuilding
.\build.cmd clean    # Clean compiled objects, then rebuild and run tests
.\run.cmd            # Start the receiver with its configured input (USB by default)
```

The build downloads and compiles its dependencies automatically. The executable is
**`ngene.exe` in this folder**. The main configuration is always
**`configuration/GNSS_RX.conf` beside the executable**. No environment variables or
user home directories select a different configuration. Builds preserve existing
settings; `sampleConfigurationFiles/` is used only to fill missing files.

To process the existing workspace recording without changing USB settings:

```powershell
.\run.cmd --input recordings\native-ab.raw
```

For textual output, add `-t -v`. The recording is a local test artifact and is not
included in a fresh source checkout. See [Windows instructions](docs/WINDOWS.md)
for configuration details and [verification](docs/VERIFICATION.md) for checks.

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
