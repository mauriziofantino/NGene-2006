# EVO to Classic: first FFT increment

This increment replaces only the FFT implementation behind Classic's existing
`InitFftTable` / `PerformFft` / `DeinitFftTable` interface. Acquisition, tracking,
thresholds, complex products, compression and the SiGe input path retain their
existing code. The aim is to converge incrementally with EVO while keeping a
receiver that acquires and computes PVT as a differential reference.

## Origin and scope

Source examined: the clean local `nav-ngene-evo` checkout at commit
`bb61e1b16f514e7848623d1d3fe2f7f3ef9cc146`. The GitHub connection could not access
the linked repository; the local checkout supplied the implementation and review.

The relevant EVO files are `src/dsp/fft/fft.c`, `src/dsp/x86/fft_sse2_32.c`,
`src/dsp/x86/fft_sse2.asm` and `docs/service/FFT_REVIEW.md`. The scalar/reference
and SIMD contracts were read together. The 32-bit source supplies portable SSE2
intrinsics; this port compiles those operations for the Microsoft x64 ABI,
without its 32-bit CPU probe or another architecture backend.

Ported changes:

- Fused eight-point SIMD codelets for the first three radix-2 stages.
- Four complex butterflies per main SSE2 iteration.
- Contiguous quartet gathers for bit reversal.
- Independent subtrees of at most 512 points for cache locality.
- Overflow-safe floor averages during the bounded scaled prefix.
- Signed32 products and sums when Classic's other gain modes require them;
  saturation follows scaling rather than destroying information before `/2`.
- Immutable tables, disjoint per-call workspaces and no global XMM backup.
- Unaligned SSE2 loads/stores; natural int16 alignment suffices.

At this FFT increment, only the two FFT C files were compiled with `/O2`. The compiler manages all
register clobbers and Microsoft ABI preservation. Historical receiver modules
with incomplete inline-assembly clobbers retain `/Od`.

## Deliberately retained Classic conventions

| Boundary | Classic adapter | EVO public API |
| --- | --- | --- |
| Scaling | First `-gain` stages divide by two; gain `-log2(N)..0` | Every stage divides by two, total `1/N` |
| Q14 tables | Truncate sine/cosine as Classic did | Round with `lround` |
| Input | Full signed16, with a widened path outside Q14 headroom | Components must be within inclusive +/-16384 |
| Direction | Forward `exp(-j*2*pi*k*n/N)` | Same forward direction |
| Correlation return transform | Existing forward-transform/time-reversal convention | Caller-dependent |

Keeping Classic's table quantization also preserves compatibility with the
existing cached `otherData/FFT_codes.dat`. Local-code gain `-6`, GPS signal gain
`-4` (or `-3` at low sample rates), and return-transform gain `-1` are unchanged.

The later HackRF Q8 boundary adds `PerformFftChecked`, with optional per-call
counts of actual saturated narrowing operations. `PerformFft` delegates with a
null counter and retains identical arithmetic. HackRF may retry a signal window
with additional stage shifts; its widened spectral product restores the extra
exponent before the original /65536 rounding. SiGe continues to use the original
gain schedule. See [HACKRF.md](HACKRF.md) for the numerical and live checks.
Importing EVO's fixed `1/N` scaling directly would change correlation amplitudes.
This adapter makes that boundary explicit for subsequent convergence work.

## Numerical verification

At this FFT increment, `build.cmd` ran six checks. `fft_evo_classic_contract` contains an independent
signed64 scalar oracle with scalar scatter/breadth-first traversal, independently
generated coefficients and explicit floor division. It checks 263,056 complete
transforms: sizes 8..16384, every supported gain, random and rail/odd inputs,
all 65,536 independent eight-point I/Q rail-sign assignments at all four gains,
misaligned guarded buffers and input immutability. It also checks independent
DFTs, a positive complex tone, invalid arguments, lifecycle and concurrent calls.

The explicit clipping regression has analytic bin-5 imaginary value
19777.2375029604 at gain `-3`; the new FFT returns 19777. The original returned
16383 because it saturated before dividing by two. Bit identity with the old
kernel is therefore not expected for overflowing cases.

A temporary comparison linked the actual EVO `NgFftReference` source. After
aligning the test-only table rounding and selecting `gain=-log2(N)`, 384
transforms across every supported size and Q14 random/rail inputs matched EVO
bit for bit. Production keeps Classic's tables and gain policy. This separates
the SIMD implementation from the numerical conventions at its boundary.

## Receiver replay and timing

Both kernels processed the same 794,615,808-byte SiGe recording with identical
settings. Each produced 23 PVT solutions at the same epochs and printed
coordinates. All 5,782 textual receiver-log lines matched after removing host
timestamps, including acquisition, tracking, bit synchronization and EOF.

The original FFT source SHA-256 was
`95a36b1dae1b9aafd91229b3b068995e7c52fefbced1800f4c81860bdb57b99f`.
A same-process benchmark compared that unmodified source with the new static
library on an Intel i7-1355U, clang-cl 22.1.3, native x64, one logical CPU.
Classic used its existing `/Od` preservation flags; the safe new FFT used `/O2`.
Each batch had 2,000 transforms, three batches with alternating backend order,
pre-created plans and the same deterministic input in [-64,64]. Medians:

| N | Gain | Original microseconds | New microseconds |
| ---: | ---: | ---: | ---: |
| 1024 | -4 | 4.30 | 2.46 |
| 4096 | -4 | 16.93 | 11.74 |
| 4096 | -1 | 17.16 | 13.11 |
| 16384 | -4 | 153.51 | 64.57 |

The benchmark outputs matched the original exactly for that input corpus.
These are complete FFT-call timings, including validation and permutation;
they do not measure receiver-wide CPU savings.

The first uncontrolled replay pair took about 9 and 19 seconds. A subsequent
old/new/new/old sequence pinned to the same logical CPU used identical receiver
objects and differed only in the linked FFT. Wall times were 9.84 / 9.62 / 9.23 /
9.49 seconds; CPU times were 8.38 / 8.61 / 8.72 / 9.00 seconds. Whole-receiver
throughput is comparable in this check, without a demonstrated CPU reduction.
Temporary sources, binaries and logs remain outside the repository.

## Next comparison boundary

This validates EVO-derived FFT operations inside the working Classic chain.
EVO's acquisition failure still needs diagnosis. The next isolated comparisons
should feed identical frames through carrier rotation, compression, replica
generation and complex correlation, explicitly tracking quantization and gains.
Kernel equivalence after aligning FFT conventions does not establish equality
of those surrounding processing stages.
