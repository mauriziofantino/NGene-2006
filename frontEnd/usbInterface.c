/**
 * @file usbInterface.c
 *
 * @author Mario Nicola
 *
 * @date 22/09/2014
 *
 * @version 1.0.1
 *
 * @brief From a previous implementation by Matteo Sticco
 *
 */

#include <stdlib.h>
#include <string.h>
#include <libusb-1.0/libusb.h>
#include <pthread.h>
#include "simd.h"
#include "frontend.h"
#include "ioFunctions.h"
#include "closeManager.h"
#include "ngeneConstants.h"
#include "arrays.h"
#include "usbDevicesConstants.h"
#include "usbInterface.h"
#include "signalHandler.h"
#include <time.h>

#ifdef NGENE_USB_TEST_IO
int LIBUSB_CALL __wrap_libusb_handle_events_timeout(libusb_context *, struct timeval *);
int LIBUSB_CALL __wrap_libusb_submit_transfer(struct libusb_transfer *);
int LIBUSB_CALL __wrap_libusb_control_transfer(libusb_device_handle *,uint8_t,uint8_t,uint16_t,uint16_t,unsigned char *,uint16_t,unsigned int);
int LIBUSB_CALL __wrap_libusb_cancel_transfer(struct libusb_transfer *);
#define libusb_handle_events_timeout __wrap_libusb_handle_events_timeout
#define libusb_submit_transfer __wrap_libusb_submit_transfer
#define libusb_control_transfer __wrap_libusb_control_transfer
#define libusb_cancel_transfer __wrap_libusb_cancel_transfer
#endif

static void RecordUsbServiceGap(double *lastTime, double *maximumGap)
{
    struct timespec now;
    double seconds;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return;
    seconds = now.tv_sec + now.tv_nsec * 1e-9;
    if (*lastTime > 0.0 && seconds - *lastTime > *maximumGap) {
        *maximumGap = seconds - *lastTime;
    }
    *lastTime = seconds;
}

/** @brief Recheck stop requests periodically while preserving the buffer mutex. */
static void WaitForUsbBuffer(pthread_cond_t *condition, pthread_mutex_t *mutex)
{
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_nsec += 100000000;
    if (deadline.tv_nsec >= 1000000000) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000;
    }
    pthread_cond_timedwait(condition, mutex, &deadline);
}


/* Global variable */
extern int USB_BLOCKSIZE; /**< Defined in manageUsbFrontEndConfiguration.c */

/* internal functions */
void FindUsbDevice(unsigned short vid, unsigned short pid, libusb_device **ppUsbDev)
{
    libusb_device **list;
    libusb_device *dev = NULL;
    struct libusb_device_descriptor desc;
    ssize_t cnt, i;
    int ret;

    if (ppUsbDev == NULL) {
	return;
    }
    (*ppUsbDev) = NULL;

    cnt = libusb_get_device_list(NULL, &list);
    if (cnt < 0) {
	return;
    }

    for (i = 0; i < cnt; i++) {
	dev = list[i];
	ret = libusb_get_device_descriptor(dev, &desc);
	if (ret < 0) {
	    continue;
	}
	if ((desc.idVendor == vid) && (desc.idProduct == pid)) {
	    (*ppUsbDev) = libusb_ref_device(dev);
	    break;
	}
    }
    libusb_free_device_list(list, 1);
}

