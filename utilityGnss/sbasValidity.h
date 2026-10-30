#ifndef NGENE_SBAS_VALIDITY_H
#define NGENE_SBAS_VALIDITY_H
#include <math.h>
#include "egnosCorrectionsDataStructure.h"

/* MT0 advertises test/unusable service. Retain RF tracking, but discard the
 * shared correction bank and reject data for a minute after the last MT0.
 * NGene currently supports one SBAS channel. This is not an integrity system. */

static inline int SbasAcceptMessage(TEgnosCorrections *bank, int type, double tow)
{
    int i, j;
    double dt;
    if (type == 0) {
        bank->testModeBlocked = 1;
        bank->lastTestModeTow = tow;
        bank->maskIodp = -1;
        bank->numOfPrnsInTheMask = 0;
        bank->degradationFactors.valid = 0;
        for (i = 0; i < EGNOS_ENTRIES; i++) {
            bank->fastCorrections[i].valid = 0;
            bank->fastCorrections[i].towOldValid = 0;
            bank->fastCorrections[i].toBeUsed = 0;
            bank->integrityInformation[i].valid = 0;
            bank->integrityInformation[i].toBeUsed = 0;
            bank->fastDegradationFactors[i].valid = 0;
            bank->longTermCorrections[i].validVelocityCode0 = 0;
            bank->longTermCorrections[i].validVelocityCode1 = 0;
        }
        bank->numOfIonoBands = bank->numOfReceivedIonoBands = 0;
        for (i = 0; i < EGNOS_IONO_BANDS; i++) {
            bank->ionoMask[i].maskIodi = -1;
            bank->ionoMask[i].numOfPointInTheMask = 0;
            for (j = 0; j < EGNOS_IONO_POINTS; j++) {
                bank->bandIonoCorrections[i].igpIonoCorrections[j].valid = 0;
                bank->bandIonoCorrections[i].igpIonoCorrections[j].usable = 0;
            }
        }
        return 0;
    }
    if (!isfinite(tow))
        return 0;
    if (bank->testModeBlocked) {
        /* An MT0 decoded before GPS time was known must not be forgotten.
         * Anchor a conservative full exclusion minute when time becomes known. */
        if (!isfinite(bank->lastTestModeTow)) {
            bank->lastTestModeTow = tow;
            return 0;
        }
        dt = tow - bank->lastTestModeTow;
        if (dt < -302400)
            dt += 604800;
        if (dt > 302400)
            dt -= 604800;
        if (dt < 60)
            return 0;
        bank->testModeBlocked = 0;
    }
    return 1;
}

typedef enum SbasReceptionStateEnum {
    SBAS_NO_RECENT_DATA,
    SBAS_TEST_SERVICE,
    SBAS_TIME_UNAVAILABLE,
    SBAS_DATA_DECODED
} TSbasReceptionState;

static inline TSbasReceptionState SbasReceptionState(int messageValid,
                                                     unsigned long long lastMessageMs,
                                                     unsigned long long currentMs,
                                                     int testModeBlocked, int gpsTimeValid)
{
    if (!messageValid || currentMs < lastMessageMs || currentMs - lastMessageMs > 3000)
        return SBAS_NO_RECENT_DATA;
    if (testModeBlocked)
        return SBAS_TEST_SERVICE;
    return gpsTimeValid ? SBAS_DATA_DECODED : SBAS_TIME_UNAVAILABLE;
}
#endif
