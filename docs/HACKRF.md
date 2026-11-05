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
LNA 32 dB, VGA 56 dB, RF amplifier off, antenna power off, filter 5 MHz and centre
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
| Classic positive-sign NCO IF | +38,500 Hz, current setting | centre minus GPS L1; 0 in supplied profile |
| RF centre setting | Fixed hardware frequency plan | Configurable; supplied profile 1,575,420,000 Hz |
| Native component amplitude in the ring | -1 or +1 | original signed ADC byte (-128..127), no digital gain or limiter |
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
native IQ path now uses the same 38500 Hz reference. The former 38400 Hz
setting biased Galileo's DLL/PLL false-lock comparison and prevented I/NAV
decoding in the retained replay; see [NATIVE_IQ.md](NATIVE_IQ.md).
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

`FillInputBuffer` reads complete I/Q pairs and retains incomplete pairs across
odd reads. HackRF uses `THackrfIq8 { int8_t i, q; }`: exactly two original ADC
bytes per sample. Ring indices count complex time samples and retention remains
16 ms. There is no input normalization, RMS target or +/-7 limiter. SiGe keeps
its existing decoding and DSP path.

`asmFolder/hackrfInput.c` implements a separate HackRF mixer. SSE2 sums the raw
components, then subtracts the integer-rounded component means over the exact
requested window, independently of USB chunks and physical ring splits. A
65,536-entry table stores full sine/cosine coefficients in Q15. SSE2 PMADDWD
performs the complex rotation, retaining six fractional bits in int16 output.
This Q6 representation preserves fractional products; it is not a digital gain.
Sums are int32 and completed powers are uint64. Only completed tracking
correlations enter the existing floating-point discriminators. Per-sample
mixing/correlation does not cast to float. PRN/BOC signs remain binary masks.
The NCO frequency/phase update uses double once per block.

Compression retains Classic's sample groups in int32. A common exponent packs
each complete complex window for the fixed-point FFT. Both forward and inverse
transforms check stage saturation and retry from their original inputs if
required. Extra transform exponents are carried to uint64 powers and the noise
variance, rather than overflowing the inverse transform or a uint32 threshold.
The HackRF spectral product rounds symmetrically to avoid the negative pedestal
introduced by flooring every small spectral bin. SiGe's product and FFT
rounding remain unchanged. FFT sizes, search bins, M/N tests and loop/PVT
algorithms are retained. See [the tracking review](HACKRF_TRACKING_REVIEW.md).

HackRF acquisition binary logs now store coarse threshold/CAF powers and
refinement powers as uint64 (the existing parameter headers are unchanged).
Readers of those optional logs must select this width for HackRF. SiGe's
corresponding binary fields retain their original uint32 layout.

The RF frontends can still differ in analogue response/noise, oscillator error
and interference, even with matching nominal digital settings.

## Verification and limits

`build.cmd` runs nine tests. The new native RX fixture exercises production
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

## Historical Q8 precision and FFT headroom checks (2026-10-25)

The previous Q8 path replaced the whole-unit HackRF quantization described in the earlier
diagnosis. A trial without the existing peak limiter degraded replay results:
the raw stream contains brief ADC-rail bursts even at moderate gain. Their
origin has not been established. That Q8 increment therefore preserved the
limiter and increases fractional precision inside it; it does not claim to keep
every saturated/outlier sample unchanged. The current raw-CI8 path described
above supersedes this normalization and limiter; the following measurements
document that earlier version.

Three-second raw captures at LNA32 and VGA48/52/56/58 measured centred component
RMS of 11.76/16.92/25.26/31.40 ADC counts. ADC-rail fractions were approximately
0.22/0.29/0.31/0.36 percent. The existing complex 4096-point FFT also measured
the raw DC component and strongest non-DC bins. These measurements select VGA56
as a tested profile with more ADC use; they do not establish an optimum RF gain
for every antenna/interference environment. Both the template and local settings
now use LNA32/VGA56, exact L1, RF amplifier off and antenna power off.

The retained 70.006-second VGA56 capture is
`recordings/hackrf-l1-vga56-ci8.raw`, with companion JSON metadata. Its original
CI8 samples measured RMS 24.95 counts. Replay comparisons use identical samples,
configuration, ephemeris/code data and separate temporary receiver homes:

| Capture / receiver | Previous byte path | Q8 path |
| --- | --- | --- |
| VGA48, 59.996 s, GPS PVT | 21 | 21 |
| VGA48, Galileo FLL lock events / PVT | 3 / 0 | 1 / 0 |
| VGA56, 70.006 s, GPS PVT | 29 | 29 |
| VGA56, Galileo FLL lock events / PVT | 4 / 2 | 3 / 0 |