void OpenUsbDevice(TUsbFe *usbFe, libusb_device *pUsbDev, libusb_device_handle **ppUDH)
{
    int returnValue;

    /* open device handle */
    returnValue = libusb_open(pUsbDev, ppUDH);
    if (returnValue < 0) {
	SwrxLog(NGENE_LOG_SEVERITY_ERROR, "Error in libusb_open (%d)", returnValue);
	return;
    }
    /** Query first: WinUSB cannot switch to a different configuration. */
    int currentConfiguration;
    returnValue = libusb_get_configuration(*ppUDH, &currentConfiguration);
    if (returnValue < 0) {
        libusb_close(*ppUDH);
        *ppUDH = NULL;
        return;
    }
    /* set configuration */
    if (currentConfiguration != usbFe->configuration)
    {
	returnValue = libusb_set_configuration((*ppUDH), usbFe->configuration);
	if (returnValue < 0) {
	    SwrxLog(NGENE_LOG_SEVERITY_ERROR, "Error in libusb_set_configuration (%d)", returnValue);
	    libusb_close(*ppUDH);
	    (*ppUDH) = NULL;
	    return;
	}
    }
    /* claim interface */
    returnValue = libusb_claim_interface((*ppUDH), usbFe->interface);
    if (returnValue < 0) {
	SwrxLog(NGENE_LOG_SEVERITY_ERROR, "Error in libusb_claim_interface (%d)", returnValue);
	libusb_close(*ppUDH);
	(*ppUDH) = NULL;
	return;
    }
    /* set alternate interface */
    {
	returnValue = libusb_set_interface_alt_setting(*ppUDH, usbFe->interface, usbFe->altInterface);
	if (returnValue < 0) {
	    SwrxLog(NGENE_LOG_SEVERITY_ERROR, "Error in libusb_set_interface_alt_setting (%d)", returnValue);
	    libusb_release_interface(*ppUDH, usbFe->interface);
	    libusb_close(*ppUDH);
	    (*ppUDH) = NULL;
	    return;
	}
    }
#if defined(NGENE_NATIVE_WINDOWS) && LIBUSB_API_VERSION >= 0x0100010C
    /* FX2 FIFO streaming requires reads queued directly to the Windows driver. */
    returnValue = libusb_endpoint_supports_raw_io(*ppUDH, usbFe->endpoint);
    if (returnValue == 1) {
        int maximumSize;
        int packetSize = libusb_get_max_packet_size(pUsbDev, usbFe->endpoint);
        maximumSize = libusb_get_max_raw_io_transfer_size(*ppUDH, usbFe->endpoint);
        if (maximumSize > 0 && packetSize > 0 &&
            USB_BLOCKSIZE % packetSize == 0 && USB_BLOCKSIZE <= maximumSize) {
            returnValue = libusb_endpoint_set_raw_io(*ppUDH, usbFe->endpoint, 1);
        } else {
            returnValue = maximumSize < 0 ? maximumSize :
                          packetSize < 0 ? packetSize : LIBUSB_ERROR_INVALID_PARAM;
        }
        if (returnValue == 0) {
            SwrxLog(NGENE_LOG_SEVERITY_INFO, "USB WinUSB RAW_IO enabled on endpoint 0x%02x",
                    usbFe->endpoint);
        }
    }
    if (returnValue < 0) {
        SwrxLog(NGENE_LOG_SEVERITY_ERROR, "USB RAW_IO setup: %s (%d)",
                libusb_error_name(returnValue), returnValue);
        libusb_release_interface(*ppUDH, usbFe->interface);
        libusb_close(*ppUDH);
        *ppUDH = NULL;
        return;
    }
#endif
}

void CloseUsbDevice(TUsbFe *usbFe, libusb_device_handle *pUDH)
{
    if (pUDH == NULL) {
	return;
    }

    libusb_release_interface(pUDH, usbFe->interface);
    libusb_close(pUDH);
}


void LIBUSB_CALL UsbReadCallback(struct libusb_transfer *transfer)
{
    TUsbTransferElement *pTransferElem = (TUsbTransferElement *) (transfer->user_data);

    switch (transfer->status) {
    case LIBUSB_TRANSFER_COMPLETED:
	/* Mark the transfer as complete */
        if (transfer->actual_length != transfer->length) {
            SwrxLog(NGENE_LOG_SEVERITY_ERROR,
                    "USB short transfer: received %d bytes, requested %d bytes",
                    transfer->actual_length, transfer->length);
        }
        /** The circular buffer consumes complete blocks, never stale tails. */
        pTransferElem->status = transfer->actual_length == transfer->length
            ? NGENE_USB_TRANSFER_COMPLETED : NGENE_USB_OTHER_ERRORS;
	break;
    case LIBUSB_TRANSFER_TIMED_OUT:
	pTransferElem->status = NGENE_USB_TRANSFER_TIMED_OUT;
	break;
    case LIBUSB_TRANSFER_OVERFLOW:
	pTransferElem->status = NGENE_USB_TRANSFER_OVERFLOW;
	break;
    case LIBUSB_TRANSFER_ERROR:
	pTransferElem->status = NGENE_USB_TRANSFER_ERROR;
	break;
    case LIBUSB_TRANSFER_CANCELLED:
	pTransferElem->status = NGENE_USB_TRANSFER_CANCELLED;
	break;
    case LIBUSB_TRANSFER_STALL:
	pTransferElem->status = NGENE_USB_TRANSFER_STALL;
	break;
    case LIBUSB_TRANSFER_NO_DEVICE:
        pTransferElem->status = NGENE_USB_TRANSFER_NO_DEVICE;
        break;
    default:
        SwrxLog(NGENE_LOG_SEVERITY_ERROR, "USB callback status: %d", transfer->status);
	pTransferElem->status = NGENE_USB_OTHER_ERRORS;
	break;
    }

}


