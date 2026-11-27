/**
 * @file check_beidou_codes.c
 *
 * @brief Comprehensive unit and contract tests for BeiDou B1C spreading codes,
 *        PRN table initialization, code generation, and PRN list management.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "beidouPrns.h"
#include "spreadingCodes.h"
#include "prnTable.h"
#include "prnList.h"
#include "ngeneConstants.h"

int SwrxLog(int severity, const char *format, ...)
{
    (void)severity;
    (void)format;
    return 0;
}

void CloseNGene(TNGeneConfiguration *cfg, int status)
{
    (void)cfg;
    (void)status;
    abort();
}

#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "BeiDou test failure at line %d: %s\n", __LINE__, #x);                 \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static int CheckBeidouGenerationAndOctals(void)
{
    char bpsk[BEIDOU_B1C_CODE_LEN];
    char mask[BEIDOU_B1C_CODE_LEN];
    int prn, k;

    /* Check invalid PRNs return 0 */
    CHECK(!GenerateBeidouB1cCode(0, bpsk, mask));
    CHECK(!GenerateBeidouB1cCode(-1, bpsk, mask));
    CHECK(!GenerateBeidouB1cCode(64, bpsk, mask));
    CHECK(!GenerateBeidouB1cCode(100, bpsk, mask));
    CHECK(!GenerateBeidouB1cCode(1, NULL, mask));
    CHECK(!GenerateBeidouB1cCode(1, bpsk, NULL));

    /* Verify all 63 PRNs */
    for (prn = 1; prn <= BEIDOU_NUMBER_OF_PRNS; prn++) {
        CHECK(GenerateBeidouB1cCode(prn, bpsk, mask) == 1);
        CHECK(VerifyBeidouB1cOctal(prn, bpsk) == 1);

        /* Verify chip values: BPSK is +1 or -1; mask is 0 or -1 */
        for (k = 0; k < BEIDOU_B1C_CODE_LEN; k++) {
            if (bpsk[k] == 1) {
                CHECK(mask[k] == 0);
            } else if (bpsk[k] == -1) {
                CHECK(mask[k] == -1);
            } else {
                CHECK(0 && "Invalid chip value");
            }
        }
    }
    return 0;
}

static int CheckBeidouPrnTableAndOversampling(void)
{
    TNGeneConfiguration cfg = {0};
    TPrnTable table = {0};
    char *codeOut = NULL;
    double codePhase, codePhaseFrac;
    int k;

    cfg.fs = 8183800.0;
    cfg.ts = 1.0 / cfg.fs;
    cfg.numberOfBeidouChannels = 4;
    snprintf(cfg.otherDataPath, sizeof(cfg.otherDataPath), "%s", NGENE_TEST_DATA_DIRECTORY);

    CHECK(InitPrnTable(&cfg, &table) == SUCCESS_RETURN);

    /* Verify that oversampled SIMD vectors hold replicated chip values */
    for (int p = 0; p < BEIDOU_NUMBER_OF_PRNS; p++) {
        s128 *bpskVec = table.beidouChipsOversampledBpsk[p];
        s128 *maskVec = table.beidouChipsOversampled[p];
        CHECK(bpskVec != NULL);
        CHECK(maskVec != NULL);

        /* Check first chip and last chip replication across 16 lanes */
        for (k = 0; k < 16; k++) {
            CHECK(bpskVec[0].b[k] == bpskVec[0].b[0]);
            CHECK(maskVec[0].b[k] == maskVec[0].b[0]);
            CHECK(bpskVec[BEIDOU_B1C_CODE_LEN - 1].b[k] == bpskVec[BEIDOU_B1C_CODE_LEN - 1].b[0]);
            CHECK(maskVec[BEIDOU_B1C_CODE_LEN - 1].b[k] == maskVec[BEIDOU_B1C_CODE_LEN - 1].b[0]);
        }
        /* Check wrap-around guard at 2 * codeLen */
        CHECK(bpskVec[2 * BEIDOU_B1C_CODE_LEN].b[0] == bpskVec[0].b[0]);
        CHECK(maskVec[2 * BEIDOU_B1C_CODE_LEN].b[0] == maskVec[0].b[0]);
    }

    CHECK(posix_memalign((void **)&codeOut, 16, 20000) == 0);

    /* Test GenerateCode for BEIDOU_SAT */
    for (int prn = 1; prn <= 5; prn++) {
        /* BPSK Modulation */
        codePhase = 0.0;
        codePhaseFrac = 0.0;
        memset(codeOut, 0xAA, 20000);
        CHECK(GenerateCode(&cfg, &table, BEIDOU_SAT, prn, BEIDOU_CODE_RATE,
                           &codePhase, &codePhaseFrac, BPSK_MODULATION, 1024, codeOut) == SUCCESS_RETURN);
        for (k = 0; k < 1024; k++) {
            CHECK(codeOut[k] == 1 || codeOut[k] == -1);
        }
        CHECK(codePhase > 0.0 && codePhase < BEIDOU_CODE_LEN);

        /* NGENE Modulation */
        codePhase = 100.5;
        codePhaseFrac = 0.5;
        memset(codeOut, 0xAA, 20000);
        CHECK(GenerateCode(&cfg, &table, BEIDOU_SAT, prn, BEIDOU_CODE_RATE,
                           &codePhase, &codePhaseFrac, NGENE_MODULATION, 2048, codeOut) == SUCCESS_RETURN);
        for (k = 0; k < 2048; k++) {
            CHECK(codeOut[k] == 0 || codeOut[k] == -1);
        }
    }

    /* Out of range PRN */
    codePhase = 0.0;
    codePhaseFrac = 0.0;
    CHECK(GenerateCode(&cfg, &table, BEIDOU_SAT, 64, BEIDOU_CODE_RATE,
                       &codePhase, &codePhaseFrac, BPSK_MODULATION, 512, codeOut) == FAILURE_RETURN);

    /* Code phase wrapping test */
    codePhase = BEIDOU_CODE_LEN - 1.0;
    codePhaseFrac = 0.0;
    UpdateCodePhase(&cfg, BEIDOU_SAT, &codePhase, &codePhaseFrac, (unsigned int)(cfg.fs * 0.02), BEIDOU_CODE_RATE);
    CHECK(codePhase >= 0.0 && codePhase < BEIDOU_CODE_LEN);

    free(codeOut);
    DeinitPrnTable(&table);
    return 0;
}

