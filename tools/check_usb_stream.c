/** @file check_usb_stream.c
 * @brief Check production USB buffer shutdown and restart with simulated I/O.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "usbInterface.h"
#include "ngeneConstants.h"
#include "signalHandler.h"
#include "usbDevicesConstants.h"
volatile sig_atomic_t ngeneStopRequested = 0;
int USB_BLOCKSIZE = 16;
static int submissions;
static int startCommands;
static int stopCommands;
static int eventCalls;
static int lastEventWait;
static pthread_t receiverThread;
static pthread_mutex_t progressMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t progressCondition = PTHREAD_COND_INITIALIZER;
/* Internal production helpers used to construct the simulated endpoint. */
TUsbEndpointHandle *MakeEndpointHandle(TUsbFe *usb, libusb_device_handle *handle);
void DeleteEndpointHandle(TUsbEndpointHandle *endpoint);

int SwrxLog(int severity, const char *format, ...)
{
    (void)severity;
    (void)format;
    return 0;
}

void CloseNGene(TNGeneConfiguration *cfg, int result)
{
    (void)cfg;
    (void)result;
    abort();
}

void ArrayCopy(char *input, char *output, int length)
{
    /* Production ArrayCopy saves XMM registers in a global receiver buffer.
     * Calling it from the USB producer can corrupt concurrent acquisition. */
    if (!pthread_equal(pthread_self(), receiverThread))
        abort();
    memcpy(output, input, (size_t)length);
}
/* Link wrappers replace only the hardware operations used by this check. */
int LIBUSB_CALL __wrap_libusb_handle_events_timeout(libusb_context *context,
                                                    struct timeval *timeout)
{
    (void)context;
    eventCalls++;
    lastEventWait = timeout->tv_usec;
    return 0;
}

int LIBUSB_CALL __wrap_libusb_submit_transfer(struct libusb_transfer *transfer)
{
    (void)transfer;
    pthread_mutex_lock(&progressMutex);
    submissions++;
    pthread_cond_signal(&progressCondition);
    pthread_mutex_unlock(&progressMutex);
    return 0;
}
int LIBUSB_CALL __wrap_libusb_control_transfer(libusb_device_handle *handle, uint8_t type,
                                               uint8_t request, uint16_t value, uint16_t index,
                                               unsigned char *data, uint16_t length,
                                               unsigned int timeout)
{
    (void)handle;
    (void)timeout;
    if (type != 0x40 || request != 0x01 || value > 1 || index != 0 || data != NULL || length != 0)
        abort();
    if (value == 0)
        stopCommands++;
    else
        startCommands++;
    return 0;
}

int LIBUSB_CALL __wrap_libusb_cancel_transfer(struct libusb_transfer *transfer)
{
    TUsbTransferElement *element = transfer->user_data;
    element->status = NGENE_USB_TRANSFER_CANCELLED;
    return 0;
}

static void *Produce(void *frontend)
{
    CopySamplesIntoUsbIntermediateBuffer(frontend);
    return NULL;
}