TUsbEndpointHandle *MakeEndpointHandle(TUsbFe *usbFe, libusb_device_handle *usbDevHandle)
{
    TUsbEndpointHandle *pTmpEpHandle;
    uintptr_t blkIdx;

    /* Sanity checks */
    if (!(usbFe->endpoint & 0x80)) {
	SwrxLog(NGENE_LOG_SEVERITY_ERROR, "Invalid endpoint in MakeEndpointHandle (%x)", usbFe->endpoint);
	return NULL;
    }
    /* Alloc the endpoint handler signal structure */
    pTmpEpHandle = (TUsbEndpointHandle *) calloc(1, sizeof(TUsbEndpointHandle));
    if (NULL == pTmpEpHandle) {
	SwrxLog(NGENE_LOG_SEVERITY_ERROR, "Error from malloc in MakeEndpointHandle");
	return NULL;
    }
    /* Save the USB device handler in the endpoint handler */
    pTmpEpHandle->usbParentDeviceHandler = usbDevHandle;
    /* Save the USB device handler in the endpoint handler */
    pTmpEpHandle->endpoint = usbFe->endpoint;
    /* Transfers array initialization elem by elem */
    for (blkIdx = 0; blkIdx < USB_BUFFER_SIZE; blkIdx++) {
	(pTmpEpHandle->transferArray[blkIdx]).destinationBuffer = &(usbFe->usbBuffer[blkIdx * USB_BLOCKSIZE]);
	(pTmpEpHandle->transferArray[blkIdx]).transfer = libusb_alloc_transfer(0);
	if ((pTmpEpHandle->transferArray[blkIdx]).transfer == NULL) {
	    SwrxLog(NGENE_LOG_SEVERITY_ERROR, "Error from libusb_alloc_transfer in MakeEndpointHandle");
            while (blkIdx > 0) {
                libusb_free_transfer(pTmpEpHandle->transferArray[--blkIdx].transfer);
            }
            free(pTmpEpHandle);
	    return NULL;
	}
	/* Fill the bulk request */
	libusb_fill_bulk_transfer((pTmpEpHandle->transferArray[blkIdx]).transfer, usbDevHandle, usbFe->endpoint, (unsigned char *) ((pTmpEpHandle->transferArray[blkIdx]).destinationBuffer), USB_BLOCKSIZE, UsbReadCallback, (void *) &(pTmpEpHandle->transferArray[blkIdx]), USB_BULK_TRANSFER_TIMEOUT);

	/* Initialize the status at OFF */
	(pTmpEpHandle->transferArray[blkIdx]).status = NGENE_USB_TRANSFER_OFF;
    }

    /*! Not started yet */
    pTmpEpHandle->flagStarted = FALSE_FLAG;

    return pTmpEpHandle;
}


void DeleteEndpointHandle(TUsbEndpointHandle *eph)
{
    int blkIdx;

    if (NULL == eph) {
	/* Error! The EP handler is null! */
	return;
    }

    if (eph->flagStarted == TRUE_FLAG) {
	/* Error! The EP handler looks busy! */
	return;
    }
    for (blkIdx = 0; blkIdx < USB_BUFFER_SIZE; blkIdx++) {
	if (NULL != (eph->transferArray[blkIdx]).transfer) {
	    libusb_free_transfer((eph->transferArray[blkIdx]).transfer);
	}
    }
    free(eph);
}


/* interface functions */


