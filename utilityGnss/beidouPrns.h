/**
 * @file beidouPrns.h
 *
 * @brief BeiDou B1C PRN parameter tables and code generator based on
 *        BDS-SIS-ICD-B1C-1.0 (Section 5.2.1).
 */
#ifndef NGENE_BEIDOU_PRNS_H
#define NGENE_BEIDOU_PRNS_H

#include <stdint.h>
#include <stddef.h>
#include "spreadingCodes.h"

#define BEIDOU_WEIL_LENGTH 10243
#define BEIDOU_B1C_CODE_LEN 10230
#define BEIDOU_MAXIMUM_PRN 63

typedef struct BeidouB1cPrnParamStruct {
    int prn;
    int w;               /**< Phase difference w (1 to 5121) */
    int p;               /**< Truncation point p (1 to 10243) */
    uint32_t first24;    /**< First 24 chips in octal from ICD Table 5-2 */
    uint32_t last24;     /**< Last 24 chips in octal from ICD Table 5-2 */
} TBeidouB1cPrnParam;

/**
 * Parameters for the 63 primary codes of the B1C data component
 * from Table 5-2 of BDS-SIS-ICD-B1C-1.0.
 */
static const TBeidouB1cPrnParam BEIDOU_B1C_PRN_PARAMS[BEIDOU_NUMBER_OF_PRNS] = {
    {  1, 2678,   699, 053773116U, 042711657U },
    {  2, 4802,   694, 032235341U, 017306122U },
    {  3,  958,  7318, 017633713U, 001145221U },
    {  4,  859,  2127, 041551514U, 005307430U },
    {  5, 3843,   715, 017205134U, 046341377U },
    {  6, 2232,  6682, 004254545U, 060604443U },
    {  7,  124,  7850, 070663435U, 050500234U },
    {  8, 4352,  5495, 016701045U, 027476454U },
    {  9, 1816,  1162, 032132263U, 070555612U },
    { 10, 1126,  7682, 025432015U, 043004057U },
    { 11, 1860,  6792, 031711760U, 007100551U },
    { 12, 4800,  9973, 025604267U, 015703521U },
    { 13, 2267,  6596, 065705054U, 012615632U },
    { 14,  424,  2092, 024700370U, 014267226U },
    { 15, 4192,    19, 072405456U, 025330122U },
    { 16, 4333, 10151, 002621063U, 015741134U },
    { 17, 2656,  6297, 000506754U, 062665617U },
    { 18, 4148,  5766, 044317266U, 007251312U },
    { 19,  243,  2359, 014463723U, 026526763U },
    { 20, 1330,  7136, 070234110U, 033737311U },
    { 21, 1593,  1706, 062002462U, 034564677U },
    { 22, 1470,  2128, 052312612U, 030142557U },
    { 23,  882,  6827, 034500023U, 052015335U },
    { 24, 3202,   693, 077312776U, 056550366U },
    { 25, 5095,  9729, 003712305U, 004531416U },
    { 26, 2546,  1620, 002501573U, 000717773U },
    { 27, 1733,  6805, 066632544U, 065070030U },
    { 28, 4795,   534, 000447425U, 065742570U },
    { 29, 4577,   712, 050643132U, 047674377U },
    { 30, 1627,  1929, 075652754U, 045534064U },
    { 31, 3638,  5355, 040610704U, 003636755U },
    { 32, 2553,  6139, 060523643U, 052040645U },
    { 33, 3646,  6339, 030522043U, 036645510U },
    { 34, 1087,  1470, 006337743U, 054551553U },
    { 35, 1843,  6867, 041375664U, 026065254U },
    { 36,  216,  7851, 020200053U, 003373656U },
    { 37, 2245,  1162, 022017103U, 015754234U },
    { 38,  726,  7659, 067327102U, 036032344U },
    { 39, 1966,  1156, 007154144U, 000456573U },
    { 40,  670,  2672, 045367715U, 020772116U },
    { 41, 4130,  6043, 046775773U, 004657766U },
    { 42,   53,  2862, 037123271U, 011652043U },
    { 43, 4830,   180, 034054132U, 063673657U },
    { 44,  182,  2663, 036632600U, 006140620U },
    { 45, 2181,  6940, 043776172U, 042103455U },
    { 46, 2006,  1645, 013675272U, 071143561U },
    { 47, 1080,  1582, 053755564U, 007122624U },
    { 48, 2288,   951, 060621674U, 032065524U },
    { 49, 2027,  6878, 022415634U, 047205733U },
    { 50,  271,  7701, 037363473U, 071732000U },
    { 51,  915,  1823, 077262176U, 011057010U },
    { 52,  497,  2391, 057132462U, 060447016U },
    { 53,  139,  2606, 013314107U, 077551540U },
    { 54, 3693,   822, 054474504U, 054256322U },
    { 55, 2054,  6403, 076023074U, 061777241U },
    { 56, 4342,   239, 060652454U, 037175533U },
    { 57, 3342,   442, 031371623U, 000254400U },
    { 58, 2592,  6769, 052134040U, 051277171U },
    { 59, 1007,  2560, 041013755U, 057767521U },
    { 60,  310,  2502, 020323763U, 060063316U },
    { 61, 4203,  5072, 052445270U, 012771226U },
    { 62,  455,  7268, 050735662U, 051142373U },
    { 63, 4318,   341, 027571255U, 047160627U }
};