static int CheckBeidouPrnList(void)
{
    TPrnList list = {0};
    TSatConstellation constellation;
    int prn;
    char str[32];

    /* Test CreatePrnList with only BeiDou */
    CHECK(CreatePrnList(&list, 0, 0, 0, 1, 0, NULL, 0, NULL) == SUCCESS_RETURN);
    CHECK(GetListLength(&list) == BEIDOU_NUMBER_OF_PRNS);
    CHECK(GetNumberOfAvailableElementsInTheList(&list) == BEIDOU_NUMBER_OF_PRNS);

    /* Test GetNextAvailableFromPrnList */
    int firstPrn = GetNextAvailableFromPrnList(BEIDOU_SAT, &list);
    CHECK(firstPrn == 1);
    CHECK(GetNumberOfAvailableElementsInTheList(&list) == BEIDOU_NUMBER_OF_PRNS - 1);

    /* Test FindAndOccupy and FindAndFree */
    CHECK(FindAndOccupyInPrnList(BEIDOU_SAT, 2, &list) == SUCCESS_RETURN);
    CHECK(FindAndOccupyInPrnList(BEIDOU_SAT, 2, &list) == FAILURE_RETURN); /* Already occupied */
    CHECK(FindAndFreeInPrnList(BEIDOU_SAT, 2, &list) == SUCCESS_RETURN);

    /* Test FreeAllElementsInTheList */
    FreeAllElementsInTheList(&list, 0, 0, 0, 1);
    CHECK(GetNumberOfAvailableElementsInTheList(&list) == BEIDOU_NUMBER_OF_PRNS);

    CHECK(DestroyPrnList(&list) == SUCCESS_RETURN);

    /* Test String Parsing */
    FromStringToPrn("BDS1", &constellation, &prn);
    CHECK(constellation == BEIDOU_SAT && prn == 1);

    FromStringToPrn("BDS63", &constellation, &prn);
    CHECK(constellation == BEIDOU_SAT && prn == 63);

    FromStringToPrn("Beidou19", &constellation, &prn);
    CHECK(constellation == BEIDOU_SAT && prn == 19);

    FromStringToPrn("BEIDOU20", &constellation, &prn);
    CHECK(constellation == BEIDOU_SAT && prn == 20);

    FromStringToPrn("B1C21", &constellation, &prn);
    CHECK(constellation == BEIDOU_SAT && prn == 21);

    FromStringToPrn("C22", &constellation, &prn);
    CHECK(constellation == BEIDOU_SAT && prn == 22);

    /* Test String Formatting */
    FromPrnToString(BEIDOU_SAT, 19, str);
    CHECK(strcmp(str, "BDS19") == 0);

    return 0;
}

int main(void)
{
    printf("Running BeiDou B1C code and contract tests...\n");
    if (CheckBeidouGenerationAndOctals() != 0) {
        fprintf(stderr, "CheckBeidouGenerationAndOctals failed!\n");
        return 1;
    }
    printf(" - Generation & ICD octal tests: PASS\n");

    if (CheckBeidouPrnTableAndOversampling() != 0) {
        fprintf(stderr, "CheckBeidouPrnTableAndOversampling failed!\n");
        return 1;
    }
    printf(" - PRN table oversampling & GenerateCode tests: PASS\n");

    if (CheckBeidouPrnList() != 0) {
        fprintf(stderr, "CheckBeidouPrnList failed!\n");
        return 1;
    }
    printf(" - PRN list tests: PASS\n");

    printf("All BeiDou B1C tests PASSED.\n");
    return 0;
}