int OpenUsbFrontend(TNGeneConfiguration *cfg, TRxFe *rxFe)
{
    TUsbFe *usbFe = &(rxFe->usbFe);
    libusb_device *pUsbDevice = NULL;
    libusb_device_handle *pUsbDevHandle = NULL;

    usbFe->index = cfg->fePars->index;
    usbFe->vid = cfg->fePars->vid;
    usbFe->pid = cfg->fePars->pid;
    if (cfg->usedFrontEnd != NGENE_FE_SIGE && cfg->usedFrontEnd != NGENE_FE_SIGE_V3) {
        return FAILURE_RETURN;
    }
    usbFe->configuration = SIGE_CONFIG;
    usbFe->interface = SIGE_INTF;
    usbFe->altInterface = SIGE_ALTINTF;
    usbFe->endpoint = SIGE_RX_EP;
    usbFe->vrqtVendorOut = SIGE_VRQT_VENDOR_OUT;
    usbFe->vrqStartXfer = SIGE_VRQ_START_XFER;
    usbFe->vrqStopXfer = SIGE_VRQ_STOP_XFER;
    usbFe->wValue = SIGE_WVALUE;

    if (posix_memalign((void **) &(usbFe->usbBuffer), POSIX_ALIGNMENT, USB_BUFFER_SIZE * USB_BLOCKSIZE) != 0) {
        return FAILURE_RETURN;
    }
    if (usbFe->usbBuffer == NULL) {
	SwrxLog(NGENE_LOG_SEVERITY_ERROR, "Malloc returned a NULL pointer in OpenUsbFrontend");
	CloseNGene(cfg, EXIT_FAILURE);
    }
    usbFe->usbBufferIndex = 0;
    if (posix_memalign((void **) &(usbFe->usbIntermediateBuffer), POSIX_ALIGNMENT, USB_INTERMEDIATE_BUFFER_SIZE * USB_BLOCKSIZE) != 0) {
        goto allocation_failure;
    }
    if (usbFe->usbIntermediateBuffer == NULL) {
	SwrxLog(NGENE_LOG_SEVERITY_ERROR, "Malloc returned a NULL pointer in OpenUsbFrontend");
	CloseNGene(cfg, EXIT_FAILURE);
    }
    usbFe->usbIntermediateBufferWriteIndex = 0;
    usbFe->usbIntermediateBufferReadIndex = 0;
    usbFe->usbIntermediateBufferSampleIndex = 0;
    if (libusb_init(NULL) < 0) {
        goto allocation_failure;
    }
    /* Search for the selected USB device */
    FindUsbDevice(usbFe->vid, usbFe->pid, &pUsbDevice);
    if (pUsbDevice == NULL) {
	SwrxLog(NGENE_LOG_SEVERITY_ERROR, "Cannot find selected USB front-end (VID/PID %04x/%04x)", usbFe->vid, usbFe->pid);
        goto usb_failure;
    }
    /* Now create a handle for the device, and selects interface and alternative configuration */
    OpenUsbDevice(usbFe, pUsbDevice, &pUsbDevHandle);
    libusb_unref_device(pUsbDevice);
    if (pUsbDevHandle == NULL) {
	SwrxLog(NGENE_LOG_SEVERITY_ERROR, "Cannot create a handle for the selected USB front-end (VID/PID %04x/%04x)", usbFe->vid, usbFe->pid);
        goto usb_failure;
    }
    /* Allocate the endpoint handler. This will be shared between the library */
    usbFe->epHandle = MakeEndpointHandle(usbFe, pUsbDevHandle);
    if (usbFe->epHandle == NULL) {
	SwrxLog(NGENE_LOG_SEVERITY_ERROR, "Cannot initialize asynchronous transfers for the selected USB front-end (VID/PID %04x/%04x)", usbFe->vid, usbFe->pid);
        CloseUsbDevice(usbFe, pUsbDevHandle);
        goto usb_failure;
    }
    usbFe->usbStatus = NGENE_USB_OK;
    usbFe->usbSamplesRead = FALSE_FLAG;
    usbFe->usbStreamGeneration = usbFe->usbReadGeneration = 0;
    usbFe->usbLastProducerTime = usbFe->usbLastConsumerTime = 0.0;
    usbFe->usbMaxProducerGap = usbFe->usbMaxConsumerGap = 0.0;
    return SUCCESS_RETURN;

usb_failure:
    libusb_exit(NULL);
allocation_failure:
    free(usbFe->usbIntermediateBuffer);
    free(usbFe->usbBuffer);
    usbFe->usbIntermediateBuffer = NULL;
    usbFe->usbBuffer = NULL;
    return FAILURE_RETURN;
}


int CloseUsbFrontend(TRxFe *rxFe)
{
    TUsbFe *usbFe = &(rxFe->usbFe);
    libusb_device_handle *pUsbDevHandle;

    if (usbFe->epHandle == NULL) {
        return SUCCESS_RETURN;
    }
    if (StopUsbInterface(rxFe) != SUCCESS_RETURN) {
        return FAILURE_RETURN;
    }
    pUsbDevHandle = usbFe->epHandle->usbParentDeviceHandler;

    CloseUsbDevice(usbFe, pUsbDevHandle);
    DeleteEndpointHandle(usbFe->epHandle);
    usbFe->epHandle = NULL;
    free(usbFe->usbBuffer);
    free(usbFe->usbIntermediateBuffer);
    usbFe->usbBuffer = NULL;
    usbFe->usbIntermediateBuffer = NULL;
    libusb_exit(NULL);

    return SUCCESS_RETURN;
}


