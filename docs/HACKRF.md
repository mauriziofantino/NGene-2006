# HackRF: first comparison with SiGe

Build from the project root with `build.cmd`. The script downloads upstream
libhackrf **v2026.01.3**, verifies SHA-256, and compiles it with native Windows
clang-cl, the Microsoft ABI/runtime, and the existing bundled libusb 1.0.30 and
pthreads4w. It is statically linked into `ngene.exe`; no external HackRF DLL or
toolchain is needed. The upstream BSD license is in
`libraries/libhackrf-LICENSE.txt`. Windows must expose HackRF through a compatible
WinUSB driver; this build does not replace a device driver or flash firmware.

## Selection and settings

```powershell
.\build.cmd
.\run.cmd --frontend hackrf
.\run.cmd --frontend sige
```

`--frontend` changes only this run. To select HackRF permanently, set
`Used FrontEnd` to **7** and `Input method` to **0** in `configuration/GNSS_RX.conf`.
Existing SiGe configuration files remain valid. When no HackRF row exists in
`Front_End.conf`, its comparison profile is added in memory. The supplied
template also includes the row explicitly.

`configuration/HackRF.conf` controls LNA gain (0..40 dB, step 8), VGA gain
(0..62 dB, step 2), RF amplifier, antenna power, baseband filter bandwidth, and
an optional `Serial number` tag followed by the hexadecimal device serial.
Without a serial, exactly one HackRF must be present. Defaults are LNA 32 dB,
VGA 32 dB, RF amplifier off, antenna power off and filter 5 MHz. The first build
copies this missing settings file without modifying existing files.

For recorded HackRF CI8 data with the same hardware settings:

```powershell
.\run.cmd --frontend hackrf --input "D:\recordings\hackrf-ci8.raw"
```

This expects raw signed I0,Q0,I1,Q1 bytes, without a WAV header, at the comparison
sample rate and tuning below. The existing `recordings/native-ab.raw` contains
SiGe marker-tagged bytes: replay it with `--frontend sige` instead.

## Sampling, tuning and Doppler

| Quantity | SiGe native IQ, current Classic settings | HackRF comparison profile |
| --- | --- | --- |
| NGene complex sample rate | 8,183,800 pairs/s | 8,183,800 pairs/s |
| Original transport | 16,367,600 marker-tagged bytes/s | 16,367,600 signed CI8 bytes/s |
| Classic positive-sign NCO IF | +38,400 Hz | +38,400 Hz |
| RF centre setting | Fixed hardware frequency plan | 1,575,458,400 Hz (GPS L1 + 38,400) |
| Native component amplitude passed to the receiver | -1 or +1 | -1 or +1 after sign quantization |
| Existing Doppler correction | +1 | -1 |

The [Colorado SiGe documentation](https://www.colorado.edu/lab/rf-satnav/resources/original-open-source-gpsgnss-sdr)
gives 8.1838 MHz, IF 38.400 kHz and one bit per I/Q component for v2. Its earlier
modules use a nominal 16.3676 MHz clock; v3 uses 16.368 MHz. These variants must
not be inferred from a label alone. GPS L1 / 16.368 MHz is exactly 96.25.
Relative to that reference, a 400 Hz clock difference gives
`L1 - 96.25 * 16367600 = 38500 Hz`. Its integer-factor contribution is
`96 * 400 = 38400 Hz`, and its quarter-factor contribution is `0.25 * 400 = 100 Hz`.
Thus the RF frequency plan matters; considering only the change of `fs/4` misses
the factor of 96 in the RF reference. These identities alone do not establish
an additional automatic 100 Hz shift in the physical IQ generator. Classic's
existing real-path settings give `4130400 - 16367600/4 = 38500 Hz`, whereas its
native IQ path uses the documented Colorado value of 38400 Hz. The 100 Hz
reference difference is retained and discussed in [NATIVE_IQ.md](NATIVE_IQ.md).
The HackRF profile matches the working Classic IQ settings; it does not assert
measured equality of independent hardware clocks.

HackRF is configured with `hackrf_set_sample_rate_manual(device, 8183800, 1)`
and `hackrf_set_freq(device, 1575458400)`. The
[official libhackrf API](https://github.com/greatscottgadgets/hackrf/blob/v2026.01.3/host/libhackrf/src/hackrf.h)
defines the manual rate as frequency/divider and supports separate RF tuning.
These are nominal hardware setpoints, subject to clock accuracy and synthesis.
No resampler or additional digital frequency shift is inserted.

For normal HackRF IQ orientation and physical RF Doppler `D`, the received
baseband carrier is `-38400 + D`. Classic's existing complex rotation multiplies
by `exp(+j*2*pi*(fif + fd)*t)`, so the cancelling NCO Doppler is `fd = -D`.
The existing `dopplerCorrection = -1` maps that to physical/code Doppler in
refinement, tracking, RINEX and PVT. I and Q are neither exchanged nor conjugated.
A complex input already represents signed frequencies at zero IF; the 38.4 kHz
offset here matches SiGe and separates the signal centre from receiver DC.

## Input and threading boundary

`frontEnd/hackrfFrontend.c` owns libhackrf initialization, configuration, RX
callbacks and cleanup. The callback copies borrowed raw CI8 bytes into a bounded
1 MiB queue (about 64.1 ms at this rate). It does not wait for free capacity,
allocate, write files or call Classic's assembly. Overflow stops reception
explicitly instead of silently dropping samples or restarting a discontinuous
stream. Disconnect/error releases blocked readers; stop joins callbacks before
their synchronization or storage is destroyed. libhackrf owns its USB event
thread; the SiGe USB event loop is not used for HackRF.

`FillInputBuffer` reads complete I/Q pairs, retains an incomplete pair across
odd fragments, and calls the SSE2 `QuantizeIqSigns` input adapter. Values below
zero map to -1; zero and positive values map to +1. This intentionally discards
HackRF's additional amplitude levels for the first SiGe comparison. Feeding
unscaled CI8 through the existing byte mixer/XOR accumulators would violate
their numeric limits. Full multibit reception is not claimed by this increment.

The existing 16 ms complex circular buffer stores I0,Q0,I1,Q1; indices count
pairs. A generic `nativeIq` flag selects the same existing SIMD complex rotation
for both frontends. Acquisition, confirmation, refinement, tracking, FFT, loop
parameters and PVT algorithms retain their existing implementations. Hardware
filters, gain/noise, quantization error and oscillator errors differ between the
two radios even when the nominal digital parameters match.

## Verification and limits

`build.cmd` passes seven tests. The new native RX fixture exercises production
code against the official vendor ABI with hardware calls simulated: exact
sample/tuning/gain settings, malformed settings, enumeration, failure cleanup,
millions of ordered CI8 bytes with queue wrap and odd reads, invalid callback
lengths, overflow, disconnect and shutdown. Existing native IQ tests exercise
every CI8 component value, SIMD alignments/tails, ring wrap and both RF Doppler
signs through the production carrier generator, complex rotation and XOR MAC.

The SiGe recording still produces 23 PVT solutions; all 5,782 textual receiver
log lines match the previous FFT version after host timestamps are removed.
HackRF enumeration on this host found no device. No live HackRF acquisition,
PVT, sustained USB performance or measured RF frequency accuracy is claimed.
