/** @file check_usb_errors.c
 * @brief Exercise USB callback and invalid-handle paths without hardware.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "usbInterface.h"
#include "ngeneConstants.h"

int USB_BLOCKSIZE = 8192; /**< Test transfer size. */

/** @brief Test logger stand-in; no receiver logging is initialized here. */
int SwrxLog(int severity, const char *format, ...)
{
    (void)severity;
    (void)format;
    return 0;
}
/** @brief Treat unexpected receiver termination as a failed check. */
void CloseNGene(TNGeneConfiguration *cfg, int result)
{
    (void)cfg;
    (void)result;
    exit(1);
}
/** @brief Memory copy stand-in for unexercised streaming code. */
void ArrayCopy(char *input, char *output, int length)
{
    memcpy(output, input, (size_t)length);
}
/** @brief Callback under test, implemented by the production USB interface. */
void LIBUSB_CALL UsbReadCallback(struct libusb_transfer *transfer);

/** @brief Verify statuses, short-transfer rejection and safe null-handle handling.
 * @return Zero when all checks pass.
 */
int main(void)
{
    TRxFe frontend = {0};
    TUsbTransferElement element = {0};
    struct libusb_transfer *transfer = libusb_alloc_transfer(0);
    int status;
    if (!transfer || CloseUsbFrontend(&frontend) != SUCCESS_RETURN ||
        StopUsbInterface(&frontend) != FAILURE_RETURN ||
        StartUsbInterface(&frontend) != FAILURE_RETURN) return 1;
    transfer->user_data = &element;
    transfer->length = 8192;
    transfer->actual_length = 8192;
    for (status = LIBUSB_TRANSFER_COMPLETED; status <= LIBUSB_TRANSFER_OVERFLOW; status++) {
        transfer->status = (enum libusb_transfer_status)status;
        UsbReadCallback(transfer);
        if ((int)element.status != status) return 1;
    }
    transfer->status = LIBUSB_TRANSFER_COMPLETED;
    transfer->actual_length = 8191;
    UsbReadCallback(transfer);
    if (element.status != NGENE_USB_OTHER_ERRORS) return 1;
    libusb_free_transfer(transfer);
    puts("USB statuses, short-transfer rejection and null-handle paths passed.");
    return 0;
}