int StartUsbInterface(TRxFe *rxFe)
{
    TUsbFe *usbFe = &(rxFe->usbFe);
    TUsbEndpointHandle *epHandle = usbFe->epHandle;
    int ret, blkIdx;
    libusb_device_handle *pUsbDevHandle;

    /*! Check if the EP handler is valid */
    if (epHandle == NULL) {
	return FAILURE_RETURN;
    }

    /* Get the USB device handler */
    pUsbDevHandle = epHandle->usbParentDeviceHandler;
    if (NULL == pUsbDevHandle) {
	return FAILURE_RETURN;
    }

    /** Every new batch starts at transfer zero, including after a timeout. */
    usbFe->usbBufferIndex = 0;
    /** Track partially submitted batches so failures can drain them safely. */
    usbFe->usbBufferFullWaits = 0;
    usbFe->usbStreamGeneration++;
    epHandle->flagStarted = TRUE_FLAG;
    /* Send out all the bulk transfers on the USB bus */
    for (blkIdx = 0; blkIdx < USB_BUFFER_SIZE; blkIdx++) {
	ret = libusb_submit_transfer((epHandle->transferArray[blkIdx]).transfer);
	if (ret < 0) {
	    SwrxLog(NGENE_LOG_SEVERITY_ERROR, "libusb_submit_transfer error (%d) in StartUsbInterface", ret);
            StopUsbInterface(rxFe);
	    return FAILURE_RETURN;
	}
	(epHandle->transferArray[blkIdx]).status = NGENE_USB_TRANSFER_PENDING;
    }

    /* Send a "start transfer" message to the Cypress GPIF */
    {
	ret = libusb_control_transfer(pUsbDevHandle,	/* dev_handle */
				      usbFe->vrqtVendorOut,	/* request_type */
				      usbFe->vrqStartXfer,	/* bRequest */
				      usbFe->wValue,	/* wValue */
				      0,	/* wIndex */
				      NULL,	/* data */
				      0,	/* wLength */
				      USB_CONTROL_TRANSFER_TIMEOUT);	/* timeout */
	if (ret < 0) {
	    SwrxLog(NGENE_LOG_SEVERITY_ERROR, "libusb_control_transfer error (%d) in StartUsbInterface", ret);
            StopUsbInterface(rxFe);
	    return FAILURE_RETURN;
	}
    }
    /* Set the EP handler flag to "started" */
    epHandle->flagStarted = TRUE_FLAG;

    return SUCCESS_RETURN;
}


int StopUsbInterface(TRxFe *rxFe)
{
    TUsbFe *usbFe = &(rxFe->usbFe);
    TUsbEndpointHandle *epHandle = usbFe->epHandle;
    int blkIdx;
    int pending;
    int ret;
    struct timeval timeout = {0, 100000};
    libusb_device_handle *pUsbDevHandle;

    if (NULL == epHandle) {
	return FAILURE_RETURN;
    }
    if (epHandle->flagStarted == FALSE_FLAG) {
	return SUCCESS_RETURN;
    }
    pUsbDevHandle = epHandle->usbParentDeviceHandler;
    if (NULL == pUsbDevHandle) {
	return FAILURE_RETURN;
    }

    /* Start and stop share request 0x01; wValue selects the operation. */
    ret = libusb_control_transfer(pUsbDevHandle,	/* dev_handle */
			    usbFe->vrqtVendorOut,	/* request_type */
			    usbFe->vrqStopXfer,	/* bRequest */
			    SIGE_STOP_WVALUE,	/* wValue */
			    0,	/* wIndex */
			    NULL,	/* data */
			    0,	/* wLength */
			    USB_CONTROL_TRANSFER_TIMEOUT);	/* timeout */
    if (ret < 0 && ret != LIBUSB_ERROR_NO_DEVICE) {
        SwrxLog(NGENE_LOG_SEVERITY_WARNING, "USB stop command: %s (%d)",
                libusb_error_name(ret), ret);
    }

    /** Cancellation is asynchronous; retain buffers until every callback runs. */
    for (blkIdx = 0; blkIdx < USB_BUFFER_SIZE; blkIdx++) {
        if (epHandle->transferArray[blkIdx].status == NGENE_USB_TRANSFER_PENDING) {
            libusb_cancel_transfer(epHandle->transferArray[blkIdx].transfer);
        }
    }
    do {
        pending = FALSE_FLAG;
        for (blkIdx = 0; blkIdx < USB_BUFFER_SIZE; blkIdx++) {
            if (epHandle->transferArray[blkIdx].status == NGENE_USB_TRANSFER_PENDING) {
                pending = TRUE_FLAG;
            }
        }
        if (pending) {
            ret = libusb_handle_events_timeout(NULL, &timeout);
            if (ret < 0 && ret != LIBUSB_ERROR_INTERRUPTED) {
                SwrxLog(NGENE_LOG_SEVERITY_ERROR, "USB shutdown: %s", libusb_error_name(ret));
                return FAILURE_RETURN;
            }
        }
    } while (pending);
    for (blkIdx = 0; blkIdx < USB_BUFFER_SIZE; blkIdx++) {
        epHandle->transferArray[blkIdx].status = NGENE_USB_TRANSFER_OFF;
    }

    /* Declare the endpoint handle stopped */
    epHandle->flagStarted = FALSE_FLAG;

    return SUCCESS_RETURN;
}


