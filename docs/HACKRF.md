# HackRF: native IQ reception and comparison with SiGe

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
Without a serial, exactly one HackRF must be present. The supplied profile uses
LNA 32 dB, VGA 48 dB, RF amplifier off, antenna power off, filter 5 MHz and centre
1,575,420,000 Hz (GPS L1). The antenna used in the live checks was externally
powered or passive, so antenna power remains off. The first build copies a
missing settings file without overwriting existing settings.

`Center frequency (Hz)` configures the actual RF centre, and NGene derives
`fif = centre - 1575420000` before initializing acquisition/tracking. Exact L1
tuning therefore uses zero IF; 1575458400 reproduces the earlier +38400 Hz NCO
comparison. Existing settings without this optional tag retain that earlier
centre for compatibility. Their unchanged fallback gains are LNA/VGA 32/32 dB.
Do not change the receiver IF independently of the hardware frequency.

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
| Classic positive-sign NCO IF | +38,400 Hz, current setting | centre minus GPS L1; 0 in supplied profile |
| RF centre setting | Fixed hardware frequency plan | Configurable; supplied profile 1,575,420,000 Hz |
| Native component amplitude passed to the receiver | -1 or +1 | Integer levels -7..+7 after DC removal/scaling |
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
The sample rate matches the working Classic IQ settings; this does not assert
measured equality of independent hardware clocks. The optional 38.4 kHz HackRF
offset was chosen to match the documented IQ setting, not inferred from a
change in the declared sample rate. Complex IQ already distinguishes frequency
sign at zero IF. A small RF offset alone does not eliminate receiver DC or make
the two hardware frequency plans equivalent.

HackRF is configured with `hackrf_set_sample_rate_manual(device, 8183800, 1)`
and `hackrf_set_freq(device, configured_centre)`. The
[official libhackrf API](https://github.com/greatscottgadgets/hackrf/blob/v2026.01.3/host/libhackrf/src/hackrf.h)
defines the manual rate as frequency/divider and supports separate RF tuning.
These are nominal hardware setpoints, subject to clock accuracy and synthesis.
No resampler or additional digital frequency shift is inserted.

For normal HackRF IQ orientation and physical RF Doppler `D`, the received
baseband carrier is `L1 - centre + D`. Classic's existing complex rotation multiplies
by `exp(+j*2*pi*(fif + fd)*t)`, so the cancelling NCO Doppler is `fd = -D`.
The existing `dopplerCorrection = -1` maps that to physical/code Doppler in
refinement, tracking, RINEX and PVT. I and Q are neither exchanged nor conjugated.
A complex input already represents signed frequencies at zero IF. DC suppression
must occur before narrowing the input; increasing hardware gain reduces DC's
relative contribution on the tested radio, but does not remove it mathematically.

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
odd fragments, and calls the SSE2 `AdaptIqClassic` input adapter over all complete
pairs in that read before splitting the result across the ring. It estimates
independent I/Q component means, subtracts them with Q8 precision, and applies
one common power-of-two gain selected from the combined centred RMS. Where
possible the scaled RMS lies between 1.5 and 3.0. Both components use the same
gain: their relative amplitude and phase are preserved before quantization.
Values are rounded to nearest (ties away from zero) and clipped to -7..+7,
including zero. A constant input maps to zero, rather than to a synthetic carrier.
This retains multiple amplitude levels; it does not retain all ADC precision.

The existing square-wave complex mixer then produces values at most +/-14;
its balanced XOR packing stays in [-15,14]. Even the worst eight-byte partial
MAC reduction stays in signed8 range. Feeding unscaled CI8 into this path would
violate those limits. No mixer, correlator, FFT gain, threshold or tracking-loop
algorithm was changed. This intrinsic-only input source is compiled with /O2;
historical inline-assembly modules retain their previous flags.

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
lengths, overflow, disconnect and shutdown. Native IQ tests exercise zero-IF
configuration, every CI8 component value, offset/noise and constant-input cases,
in-place/out-of-place SIMD alignment and tails against an independent scalar
rounding oracle, paired I/Q ring wrap and worst-case multilevel MAC rails.
Both RF Doppler signs are checked through Classic's carrier generator/rotation.

The SiGe recording still produces 23 PVT solutions; all 5,782 textual receiver
log lines match the previous FFT version after host timestamps are removed.
That was the initial SiGe regression; the same recording is rechecked after the
HackRF input changes. The newer hardware checks below supersede the initial
absence of a connected HackRF. They do not measure its absolute RF clock accuracy.

## Connected HackRF diagnosis

The original sign-only adapter reproduced the reported failure on a 59.996 s
raw CI8 capture: GPS entered tracking without bit synchronization or PVT; Galileo
did not reach tracking. At LNA/VGA 32/32 dB, measured component means were
I=1.099, Q=3.348 ADC counts, while centred RMS was about 1.62 counts. Narrowing
before removing that DC discarded most Q variation. After the input correction,
the **same recording** produced five GPS bit synchronizations and 29 PVT solutions.
Galileo still did not reach tracking in that low-gain capture.

At LNA/VGA 32/48 dB the measured centred RMS increased to about 12 counts while
DC means remained near I=1.1, Q=3.35. A new capture tuned exactly to L1 produced
21 GPS PVT solutions. Galileo PRNs 2, 34 and 36 reached FLL lock, and their
navigation logs contained decoded I/NAV words, including ephemeris and GST.
There were insufficient acquired Galileo satellites for a Galileo-only PVT.
A separate capture with the same gains at L1+38400 also produced 21 GPS PVTs;
one Galileo channel reached FLL lock. These sequential RF captures are not an
identical-sample comparison and do not establish that zero IF is universally
better. They show that an IF offset is not required for successful reception.

A direct USB run with eight GPS channels at exact L1, LNA/VGA 32/48 dB, ran for
90 seconds and produced **34 PVT solutions**, with 12 bit-synchronization events
and no reported HackRF queue/stream error. A separate 60-second mixed run with
eight GPS and eight Galileo channels reached Galileo FLL/PLL lock but produced
no PVT within that interval. Both stopped cleanly using the normal Windows
console control handler. These finite checks do not guarantee a time to first
fix, all-channel capacity or long-duration continuity. The connected device was
returned to idle, with antenna power off.

`tools/hackrf_capture.c` provides a finite raw CI8 capture with the production
native backend, stream monitoring and graceful shutdown. It writes the original
eight-bit samples before input adaptation. The retained local captures are
`recordings/hackrf-debug-ci8.raw` (L1+38400, VGA32) and
`recordings/hackrf-l1-ci8.raw` (exact L1, VGA48), with companion JSON metadata.
They are ignored by Git. Temporary receiver homes and diagnostic logs remain
outside the repository. Replay tuning must match the capture metadata.