int main(void)
{
    TRxFe frontend = {0};
    TUsbFe *usb = &frontend.usbFe;
    TUsbEndpointHandle *endpoint;
    pthread_t producer;
    char output[16];
    int index;
    receiverThread = pthread_self();
    pthread_mutex_init(&usb->frontEndMutex, NULL);
    pthread_cond_init(&usb->intermediateBufferFull, NULL);
    pthread_cond_init(&usb->intermediateBufferEmpty, NULL);
    usb->usbBuffer = calloc(USB_BUFFER_SIZE, USB_BLOCKSIZE);
    usb->usbIntermediateBuffer = calloc(USB_INTERMEDIATE_BUFFER_SIZE, USB_BLOCKSIZE);
    if (!usb->usbBuffer || !usb->usbIntermediateBuffer)
        return 1;
    usb->endpoint = 0x86;
    usb->vrqtVendorOut = SIGE_VRQT_VENDOR_OUT;
    usb->vrqStartXfer = SIGE_VRQ_START_XFER;
    usb->vrqStopXfer = SIGE_VRQ_STOP_XFER;
    usb->wValue = SIGE_WVALUE;
    /* The fake device handle is never passed to real libusb I/O. */
    usb->epHandle = MakeEndpointHandle(usb, (libusb_device_handle *)&frontend);
    endpoint = usb->epHandle;
    if (!endpoint)
        return 1;
    /* Fill the last free block, then request shutdown while the producer waits. */
    usb->usbIntermediateBufferWriteIndex = USB_INTERMEDIATE_BUFFER_SIZE - 1;
    endpoint->transferArray[0].status = NGENE_USB_TRANSFER_COMPLETED;
    endpoint->transferArray[1].status = NGENE_USB_TRANSFER_COMPLETED;
    memset(endpoint->transferArray[0].destinationBuffer, 0x11, USB_BLOCKSIZE);
    memset(endpoint->transferArray[1].destinationBuffer, 0x22, USB_BLOCKSIZE);
    memset(usb->usbIntermediateBuffer, 0x55, USB_BLOCKSIZE);
    if (pthread_create(&producer, NULL, Produce, &frontend) != 0)
        return 1;
    pthread_mutex_lock(&progressMutex);
    while (!submissions)
        pthread_cond_wait(&progressCondition, &progressMutex);
    pthread_mutex_unlock(&progressMutex);
    pthread_mutex_lock(&usb->frontEndMutex);
    usb->shutdownRequested = TRUE_FLAG;
    pthread_cond_broadcast(&usb->intermediateBufferFull);
    pthread_mutex_unlock(&usb->frontEndMutex);
    pthread_join(producer, NULL);
    if (submissions != 1 || usb->usbIntermediateBuffer[0] != 0x55)
        return 1;
    if (eventCalls < 2 || lastEventWait != 0 || usb->usbBufferFullWaits == 0)
        return 1;
    /* Empty reads must also return immediately during shutdown. */
    usb->flagBufferFull = FALSE_FLAG;
    if (ReadUsbInterface(&frontend, output, sizeof(output)) != 0)
        return 1;
    usb->shutdownRequested = FALSE_FLAG;
    /* A timeout at a nonzero index must restart consumption at transfer zero. */
    for (index = 0; index < USB_BUFFER_SIZE; index++) {
        endpoint->transferArray[index].status = NGENE_USB_TRANSFER_OFF;
    }
    usb->usbBufferIndex = 3;
    usb->usbSamplesRead = TRUE_FLAG;
    usb->usbStatus = NGENE_USB_OK;
    endpoint->flagStarted = TRUE_FLAG;
    endpoint->transferArray[3].status = NGENE_USB_TRANSFER_TIMED_OUT;
    CopySamplesIntoUsbIntermediateBuffer(&frontend);
    if (usb->usbBufferIndex != 0 || usb->usbStatus != NGENE_USB_OK ||
        submissions != USB_BUFFER_SIZE + 1)
        return 1;
    endpoint->transferArray[0].status = NGENE_USB_TRANSFER_COMPLETED;
    CopySamplesIntoUsbIntermediateBuffer(&frontend);
    if (lastEventWait != 0)
        return 1;
    if (ReadUsbInterface(&frontend, output, sizeof(output)) != sizeof(output) ||
        output[0] != 0x11 || usb->usbBufferIndex != 1)
        return 1;
    /* Pending reads may wait for events; completed reads above must not. */
    CopySamplesIntoUsbIntermediateBuffer(&frontend);
    if (lastEventWait != 100000)
        return 1;
    /* A later completed transfer must wait for the earlier pending one.
     * Once both are ready, partial consumer reads must preserve byte order. */
    memset(endpoint->transferArray[1].destinationBuffer, 0x22, USB_BLOCKSIZE);
    memset(endpoint->transferArray[2].destinationBuffer, 0x33, USB_BLOCKSIZE);
    endpoint->transferArray[2].status = NGENE_USB_TRANSFER_COMPLETED;
    CopySamplesIntoUsbIntermediateBuffer(&frontend);
    if (usb->usbIntermediateBufferReadIndex != usb->usbIntermediateBufferWriteIndex ||
        usb->usbBufferIndex != 1)
        return 1;
    endpoint->transferArray[1].status = NGENE_USB_TRANSFER_COMPLETED;
    CopySamplesIntoUsbIntermediateBuffer(&frontend);
    if (usb->usbBufferIndex != 3 || ReadUsbInterface(&frontend, output, 7) != 7)
        return 1;
    for (index = 0; index < 7; index++)
        if (output[index] != 0x22)
            return 1;
    if (ReadUsbInterface(&frontend, output, 9) != 9)
        return 1;
    for (index = 0; index < 9; index++)
        if (output[index] != 0x22)
            return 1;
    if (ReadUsbInterface(&frontend, output, sizeof(output)) != sizeof(output))
        return 1;
    for (index = 0; index < (int)sizeof(output); index++)
        if (output[index] != 0x33)
            return 1;
    /* Cross the physical end of the intermediate USB queue in one read. */
    {
        char wrapped[32];
        usb->usbIntermediateBufferWriteIndex = usb->usbIntermediateBufferReadIndex =
            USB_INTERMEDIATE_BUFFER_SIZE - 1;
        memset(endpoint->transferArray[3].destinationBuffer, 0x44, USB_BLOCKSIZE);
        memset(endpoint->transferArray[4].destinationBuffer, 0x66, USB_BLOCKSIZE);
        endpoint->transferArray[3].status = endpoint->transferArray[4].status =
            NGENE_USB_TRANSFER_COMPLETED;
        CopySamplesIntoUsbIntermediateBuffer(&frontend);
        if (ReadUsbInterface(&frontend, wrapped, sizeof(wrapped)) != sizeof(wrapped))
            return 1;
        for (index = 0; index < (int)sizeof(wrapped); index++)
            if (wrapped[index] != (index < USB_BLOCKSIZE ? 0x44 : 0x66))
                return 1;
    }
    if (StopUsbInterface(&frontend) != SUCCESS_RETURN)
        return 1;
    if (startCommands != 1 || stopCommands != 2)
        return 1;
    DeleteEndpointHandle(endpoint);
    free(usb->usbBuffer);
    free(usb->usbIntermediateBuffer);
    pthread_cond_destroy(&usb->intermediateBufferFull);
    pthread_cond_destroy(&usb->intermediateBufferEmpty);
    pthread_mutex_destroy(&usb->frontEndMutex);
    puts(
        "USB producer isolation, backpressure shutdown, empty/partial reads, ordered completions, queue wrap and timeout restart passed.");
    return 0;
}
