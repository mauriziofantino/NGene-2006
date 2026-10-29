/**
 * @file usbDevicesConstants.h
 *
 * @author Mario Nicola
 *
 * @date 24/09/2014
 *
 * @version 1.0.1
 *
 * @brief Constants related to the different supported front-ends
 */
#ifndef USB_DEVICES_CONSTANTS_H_INCLUDED
#define USB_DEVICES_CONSTANTS_H_INCLUDED
/**
 * @def USB_WAIT_NS
 * @brief When CopySamplesIntoUsbIntermediateBuffer terminates, no completed USB transfers are
 * available, the USB thread sleep this time to allow the main to be scheduled
 */
#define USB_WAIT_NS 1000
/** @brief SiGe USB configuration and Cypress vendor requests. */
#define SIGE_CONFIG (1)
#define SIGE_INTF (2)
#define SIGE_ALTINTF (0)
#define SIGE_RX_EP (0x86)
#define SIGE_VRQT_VENDOR_OUT (0x40)
#define SIGE_VRQ_START_XFER (0x01)
#define SIGE_VRQ_STOP_XFER (0x01)
#define SIGE_WVALUE (0x01)
#define SIGE_STOP_WVALUE (0x00)
#endif /* USB_DEVICES_CONSTANTS_H_INCLUDED */
