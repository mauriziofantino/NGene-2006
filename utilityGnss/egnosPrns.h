#ifndef NGENE_EGNOS_PRNS_H
#define NGENE_EGNOS_PRNS_H
#include "spreadingCodes.h"
/* Keep the first four slots in the order of the legacy code/FFT files. */
#define EGNOS_LEGACY_PRNS 4

static inline int EgnosPrnAt(int index)
{
    const int prns[EGNOS_NUMBER_OF_PRNS] = {120, 124, 126, 136, 121, 123};
    return index >= 0 && index < EGNOS_NUMBER_OF_PRNS ? prns[index] : -1;
}

static inline int EgnosPrnIndex(int prn)
{
    int i;
    for (i = 0; i < EGNOS_NUMBER_OF_PRNS; i++)
        if (EgnosPrnAt(i) == prn)
            return i;
    return -1;
}
/* IS-GPS-200N Table 6-I, sheets 2/3, G1/G2 in section 3.3.2.3.
 * NGene's file convention is inverted: binary 0 -> -1, binary 1 -> +1
 * (BPSK) or 0 (sign-mask mixer). This preserves the historical code bytes. */

static inline int GenerateEgnosCode(int prn, char *bpsk, char *mask)
{
    const int delays[EGNOS_NUMBER_OF_PRNS] = {145, 237, 886, 595, 175, 21};
    unsigned g1 = 1023, g2 = 1023, f1, f2;
    unsigned char a[1023], b[1023];
    int i, index = EgnosPrnIndex(prn);
    if (index < 0 || !bpsk || !mask)
        return 0;
    for (i = 0; i < 1023; i++) {
        a[i] = (g1 >> 9) & 1;
        b[i] = (g2 >> 9) & 1;
        f1 = ((g1 >> 2) ^ (g1 >> 9)) & 1;
        f2 = ((g2 >> 1) ^ (g2 >> 2) ^ (g2 >> 5) ^ (g2 >> 7) ^ (g2 >> 8) ^ (g2 >> 9)) & 1;
        g1 = ((g1 << 1) | f1) & 1023;
        g2 = ((g2 << 1) | f2) & 1023;
    }
    for (i = 0; i < 1023; i++) {
        int bit = a[i] ^ b[(i + 1023 - delays[index]) % 1023];
        bpsk[i] = bit ? 1 : -1;
        mask[i] = bit ? 0 : -1;
    }
    return 1;
}
#endif