Thus increased numerical precision is verified, but improved Galileo reception
is **not** established; this short replay shows fewer locks/PVTs. Loop/search
parameters were not retuned to hide that difference. On the VGA56 GPS capture,
CPU time was 5.83 seconds for the byte path and 7.75 for Q8 (8.55 seconds wall
time for 70 seconds of signal). These are single runs, not a capacity benchmark.
The checked FFT recovered headroom without a reported unrepresentable input or
spectral product. A strong synthetic tone separately proves detection, retry
and exponent restoration against an independent expected result.

Direct native USB checks with Q8 and VGA56 completed cleanly: an eight-GPS-channel
90-second run produced **63 PVT solutions**; a separate eight-Galileo-channel
90-second run produced four FLL-lock events and no PVT. Neither reported a queue,
stream or numeric-headroom error. Both stopped through the normal console
handler and returned the device to idle. These are finite checks, not proof of
long-duration continuity or a universal advantage over SiGe.

The SiGe recording still yields 23 PVT solutions. All 5,717 ordered log lines
from receiver initialization through shutdown match the previous reference
exactly after host timestamps are removed. The new test also checks every CI8
code, mean/energy SIMD reductions over a full-size block, fractional tails,
split-ring rotation, exact signed32 MACs, original compression groups, FFT
exponents/saturation recovery, spectral-product overflow endpoints and both
Doppler signs at zero IF. All nine native tests pass.

## CPU and PVT follow-up (2026-10-26)

The receiver now computes the six HackRF early/prompt/late correlations in one
SSE2 pass. Its output is exactly equal to six independent signed32 dot products.
Carrier and PRN generation also use SSE2 intrinsics with compiler-managed
registers and /O2, replacing their incomplete inline-assembly register contracts.
Tables, phase quantization, short/long code-chip selection, NCO increments and
phase updates are unchanged. Other historical assembly sources retain /Od.
The new preservation fixture checks 5,040 cases against hashes recorded before
the conversion, including both frequency signs, phase wrapping, code/carrier
tails, output guards, GPS/Galileo/EGNOS codes and both code representations.
All bytes and returned phases match. The HackRF GPS RINEX file also remains
byte-identical after these CPU optimizations.

With profiling enabled in both builds, three alternating replay runs measured
median total CPU time of **8.86 s before / 7.70 s after** for the same 70-second
capture (about 13% lower). Individual after-runs ranged from 6.52 to 9.08 s, so
this is not a deterministic speed guarantee. In the stage profiles, GPS tracking
went from 9.97 to 7.54 microseconds per 1 ms channel block. The final build has
profiling disabled. Its final GPS replay consumed 6.88 CPU seconds for 70 seconds
of signal; SiGe consumed 5.05 for 48.55 seconds. Those captures have different
satellite workloads and are not a controlled RF comparison. No live USB CPU
measurement was possible in this follow-up because the radios were disconnected.

The supplied approximate Maps point resolves to **44.243606 N, 7.396659 E**.
Relative to that pin, the mean SiGe fix is about 5.2 m away horizontally, the
70-second Q8 HackRF replay about 9.2 m, and the separate earlier 90-second live
HackRF run about 4.5 m. These were not simultaneous captures, and an approximate
map pin is not a surveyed antenna reference. They do not certify absolute errors.

The initially observed 35.19 m difference in mean height between byte and Q8
HackRF replays does not compare the same PVT input set: both use four satellites,
but Q8 uses G16 where the byte replay uses G31. G31's navigation data become
available later in Q8, and the existing minimum of ten carrier-smoothing updates
excludes it through the end of the short capture. The diagnostics explicitly log
`Discarded GPS 31 from PVT (CSM counter: 1)` through `10`. This selection policy,
ephemeris parser and smoothing algorithm were not changed.

Across 29 paired RINEX epochs, the 87 raw pseudoranges for common G18/G26/G29
differ by **0.251 m RMS / 0.542 m maximum** after removing each epoch's common
clock offset. Epoch times differ by at most 0.107 ms. This does not demonstrate a
new tens-of-metres differential tracking bias on those satellites; it also does
not exclude errors common to both receivers or their clock offsets. To control
satellite selection, temporary manual configurations forced G18/G26/G29/G31.
With smoothing disabled only in those diagnostic copies, 29 paired solutions
had mean Q8-minus-byte differences of **-0.17 m E, -0.20 m N, -2.08 m U**;
the maximum vertical difference was 14.6 m. Production loop, PVT and smoothing
settings remain unchanged. The available short recordings cannot establish a
long-term antenna-position bias.

Receiver height is **WGS84 ellipsoidal h**. Ncurses now labels it `h WGS84 [m]`;
the PVT values are not shifted. The receiver's existing coarse geoid model gives
N approximately +48.49 m at this point, so its approximate sea-level height would
be H=h-N. The user's roughly 950 m height did not specify a vertical datum;
subtracting 48.49 m is therefore not treated as a measured accuracy correction.
No horizontal or vertical constant was introduced to force a desired position.