void CopySamplesIntoUsbIntermediateBuffer(TRxFe *rxFe)
{
    TUsbFe *usbFe = &(rxFe->usbFe);
    TUsbEndpointHandle *epHandle = usbFe->epHandle;
    TUsbTransferElement *currTransfer;
    int idx = 0;
    char *srcPtr;
    char *dstPtr;
    int samplesWritten = FALSE_FLAG;
    int eventResult;
    struct timeval eventTimeout = {0, 100000};

    /* A previous event dispatch may already have completed the next block.
     * Never wait for a new event before recycling those completed reads. */
    if (epHandle->transferArray[usbFe->usbBufferIndex].status != NGENE_USB_TRANSFER_PENDING) {
        eventTimeout.tv_usec = 0;
    }
    eventResult = libusb_handle_events_timeout(NULL, &eventTimeout);
    if (eventResult < 0 && eventResult != LIBUSB_ERROR_INTERRUPTED) {
        SwrxLog(NGENE_LOG_SEVERITY_ERROR, "USB event handling: %s (%d)",
                libusb_error_name(eventResult), eventResult);
        usbFe->usbStatus = NGENE_USB_OTHER_ERRORS;
        return;
    }

    pthread_mutex_lock(&usbFe->frontEndMutex);
    RecordUsbServiceGap(&usbFe->usbLastProducerTime, &usbFe->usbMaxProducerGap);
    if (ngeneStopRequested || usbFe->shutdownRequested) {
        pthread_mutex_unlock(&usbFe->frontEndMutex);
        return;
    }
    currTransfer = &(epHandle->transferArray[usbFe->usbBufferIndex]);
    while ((currTransfer->status == NGENE_USB_TRANSFER_COMPLETED) && (idx < USB_BUFFER_SIZE)) {
	/* A wakeup alone does not mean the consumer freed a complete block. */
        while (!ngeneStopRequested && !usbFe->shutdownRequested && usbFe->flagBufferFull) {
            if (usbFe->usbBufferFullWaits++ == 0) {
                SwrxLog(NGENE_LOG_SEVERITY_WARNING,
                        "USB sample buffer full (%u bytes): receiver processing is behind; stream continuity at risk",
                        (unsigned int)(USB_INTERMEDIATE_BUFFER_SIZE * USB_BLOCKSIZE));
            }
            WaitForUsbBuffer(&usbFe->intermediateBufferFull, &usbFe->frontEndMutex);
            /* Keep dispatching completions and errors even under backpressure.
             * A zero timeout avoids delaying a consumer that just freed space. */
            pthread_mutex_unlock(&usbFe->frontEndMutex);
            eventTimeout.tv_sec = 0;
            eventTimeout.tv_usec = 0;
            eventResult = libusb_handle_events_timeout(NULL, &eventTimeout);
            pthread_mutex_lock(&usbFe->frontEndMutex);
            if (eventResult < 0 && eventResult != LIBUSB_ERROR_INTERRUPTED) {
                usbFe->usbStatus = NGENE_USB_OTHER_ERRORS;
                pthread_mutex_unlock(&usbFe->frontEndMutex);
                return;
            }
        }
        if (ngeneStopRequested || usbFe->shutdownRequested) {
            pthread_mutex_unlock(&usbFe->frontEndMutex);
            return;
        }
	/* copy samples */
	srcPtr = currTransfer->destinationBuffer;
	dstPtr = &(usbFe->usbIntermediateBuffer[usbFe->usbIntermediateBufferWriteIndex * USB_BLOCKSIZE]);
        /* The legacy SIMD copy uses the receiver's global XMM backup. It must
         * never run concurrently in the USB producer and the receiver thread. */
        memcpy(dstPtr, srcPtr, (size_t)USB_BLOCKSIZE);
	/* new transfer */
	if (libusb_submit_transfer(currTransfer->transfer) < 0) {
	    usbFe->usbStatus = NGENE_USB_SUBMIT_TRANSFER_ERROR;
            currTransfer->status = NGENE_USB_SUBMIT_TRANSFER_ERROR;
        } else {
            currTransfer->status = NGENE_USB_TRANSFER_PENDING;
        }
	/* update indexes */
	idx++;
	usbFe->usbBufferIndex = (usbFe->usbBufferIndex + 1) % USB_BUFFER_SIZE;
	currTransfer = &(epHandle->transferArray[usbFe->usbBufferIndex]);
	usbFe->usbIntermediateBufferWriteIndex = (usbFe->usbIntermediateBufferWriteIndex + 1) % USB_INTERMEDIATE_BUFFER_SIZE;
	samplesWritten = TRUE_FLAG;
	usbFe->usbSamplesRead = TRUE_FLAG;
	//
	if (usbFe->usbIntermediateBufferWriteIndex == usbFe->usbIntermediateBufferReadIndex) {
	    /* intermediate buffer is full */
	    // do not stop NGene2 when the intermediate buffer is full
	    // usbFe->usbStatus = NGENE_USB_INTERMEDIATE_BUFFER_FULL;
	    usbFe->flagBufferFull = TRUE_FLAG;
	    pthread_cond_signal(&rxFe->usbFe.intermediateBufferEmpty);
	}
    }
    if (ngeneStopRequested || usbFe->shutdownRequested) {
        pthread_mutex_unlock(&usbFe->frontEndMutex);
        return;
    }
    /* verify errors */
    if ((currTransfer->status == NGENE_USB_TRANSFER_TIMED_OUT) && (usbFe->usbSamplesRead == TRUE_FLAG)) {
	// if timeout and some samples have been read, try to restart
	// if no samples have been read, better to stop and try to change the USB_BLOCKSIZE
	SwrxLog(NGENE_LOG_SEVERITY_ERROR,
                "LIBUSB_TRANSFER_TIMED_OUT: block %d, %d/%d bytes, buffer waits %u; restarting USB (sample discontinuity)",
                usbFe->usbBufferIndex, currTransfer->transfer->actual_length,
                currTransfer->transfer->length, usbFe->usbBufferFullWaits);
        SwrxLog(NGENE_LOG_SEVERITY_WARNING,
                "USB maximum service gaps since open (including waits): producer %.3f ms, receiver %.3f ms",
                1000.0 * usbFe->usbMaxProducerGap, 1000.0 * usbFe->usbMaxConsumerGap);
        if (StopUsbInterface(rxFe) != SUCCESS_RETURN ||
            (!ngeneStopRequested && StartUsbInterface(rxFe) != SUCCESS_RETURN)) {
            usbFe->usbStatus = NGENE_USB_OTHER_ERRORS;
        } else {
            usbFe->usbIntermediateBufferWriteIndex = 0;
            usbFe->usbIntermediateBufferReadIndex = 0;
            usbFe->usbIntermediateBufferSampleIndex = 0;
        }
	usbFe->flagBufferFull = FALSE_FLAG;
    } else if ((currTransfer->status != NGENE_USB_TRANSFER_COMPLETED) && (currTransfer->status != NGENE_USB_TRANSFER_PENDING) && (currTransfer->status != NGENE_USB_TRANSFER_OFF)) {
	// if other errors, stop
	usbFe->usbStatus = currTransfer->status;
    }

    /* if samples have been copied, signal to the main thread (ReadUsbInterface) */
    if (samplesWritten == TRUE_FLAG) {
	pthread_cond_signal(&usbFe->intermediateBufferEmpty);
    }
    pthread_mutex_unlock(&usbFe->frontEndMutex);
}


