/**
 * @file generate_beidou_codes.c
 *
 * @brief Generates BeiDou B1C spreading codes and real PRN list according to BDS-SIS-ICD-B1C-1.0.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "beidouPrns.h"
#include "spreadingCodes.h"

int main(int argc, char **argv)
{
    const char *outDirs[] = {
        "otherData",
        "sampleConfigurationFiles/otherData"
    };
    int numDirs = sizeof(outDirs) / sizeof(outDirs[0]);
    int d, prn;
    char bpsk[BEIDOU_B1C_CODE_LEN];
    char mask[BEIDOU_B1C_CODE_LEN];

    (void)argc;
    (void)argv;

    printf("Generating BeiDou B1C spreading codes for %d PRNs...\n", BEIDOU_NUMBER_OF_PRNS);

    /* Verify all PRNs against ICD octal vectors first */
    for (prn = 1; prn <= BEIDOU_NUMBER_OF_PRNS; prn++) {
        if (!GenerateBeidouB1cCode(prn, bpsk, mask)) {
            fprintf(stderr, "Error generating PRN %d\n", prn);
            return 1;
        }
        if (!VerifyBeidouB1cOctal(prn, bpsk)) {
            fprintf(stderr, "Octal mismatch for PRN %d\n", prn);
            return 1;
        }
    }
    printf("All %d PRNs verified against ICD Table 5-2 octal test vectors.\n", BEIDOU_NUMBER_OF_PRNS);

    for (d = 0; d < numDirs; d++) {
        char datPath[512];
        char txtPath[512];
        FILE *datFile, *txtFile;

        snprintf(datPath, sizeof(datPath), "%s/Beidou_B1C_codes.dat", outDirs[d]);
        snprintf(txtPath, sizeof(txtPath), "%s/Beidou_real_PRNs.txt", outDirs[d]);

        datFile = fopen(datPath, "wb");
        if (!datFile) {
            fprintf(stderr, "Failed to open %s for writing\n", datPath);
            return 1;
        }

        for (prn = 1; prn <= BEIDOU_NUMBER_OF_PRNS; prn++) {
            GenerateBeidouB1cCode(prn, bpsk, mask);
            if (fwrite(bpsk, 1, BEIDOU_B1C_CODE_LEN, datFile) != BEIDOU_B1C_CODE_LEN ||
                fwrite(mask, 1, BEIDOU_B1C_CODE_LEN, datFile) != BEIDOU_B1C_CODE_LEN) {
                fprintf(stderr, "Failed writing codes to %s\n", datPath);
                fclose(datFile);
                return 1;
            }
        }
        fclose(datFile);
        printf("Written %s (%d bytes)\n", datPath, (int)(BEIDOU_NUMBER_OF_PRNS * 2 * BEIDOU_B1C_CODE_LEN));

        txtFile = fopen(txtPath, "w");
        if (!txtFile) {
            fprintf(stderr, "Failed to open %s for writing\n", txtPath);
            return 1;
        }
        fprintf(txtFile, "# This file contains the list of existing BeiDou satellites transmitting B1C\n");
        fprintf(txtFile, "# One PRN per row, NGene Classic reads just the first number in the row (the PRN)\n");
        /* Operational BDS-3 MEO and IGSO satellites with B1C payload (e.g. C19-C46) */
        const int operationalPrns[] = {
            19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
            32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46
        };
        int numOp = sizeof(operationalPrns) / sizeof(operationalPrns[0]);
        int i;
        for (i = 0; i < numOp; i++) {
            fprintf(txtFile, "%d\n", operationalPrns[i]);
        }
        fclose(txtFile);
        printf("Written %s (%d PRNs)\n", txtPath, numOp);
    }

    printf("BeiDou B1C code and PRN file generation complete.\n");
    return 0;
}
