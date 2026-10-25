# Native Windows verification

The supported package is Windows x64 with Visual Studio clang-cl, Windows SDK
and a statically linked Microsoft runtime. Build using `build.cmd`; clean and
rebuild using `build.cmd clean`. Both execute the following tests.

| Check | Result |
| --- | --- |
| HackRF native RX settings, cleanup, queue ordering, overflow, disconnect and shutdown (simulated hardware) | Passed |
| EVO FFT port: 263,056 exact scalar comparisons, DFT, gains, guards and concurrency | Passed |
| Legacy SiGe conversion preservation | Passed |
| Native I/Q decoding, mixing and circular buffer | Passed |
| USB error handling | Passed |
| Simulated USB shutdown and restart | Passed |
| Win32 clocks, allocation, pthreads, options, text input, sockets and >4 GiB offsets | Passed |

The full receiver and all seven tests compile and link using Microsoft ABI native
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

The HackRF increment also retains the complete SiGe replay: 23 PVT solutions and
all 5,782 receiver-log lines identical after host timestamps are removed.
Native I/Q checks cover every CI8 component value, sign quantization, odd reads,
ring wrap and positive/negative synthetic RF Doppler through the actual Classic
carrier generator, SSE2 rotation and XOR MAC. HackRF enumeration found no device;
RF acquisition, hardware tuning accuracy and sustained reception are unverified.
See [HACKRF.md](HACKRF.md).
