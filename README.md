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
- `sampleConfigurationFiles/` contains example configuration and supporting data.
- `libraries/` contains bundled dependencies.

## Build

For the incremental SiGe-only Windows x64 build, see [Windows instructions](docs/WINDOWS.md).
It uses Cygwin GCC to retain the classic POSIX and assembly implementation.

The source tree includes CMake and Autotools build descriptions. Consult the selected
build system and the platform's dependency documentation before building. The receiver
uses system libraries such as ncurses; bundled components may have their own build or
license requirements.

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
