/** @file check_sige_samples.c
 * @brief Check unchanged SiGe converters against their scalar sample mappings.
 */
#include <stdio.h>
#include <string.h>
#include "sige.h"
#include "sigeV3.h"
static char backup[128] __attribute__((aligned(16)));
char *ngeneXmmsBackup = backup; /**< Scratch required by the classic SIMD ABI. */
/** @brief Check byte values, alignment, vector boundaries and in-place conversion.
 * @return Zero when all retained conversion cases match their scalar mappings.
 */
int main(void)
{
    static const int lengths[] = {0, 1, 15, 16, 17, 31, 95, 96, 97, 127, 128, 255, 256};
    static const signed char v3[4] = {1, -1, 3, -3};
    unsigned char input[512] __attribute__((aligned(16)));
    unsigned char output[512] __attribute__((aligned(16)));
    unsigned int variant, alignment, lengthIndex, phase, value, i;
    for (variant = 0; variant < 2; variant++) {
        for (alignment = 0; alignment < 16; alignment++) {
            for (lengthIndex = 0; lengthIndex < sizeof(lengths) / sizeof(lengths[0]);
                 lengthIndex++) {
                for (phase = 0; phase < 4; phase++) {
                    for (value = 0; value < 256; value++) {
                        int length = lengths[lengthIndex];
                        for (i = 0; i < sizeof(input); i++)
                            input[i] = (unsigned char)(value + i);
                        memcpy(output, input, sizeof(input));
                        if (variant == 0) {
                            SigeSamplesElab((char *)output + alignment, (char *)output + alignment,
                                            length, phase);
                        } else {
                            SigeV3SamplesElab((char *)output + alignment,
                                              (char *)output + alignment, length);
                        }
                        for (i = 0; i < sizeof(input); i++) {
                            unsigned char expected = input[i];
                            if (i >= alignment && i < alignment + (unsigned int)length) {
                                expected =
                                    variant ? (unsigned char)v3[input[i] & 3]
                                            : (unsigned char)(((input[i] & 1) ? -1 : 1) ^
                                                              ((((i - alignment + phase) & 3) >= 2)
                                                                   ? 0xfe
                                                                   : 0));
                            }
                            if (output[i] != expected) {
                                fprintf(stderr,
                                        "SiGe variant %u alignment %u length %d phase %u byte %u\n",
                                        variant, alignment, length, phase, i);
                                return 1;
                            }
                        }
                    }
                }
            }
        }
    }
    puts("SiGe sample mappings and buffer bounds passed.");
    return 0;
}
