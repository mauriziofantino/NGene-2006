/**
 * @file usbFrontEndConfiguration.h
 *
 * @author Mario Nicola
 *
 * @date 08/08/2014
 *
 * @version 1.0.0
 *
 * @brief This header defines the structure that stores NGene Classic configuration for USB FE.
 *        Parameters for all possible FEs are stored.
 */
#ifndef USB_FRONT_END_CONFIGURATION_H_INCLUDED
#define USB_FRONT_END_CONFIGURATION_H_INCLUDED
#include "ngeneConstants.h"
/* Supported frontend IDs must match the Front_End.conf configuration file. */
#define NGENE_FE_SIGE 1
#define NGENE_FE_SIGE_V3 6
#define NGENE_FE_HACKRF 7
typedef struct HackrfConfigurationStruct {
    int loaded;
    int centerFrequencyHz, bandwidthHz, lnaGain, vgaGain, amplifier, antennaPower;
    int autoTuneGains, autoTuneTargetRms;
    char serial[33];
} THackrfConfiguration;
/**
 * @struct usbFeParsStruct
 * @brief Contains parameters for one of the possible FEs.
 */
typedef struct usbFeParsStruct {
    int index;
    char name[MAX_ROW_LENGTH];
    float intermediateFrequency; /**< \f$ f_{IF} \f$ */
    float samplingRate;          /**< \f$ f_s \f$ */
    int dopplerCorrection;       /**< +1/-1 depending on the FE frequency plan */
    unsigned int resolution;     /**< Resolution in number of used bit of each sample */
    unsigned int meanValue; /**< If unsigned char samples are used, mean value is grater than 0.
                         NGene must convert to signed char, subtracting the mean value */
    int vid;                /**< Vendor ID of the USB front end */
    int pid;                /**< Product ID of the USB front end */
} TUsbFePars;
/**
 * @struct usbFeParametersStruct
 *
 * @brief
 */
typedef struct usbFeConfigurationStruct {
    int numberOfSupportedFes;
    TUsbFePars *feList;
} TUsbFeConfiguration;
#endif /* USB_FRONT_END_CONFIGURATION_H_INCLUDED */
