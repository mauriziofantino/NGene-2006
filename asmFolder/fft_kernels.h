/** @file fft_kernels.h @brief Internal x86_64 SSE2 FFT boundary. */
#ifndef NGENE_FFT_KERNELS_H
#define NGENE_FFT_KERNELS_H
#include <stddef.h>
/** Test Q14 headroom to select EVO's lossless packed averaging path. */
int FftInputInRangeSse2(const short *input, size_t size);
/** Fuse widths 2/4/8. size is a multiple of eight; scaling counts initial stages. */
void FftFirstStagesSse2(short *data, size_t size, unsigned scaledStages, int bounded,
                        unsigned *clipped);
/** Four complex butterflies per iteration; count is a positive multiple of four.
 * scaled divides this stage by two. bounded is valid only during the scaled prefix.
 * Full-int16 inputs use widened products/sums and saturate after scaling. */
void FftButterfliesSse2(short *upper, short *lower, const short *twiddle, size_t count, int scaled,
                        int bounded, unsigned *clipped);
#endif
