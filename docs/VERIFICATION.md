# Native Windows verification

Linux x86_64 support is also available; see [LINUX.md](LINUX.md). On 2026-10-26,
all 110 receiver/library/check translation units compiled to Linux ELF objects
using Clang with Debian bookworm development headers. This is a compilation
check, not a Linux link or runtime test. WSL cannot run on the current Windows
host because virtualization is unavailable. The Linux CI workflow builds and runs
the eight shared checks with both GCC and Clang on Ubuntu 24.04; the ninth check
is specific to Windows. Linux live USB and replay have not yet been validated.

The native Windows build uses Visual Studio clang-cl, Windows SDK
and a statically linked Microsoft runtime. Build using `build.cmd`; clean and
rebuild using `build.cmd clean`. Both execute the following tests.

| Check | Result |
| --- | --- |
| HackRF native RX settings, cleanup, queue ordering, overflow, disconnect and shutdown (simulated hardware) | Passed |
| EVO FFT port: 263,056 exact scalar comparisons, DFT, gains, guards and concurrency | Passed |
| HackRF Q8 statistics, mixing, exact int32 MAC, compression, FFT exponent recovery and signed Doppler | Passed |
| Carrier/PRN intrinsic optimization: 5,040 original-byte/phase preservation cases | Passed |
| Legacy SiGe conversion preservation | Passed |
| Native I/Q decoding, mixing and circular buffer | Passed |
| USB error handling | Passed |
| Simulated USB shutdown and restart | Passed |
| Win32 clocks, allocation, pthreads, options, text input, sockets and >4 GiB offsets | Passed |

The full receiver and all nine tests compile and link using Microsoft ABI native
libraries. The executable imports only Windows system DLLs: WS2_32, KERNEL32,
ADVAPI32, USER32 and CRYPT32. `--help` and `--version` run directly from PowerShell.

The root launcher was also run from `build/windows-native` with an obsolete
`NGENE_HOME` value deliberately set. The receiver selected the root executable's
`configuration` and `otherData` directories and processed the recording selected
with `--input recordings/native-ab.raw`. It reached EOF with 23 PVT solutions. The
configuration files were not changed by the input override.

That recording contains 794,615,808 original SiGe bytes (about 48.55 seconds).
The previous native Windows replay produced 23 solutions at the same epochs and
with the same printed coordinates as the earlier I/Q reference. The optional
I/Q path and its comparison with the original real-IF receiver are described in
[NATIVE_IQ.md](NATIVE_IQ.md).

Temporary validation logs are kept outside the repository. Obsolete local
logs, test captures and legacy toolchains have been removed. Only the original
recording in recordings/native-ab.raw is retained for optional playback.

Live USB stability remains a separate hardware validation; recorded PVT and
simulated USB tests do not establish continuous error-free sampling.

The incremental EVO FFT migration and differential replay results are documented
in [FFT_MIGRATION.md](FFT_MIGRATION.md).

The HackRF Q8 increment adds signed16 sample/mixer primitives, signed32 SSE2
correlation and checked FFT scaling. The connected VGA56 GPS run produced 63 PVT
solutions in 90 seconds without a reported USB or numeric-headroom error.
Galileo did not produce PVT in its separate 90-second run; identical-capture
Galileo replay also regressed versus the byte adapter. Increased precision is
verified, but a Galileo performance improvement is not claimed. Comparative
counts, CPU cost, ADC-rail measurements and the retained recordings are recorded
in [HACKRF.md](HACKRF.md).

The HackRF increment also retains the complete SiGe replay: 23 PVT solutions and
all 5,782 receiver-log lines identical after host timestamps are removed.
Native I/Q checks cover CI8 DC removal and multilevel scaling, odd reads,
ring wrap and positive/negative synthetic RF Doppler through the actual Classic
carrier generator, SSE2 rotation and XOR MAC. Connected HackRF captures now
produce GPS PVT and Galileo I/NAV decoding after frontend corrections. Absolute
hardware tuning accuracy and long-duration reception remain unmeasured.
The direct 90-second GPS USB check produced 34 PVT solutions without a reported
HackRF stream error and stopped through the normal console handler. A 60-second
mixed GPS/Galileo check reached Galileo carrier lock but no PVT in that interval.
See [HACKRF.md](HACKRF.md).