static inline int BeidouPrnAt(int index)
{
    return index >= 0 && index < BEIDOU_NUMBER_OF_PRNS ? (index + BASE_PRN) : -1;
}

static inline int BeidouPrnIndex(int prn)
{
    return prn >= BASE_PRN && prn <= BEIDOU_NUMBER_OF_PRNS ? (prn - BASE_PRN) : -1;
}

/**
 * @brief Generates the B1C data component primary code for a given PRN.
 * @details Conforms to BDS-SIS-ICD-B1C-1.0 Section 5.2.1.
 *          Outputs both BPSK (+1/-1) and NGene sign-mask (0/-1) modulations.
 *
 * @param prn PRN index (1 to 63)
 * @param bpsk Output buffer for BPSK chips (+1/-1, length 10230)
 * @param mask Output buffer for NGene sign-mask chips (0/-1, length 10230)
 * @return 1 on success, 0 on invalid parameters
 */
static inline int GenerateBeidouB1cCode(int prn, char *bpsk, char *mask)
{
    static uint8_t legendre[BEIDOU_WEIL_LENGTH];
    static int legendreInitialized = 0;
    int idx, w, p, n;

    idx = BeidouPrnIndex(prn);
    if (idx < 0 || !bpsk || !mask)
        return 0;

    /* Initialize Legendre sequence modulo 10243 once:
     * L(0) = 0; L(k) = 1 if k is a quadratic residue modulo 10243, else 0 */
    if (!legendreInitialized) {
        int x;
        for (x = 0; x < BEIDOU_WEIL_LENGTH; x++)
            legendre[x] = 0;
        for (x = 1; x < BEIDOU_WEIL_LENGTH; x++) {
            int sq = (int)(((int64_t)x * x) % BEIDOU_WEIL_LENGTH);
            legendre[sq] = 1;
        }
        legendre[0] = 0;
        legendreInitialized = 1;
    }

    w = BEIDOU_B1C_PRN_PARAMS[idx].w;
    p = BEIDOU_B1C_PRN_PARAMS[idx].p;

    /* Primary code is cyclically truncated from Weil code:
     * W(k; w) = L(k) ^ L((k + w) % N)
     * c(n) = W((n + p - 1) % N; w) for n = 0 ... 10229 */
    for (n = 0; n < BEIDOU_B1C_CODE_LEN; n++) {
        int k = (n + p - 1) % BEIDOU_WEIL_LENGTH;
        int kw = (k + w) % BEIDOU_WEIL_LENGTH;
        int bit = legendre[k] ^ legendre[kw];
        /* NGene convention: binary 0 -> +1 (BPSK) / 0 (mask); binary 1 -> -1 (BPSK) / -1 (mask) */
        bpsk[n] = bit ? -1 : 1;
        mask[n] = bit ? -1 : 0;
    }
    return 1;
}

/**
 * @brief Helper to verify generated B1C code against ICD Table 5-2 octal values.
 *
 * @param prn PRN index (1 to 63)
 * @param bpsk BPSK chips buffer (+1/-1)
 * @return 1 if matched, 0 on mismatch or error
 */
static inline int VerifyBeidouB1cOctal(int prn, const char *bpsk)
{
    int idx, i;
    uint32_t f24 = 0, l24 = 0;
    idx = BeidouPrnIndex(prn);
    if (idx < 0 || !bpsk)
        return 0;

    for (i = 0; i < 24; i++) {
        int bit = (bpsk[i] < 0) ? 1 : 0;
        f24 = (f24 << 1) | (uint32_t)bit;
    }
    for (i = BEIDOU_B1C_CODE_LEN - 24; i < BEIDOU_B1C_CODE_LEN; i++) {
        int bit = (bpsk[i] < 0) ? 1 : 0;
        l24 = (l24 << 1) | (uint32_t)bit;
    }
    return (f24 == BEIDOU_B1C_PRN_PARAMS[idx].first24) &&
           (l24 == BEIDOU_B1C_PRN_PARAMS[idx].last24);
}

#endif /* NGENE_BEIDOU_PRNS_H */
