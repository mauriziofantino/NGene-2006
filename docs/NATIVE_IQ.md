# Native SiGe I/Q input

The marker-tagged stream from the connected `16c0:072f` sampler contains
alternating I and Q components. Bit 1 is set on I and clear on Q; bit 0 maps to
`+1` for zero and `-1` for one. This matches `LUT4120` and the alignment logic in
the [Colorado GN3Sv2 source bundle](https://ccar.colorado.edu/gnss/files/GN3Sv2.7z).
The [Colorado module documentation](https://www.colorado.edu/lab/rf-satnav/resources/original-open-source-gpsgnss-sdr)
specifies 8.1838 MHz complex sampling and 38.4 kHz residual IF for this format.
The separate packed 4-bit modes of the V3 firmware are a different wire format.

## Configuration

Keep frontend ID 1 and its existing transport configuration. Add these optional
tags to the active `GNSS_RX.conf`:

```text
[SiGe native IQ]
1
[SiGe IQ intermediate frequency (Hz)]
38500
```

Setting `SiGe native IQ` to zero, or omitting it, selects the working real-IF
path. Both USB and file input use raw marker-tagged sampler bytes. Native mode
does not expect the already decoded signed I/Q files exported by GN3Sv2.exe.
No firmware mode-switch command is sent.

Native `fs` is half the configured transport component rate: 8,183,800 complex
samples/s for the existing 16,367,600 byte/s entry. The native IF defaults to
38,500 Hz, matching the working real configuration:
`4,130,400 - 16,367,600/4 = 38,500 Hz`. Reported Doppler therefore uses the same
reference in both paths. An explicit IF remains available for other frequency
plans; the wire format alone does not determine it.

The earlier 38,400 Hz default was corrected on 2026-11-05. The resulting 100 Hz
Doppler bias triggered erroneous 125 Hz Galileo PLL false-lock corrections,
preventing I/NAV decoding despite apparently stable tracking. On the retained
capture, an isolated 4 GPS / 4 Galileo replay changes from zero mixed fixes to
13 fixes using Galileo (up to two Galileo satellites); both settings produce
23 total fixes and zero tracking losses. This verifies Galileo participation,
not improved position accuracy or live USB operation. With the operational
8 GPS / 4 Galileo / 1 EGNOS configuration, the 48.55-second capture ends before
Galileo has collected a complete ephemeris set, even with the corrected IF.
See `development/profiling/sige-galileo-20261105/` for the run references.

## Input boundary

The circular buffer stores signed bytes as `I0,Q0,I1,Q1,...`. Its dimensions,
head/tail positions, channel offsets, code delays and discarded-sample counters
count complex pairs. A stride converts these indices to byte addresses. The
buffer still spans 16 ms; the USB intermediate buffer remains 512 blocks.
Odd reads preserve an incomplete I component. Initial alignment and each USB
restart use the stream's I/Q markers; components from different USB generations
are never joined. An invalid marker sequence inside a stream is reported;
unmatched components are discarded while retaining adjacent valid I,Q pairs.
The resulting sample discontinuity is explicit rather than silently swapping
the two components.

The native decoder does not apply the old `+1,+1,-1,-1` real-IF modulation.
Acquisition, Doppler/code refinement, confirmation and tracking reuse the
existing carrier generator and phase accumulator. The input mixer computes
`(I+jQ)*(cos(theta)+j*sin(theta))` with SSE2 and NGene's existing sign-valued
oscillator. The oscillator uses the residual IF plus the selected or tracked
Doppler, at the complex sampling rate. Circular wrap and in-place oscillator
output aliases are supported.

The XOR MAC interprets a stored byte `a` as `a+1/2`, through the existing `N/2`
correction in its callers. Integer rotated components therefore require balanced
rounding of that half-level offset. Alternating numeric packing masks preserve
their amplitude and suppress a spurious low-IF oscillator contribution. Coarse
acquisition subtracts the returned packing masks exactly, using its original
`ArraySub` calls. This packing does not remodulate or combine I/Q into a real IF.

FFT, sample compression, code generation, correlation kernels, acquisition
search/threshold rules, discriminators, loop filters, navigation and PVT
algorithms are unchanged. Galileo's existing subcarrier removal is applied to
both native input components at the same input boundary. The existing GUI
histogram and real-valued spectrum display the I component in native mode;
they are not a complex-spectrum display.

## Verification on 2026-10-24

All five CTest checks passed. The native check covers decoding amplitudes against
the actual `SigeSamplesElab` real-IF implementation, marker validation, SIMD tails,
all oscillator/component sign combinations, every buffer split, in-place aliases,
odd reads, circular wrap, unmatched-component recovery and I/Q realignment
across USB restart. It also checks
amplitude and zero-quadrature results with the unmodified production XOR MAC and
its existing `N/2` correction, for both code polarities.

A single hardware capture supplied the same 794,615,808 raw bytes (about
48.55 seconds) to the pre-native reference executable and the native executable.
Both used the existing automatic eight-GPS-channel configuration and the same
acquisition/tracking/PVT settings. The capture ended after a USB timeout; the
complete preceding blocks had no reported transfer errors or I/Q marker errors.
Marker consistency alone cannot rule out losses of an even number of bytes.

Both paths produced 23 PVT solutions at matching GPS epochs. The mean horizontal
separation was 0.62 m, the maximum 0.78 m. Vertical differences ranged from
-2.18 m to +1.30 m. Quantized mixing and half-rate sample timing do not produce
bit-identical channel histories or PVT values.

For the six continuously tracked PRNs, statistics over samples after 10 s were:

| PRN | IF mean C/N0 | Native mean C/N0 | Aligned Doppler difference | Native/IF prompt RMS |
| --- | ---: | ---: | ---: | ---: |
| 16 | 45.2397 | 45.2705 | -0.0006 Hz | 1.00268 |
| 18 | 49.1851 | 49.1815 | -0.0009 Hz | 1.00271 |
| 26 | 50.7053 | 50.7218 | -0.0008 Hz | 1.00289 |
| 28 | 44.0606 | 44.0803 | -0.0006 Hz | 1.00209 |
| 29 | 49.7717 | 49.8056 | -0.0006 Hz | 1.00290 |
| 31 | 48.0914 | 48.1111 | -0.0009 Hz | 1.00256 |

C/N0 is in dB-Hz. These means cover nearly identical intervals rather than
sample-by-sample equality. Weak PRN 25 was also acquired, with different lock
histories in the two paths, and is excluded from this stable-channel comparison.

The original raw capture is retained as `recordings/native-ab.raw`.
Temporary comparison logs and CSV files have been removed. This comparison validates the
native input path and resulting GPS PVT on this recording, not general equivalence
under all signals or the stability of the USB transport.

A bounded direct USB run also reached native acquisition, tracking and bit
synchronization. Existing USB timeouts interrupted that run; live PVT was not
established by that check. Restart alignment and unmatched-component recovery
are tested separately from the transport's reliability.