int ReadUsbInterface(TRxFe *rxFe, char *buffer, int dim)
{
    TUsbFe *usbFe = &(rxFe->usbFe);
    unsigned int availableSamples = 0;
    unsigned int nSamples = 0;
    unsigned int availableSamples1 = 0;
    unsigned int availableSamples2 = 0;
    unsigned int nSamples1 = 0;
    unsigned int nSamples2 = 0;
    char *srcPtr;

    pthread_mutex_lock(&rxFe->usbFe.frontEndMutex);
    RecordUsbServiceGap(&usbFe->usbLastConsumerTime, &usbFe->usbMaxConsumerGap);

    /* Verify the presence of some samples */
    if (!ngeneStopRequested && !usbFe->shutdownRequested && (usbFe->usbIntermediateBufferWriteIndex == usbFe->usbIntermediateBufferReadIndex) && (usbFe->flagBufferFull == FALSE_FLAG)) {
	/* If the intermediate buffer is empty, wait */
        WaitForUsbBuffer(&rxFe->usbFe.intermediateBufferEmpty, &rxFe->usbFe.frontEndMutex);
    }

    /* Check again if samples are available, to avoid errors in the circular buffer */
    if ((usbFe->usbIntermediateBufferWriteIndex == usbFe->usbIntermediateBufferReadIndex) && (usbFe->flagBufferFull == FALSE_FLAG)) {
	pthread_mutex_unlock(&rxFe->usbFe.frontEndMutex);
	// still empty! zero data read
	return 0;
    }

    /* Associate the delivered bytes with their stream, so a native I/Q
     * decoder never joins a pending component to a restarted USB stream. */
    usbFe->usbReadGeneration = usbFe->usbStreamGeneration;
    /* Now some samples are available */
    srcPtr = &(usbFe->usbIntermediateBuffer[(usbFe->usbIntermediateBufferReadIndex * USB_BLOCKSIZE) + usbFe->usbIntermediateBufferSampleIndex]);
    if (usbFe->usbIntermediateBufferWriteIndex > usbFe->usbIntermediateBufferReadIndex) {
	/* samples in intermediate buffer in one chunk */
	availableSamples = (usbFe->usbIntermediateBufferWriteIndex - usbFe->usbIntermediateBufferReadIndex) * USB_BLOCKSIZE;
	availableSamples -= usbFe->usbIntermediateBufferSampleIndex;
	nSamples = (availableSamples < (unsigned int) dim) ? availableSamples : (unsigned int) dim;
	ArrayCopy(srcPtr, buffer, nSamples);
    } else {
	/* samples in intermediate buffer in two chunks */
	availableSamples1 = (USB_INTERMEDIATE_BUFFER_SIZE - usbFe->usbIntermediateBufferReadIndex) * USB_BLOCKSIZE;
	availableSamples1 -= usbFe->usbIntermediateBufferSampleIndex;
	availableSamples2 = usbFe->usbIntermediateBufferWriteIndex * USB_BLOCKSIZE;
	if (availableSamples1 >= (unsigned int) dim) {
	    nSamples1 = dim;
	    nSamples2 = 0;
	} else if ((availableSamples1 + availableSamples2) >= (unsigned int) dim) {
	    nSamples1 = availableSamples1;
	    nSamples2 = dim - availableSamples1;
	} else {
	    nSamples1 = availableSamples1;
	    nSamples2 = availableSamples2;
	}
	ArrayCopy(srcPtr, buffer, nSamples1);
	ArrayCopy(usbFe->usbIntermediateBuffer, &(buffer[nSamples1]), nSamples2);
	nSamples = nSamples1 + nSamples2;
    }
    /* update read index */
    usbFe->usbIntermediateBufferSampleIndex += nSamples;
    usbFe->usbIntermediateBufferReadIndex += (usbFe->usbIntermediateBufferSampleIndex / USB_BLOCKSIZE);
    usbFe->usbIntermediateBufferReadIndex = usbFe->usbIntermediateBufferReadIndex % USB_INTERMEDIATE_BUFFER_SIZE;
    usbFe->usbIntermediateBufferSampleIndex = usbFe->usbIntermediateBufferSampleIndex % USB_BLOCKSIZE;
    if ((usbFe->flagBufferFull == TRUE_FLAG) && (usbFe->usbIntermediateBufferReadIndex != usbFe->usbIntermediateBufferWriteIndex)) {
	// now we are sure that the buffer is not full
	usbFe->flagBufferFull = FALSE_FLAG;
	// send the signal only when the buffer was full and some space is now available in the intermediate buffer
	pthread_cond_signal(&rxFe->usbFe.intermediateBufferFull);
    }
    pthread_mutex_unlock(&rxFe->usbFe.frontEndMutex);
    return nSamples;
}


int UsbCheckOverrun(TRxFe *rxFe)
{
    if (rxFe->usbFe.usbStatus == NGENE_USB_OK) {
	return SUCCESS_RETURN;
    }
    /* an error occurred: prints a message in function of the type of error */
    switch (rxFe->usbFe.usbStatus) {
    case NGENE_USB_TRANSFER_ERROR:
	SwrxLog(NGENE_LOG_SEVERITY_ERROR, "libusb error: LIBUSB_TRANSFER_ERROR");
	break;
    case NGENE_USB_TRANSFER_TIMED_OUT:
	SwrxLog(NGENE_LOG_SEVERITY_ERROR, "libusb error: LIBUSB_TRANSFER_TIMED_OUT");
	break;
    case NGENE_USB_TRANSFER_CANCELLED:
	SwrxLog(NGENE_LOG_SEVERITY_ERROR, "libusb error: LIBUSB_TRANSFER_CANCELLED");
	break;
    case NGENE_USB_TRANSFER_STALL:
	SwrxLog(NGENE_LOG_SEVERITY_ERROR, "libusb error: LIBUSB_TRANSFER_STALL");
	break;
    case NGENE_USB_TRANSFER_NO_DEVICE:
	SwrxLog(NGENE_LOG_SEVERITY_ERROR, "libusb error: LIBUSB_TRANSFER_NO_DEVICE");
	break;
    case NGENE_USB_TRANSFER_OVERFLOW:
	SwrxLog(NGENE_LOG_SEVERITY_ERROR, "libusb error: LIBUSB_TRANSFER_OVERFLOW");
	break;
    case NGENE_USB_INTERMEDIATE_BUFFER_FULL:
	SwrxLog(NGENE_LOG_SEVERITY_ERROR, "USB interface error: intermediate buffer full");
	break;
    case NGENE_USB_SUBMIT_TRANSFER_ERROR:
	SwrxLog(NGENE_LOG_SEVERITY_ERROR, "USB interface error: libusb_submit_transfer returns error");
	break;
    default:
	SwrxLog(NGENE_LOG_SEVERITY_ERROR, "USB interface error: libusb other error");
	break;
    }
    return FAILURE_RETURN;
}
