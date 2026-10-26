/* Native libhackrf RX. The callback only copies borrowed CI8 bytes; all sample
 * conversion and receiver SIMD remain on the receiver thread. */
#include <hackrf.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <ctype.h>
#include <limits.h>
#include "hackrfFrontend.h"
#include "ioFunctions.h"
#include "signalHandler.h"

/* Four upstream 262144-byte transfers, about 64 ms at 8.1838 M complex/s.
 * Stop on overflow, never block USB behind processing or silently drop data. */
#define HACKRF_QUEUE_BYTES (4u * 262144u)

struct HackrfFrontend {
    hackrf_device *device;
    unsigned char *queue;
    unsigned int head, tail, count, highWater;
    int initialized, started, failed, reported, antennaPower;
};

static void WaitBriefly(pthread_cond_t *condition, pthread_mutex_t *mutex)
{
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_nsec += 20000000;
    if (deadline.tv_nsec >= 1000000000) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000;
    }
    pthread_cond_timedwait(condition, mutex, &deadline);
}

static int OnHackrfSamples(hackrf_transfer *transfer)
{
    TRxFe *fe = transfer->rx_ctx;
    struct HackrfFrontend *h = fe->hackrf;
    TUsbFe *usb = &fe->usbFe;
    unsigned int n, first;
    int result = 0;
    pthread_mutex_lock(&usb->frontEndMutex);
    if (usb->shutdownRequested || ngeneStopRequested || h->failed) {
        result = -1;
    } else if (transfer->valid_length < 0 ||
               transfer->valid_length > transfer->buffer_length ||
               (transfer->valid_length & 1) ||
               (transfer->valid_length && !transfer->buffer)) {
        h->failed = 1;
        result = -1;
    } else {
        n = (unsigned int)transfer->valid_length;
        if (n > HACKRF_QUEUE_BYTES - h->count) {
            h->failed = 2;
            result = -1;
        } else if (n) {
            first = HACKRF_QUEUE_BYTES - h->head;
            if (first > n) first = n;
            memcpy(h->queue + h->head, transfer->buffer, first);
            memcpy(h->queue, transfer->buffer + first, n - first);
            h->head = (h->head + n) % HACKRF_QUEUE_BYTES;
            h->count += n;
            if (h->count > h->highWater) h->highWater = h->count;
        }
    }
    pthread_cond_broadcast(&usb->intermediateBufferEmpty);
    pthread_mutex_unlock(&usb->frontEndMutex);
    return result;
}

static int VendorResult(int code, const char *operation)
{
    if (code == HACKRF_SUCCESS) return SUCCESS_RETURN;
    SwrxLog(NGENE_LOG_SEVERITY_ERROR, "HackRF %s: %s (%d)", operation,
            hackrf_error_name((enum hackrf_error)code), code);
    return FAILURE_RETURN;
}

/* Local bounded parser: optional settings, exact tags, no commented tag matches
 * and no silently accepted malformed values. Existing receiver parsers stay put. */
static int ReadSettings(const char *filename, int values[6], char *serial, size_t serialSize)
{
    static const char *tags[] = { "LNA gain (dB)", "VGA gain (dB)", "RF amplifier",
                                 "Antenna power", "Baseband bandwidth (Hz)", "Center frequency (Hz)", "Serial number" };
    FILE *file = fopen(filename, "r");
    char line[512], *text, *end;
    unsigned int seen = 0;
    int pending = -1, tag;
    if (!file) {
        if (errno == ENOENT) return SUCCESS_RETURN;
        SwrxLog(NGENE_LOG_SEVERITY_ERROR, "Cannot read %s", filename);
        return FAILURE_RETURN;
    }
    while (fgets(line, sizeof(line), file)) {
        if (!strchr(line, '\n') && !feof(file)) goto invalid;
        text = line;
        while (isspace((unsigned char)*text)) text++;
        if (!*text || *text == '#') continue;
        end = text + strlen(text);
        while (end > text && isspace((unsigned char)end[-1])) *--end = '\0';
        if (pending < 0) {
            size_t length = strlen(text);
            if (length < 3 || text[0] != '[' || text[length-1] != ']') goto invalid;
            text[length-1] = '\0';
            for (tag = 0; tag < 7; tag++) if (!strcmp(text+1, tags[tag])) break;
            if (tag == 7 || (seen & (1u << tag))) goto invalid;
            seen |= 1u << tag;
            pending = tag;
        } else {
            if (pending == 6) {
                size_t k, length = strlen(text);
                if (!length || length > 32 || length >= serialSize) goto invalid;
                for (k = 0; k < length; k++) if (!isxdigit((unsigned char)text[k])) goto invalid;
                memcpy(serial, text, length + 1);
            } else {
                long value;
                errno = 0;
                value = strtol(text, &end, 10);
                if (end == text || *end || errno || value < INT_MIN || value > INT_MAX) goto invalid;
                values[pending] = (int)value;
            }
            pending = -1;
        }
    }
    if (pending >= 0 || ferror(file)) goto invalid;
    fclose(file);
    return SUCCESS_RETURN;
invalid:
    SwrxLog(NGENE_LOG_SEVERITY_ERROR, "Invalid or duplicate tag/value in %s", filename);
    fclose(file);
    return FAILURE_RETURN;
}

#define CONFIGURE(call) do { if (VendorResult((call), #call) != SUCCESS_RETURN) goto failed; } while (0)

int ConfigureHackrfFrontend(TNGeneConfiguration *cfg)
{
    THackrfConfiguration *settings = &cfg->hackrfConfiguration;
    int values[6] = {32, 32, 0, 0, 5000000, 1575458400};
    int lna, vga, amp, antenna, bandwidth;
    char filename[MAX_ROW_LENGTH], serial[MAX_ROW_LENGTH] = "";
    settings->loaded = 0;
    /* fs counts complex pairs. RF centre and signed IF may vary independently
     * of sample rate; derive the NCO reference from the actual hardware tune. */
    if (cfg->fs != 8183800.0 ||
        cfg->fePars->dopplerCorrection != -1 || cfg->fePars->resolution != 8 ||
        cfg->fePars->meanValue != 0) {
        SwrxLog(NGENE_LOG_SEVERITY_ERROR,
                "HackRF profile requires fs=8183800, Doppler correction=-1, signed CI8");
        return FAILURE_RETURN;
    }
    snprintf(filename, sizeof(filename), "%sHackRF.conf", cfg->configurationPath);
    if (ReadSettings(filename, values, serial, sizeof(serial)) != SUCCESS_RETURN) return FAILURE_RETURN;
    lna = values[0]; vga = values[1]; amp = values[2]; antenna = values[3]; bandwidth = values[4];
    if (lna < 0 || lna > 40 || lna % 8 || vga < 0 || vga > 62 || vga % 2 ||
        (amp != 0 && amp != 1) || (antenna != 0 && antenna != 1) || bandwidth <= 0 ||
        bandwidth > cfg->fs || hackrf_compute_baseband_filter_bw(bandwidth) != (unsigned int)bandwidth ||
        fabs(values[5] - 1575420000.0) >= cfg->fs / 2.0) {
        SwrxLog(NGENE_LOG_SEVERITY_ERROR, "Invalid gains, switches or bandwidth in HackRF.conf");
        return FAILURE_RETURN;
    }
    settings->centerFrequencyHz = values[5]; settings->bandwidthHz = bandwidth;
    settings->lnaGain = lna; settings->vgaGain = vga;
    settings->amplifier = amp; settings->antennaPower = antenna;
    strcpy(settings->serial, serial);
    cfg->fif = settings->centerFrequencyHz - 1575420000.0;
    settings->loaded = 1;
    return SUCCESS_RETURN;
}

int OpenHackrfFrontend(TNGeneConfiguration *cfg, TRxFe *fe)
{
    struct HackrfFrontend *h;
    hackrf_device_list_t *devices;
    THackrfConfiguration *settings = &cfg->hackrfConfiguration;
    if (!settings->loaded && ConfigureHackrfFrontend(cfg) != SUCCESS_RETURN) return FAILURE_RETURN;
    h = calloc(1, sizeof(*h));
    if (!h) return FAILURE_RETURN;
    fe->hackrf = h;
    h->antennaPower = settings->antennaPower;
    h->queue = malloc(HACKRF_QUEUE_BYTES);
    if (!h->queue) goto failed;
    CONFIGURE(hackrf_init());
    h->initialized = 1;
    if (settings->serial[0]) {
        CONFIGURE(hackrf_open_by_serial(settings->serial, &h->device));
    } else {
        devices = hackrf_device_list();
        if (!devices || devices->devicecount != 1) {
            SwrxLog(NGENE_LOG_SEVERITY_ERROR,
                    "Expected one HackRF; found %d. Select a serial in configuration/HackRF.conf when multiple devices are connected.",
                    devices ? devices->devicecount : 0);
            if (devices) hackrf_device_list_free(devices);
            goto failed;
        }
        {
            int code = hackrf_device_list_open(devices, 0, &h->device);
            hackrf_device_list_free(devices);
            if (VendorResult(code, "open") != SUCCESS_RETURN) goto failed;
        }
    }
    CONFIGURE(hackrf_set_sample_rate_manual(h->device, 8183800, 1));
    CONFIGURE(hackrf_set_baseband_filter_bandwidth(h->device, (uint32_t)settings->bandwidthHz));
    /* Native HackRF has exp(+j*2*pi*(RF-centre)*t). The positive-sign NCO
     * uses centre-L1. correction=-1 maps NCO Doppler back to physical RF/code
     * Doppler at zero or nonzero IF, without conjugating input samples. */
    CONFIGURE(hackrf_set_freq(h->device, (uint64_t)settings->centerFrequencyHz));
    CONFIGURE(hackrf_set_lna_gain(h->device, (uint32_t)settings->lnaGain));
    CONFIGURE(hackrf_set_vga_gain(h->device, (uint32_t)settings->vgaGain));
    CONFIGURE(hackrf_set_amp_enable(h->device, (uint8_t)settings->amplifier));
    SwrxLog(NGENE_LOG_SEVERITY_INFO,
            "HackRF RX: fs %.0f complex/s, RF centre %d Hz, RF-centre %+.0f Hz, Classic NCO IF %+.0f Hz",
            cfg->fs, settings->centerFrequencyHz, -cfg->fif, cfg->fif);
    SwrxLog(NGENE_LOG_SEVERITY_INFO,
            "HackRF: LNA %d dB, VGA %d dB, RF amp %d, antenna power %d, filter %d Hz",
            settings->lnaGain, settings->vgaGain, settings->amplifier,
            settings->antennaPower, settings->bandwidthHz);
    SwrxLog(NGENE_LOG_SEVERITY_INFO,
            "HackRF CI8 input: unchanged ADC bytes; DC subtraction and unit-circle rotation in SIMD; no digital gain or input clipping");
    return SUCCESS_RETURN;
failed:
    CloseHackrfFrontend(fe);
    return FAILURE_RETURN;
}

int StartHackrfFrontend(TRxFe *fe)
{
    struct HackrfFrontend *h = fe->hackrf;
    int result;
    if (!h || h->started) return FAILURE_RETURN;
    /* Firmware turns antenna power off in idle mode. Set only when enabled. */
    if (h->antennaPower && VendorResult(hackrf_set_antenna_enable(h->device, 1),
                                      "antenna power") != SUCCESS_RETURN) return FAILURE_RETURN;
    result = VendorResult(hackrf_start_rx(h->device, OnHackrfSamples, fe), "start RX");
    if (result == SUCCESS_RETURN) h->started = 1;
    return result;
}

int StopHackrfFrontend(TRxFe *fe)
{
    struct HackrfFrontend *h = fe->hackrf;
    int result = SUCCESS_RETURN;
    if (h && h->started) {
        pthread_mutex_lock(&fe->usbFe.frontEndMutex);
        fe->usbFe.shutdownRequested = 1;
        pthread_cond_broadcast(&fe->usbFe.intermediateBufferEmpty);
        pthread_mutex_unlock(&fe->usbFe.frontEndMutex);
        result = VendorResult(hackrf_stop_rx(h->device), "stop RX");
        h->started = 0;
    }
    return result;
}

void CloseHackrfFrontend(TRxFe *fe)
{
    struct HackrfFrontend *h = fe->hackrf;
    if (!h) return;
    StopHackrfFrontend(fe);
    if (h->device) hackrf_close(h->device);
    if (h->initialized) hackrf_exit();
    free(h->queue);
    free(h);
    fe->hackrf = NULL;
}

unsigned int ReadHackrfFrontend(TRxFe *fe, char *buffer, unsigned int bytes)
{
    struct HackrfFrontend *h = fe->hackrf;
    TUsbFe *usb = &fe->usbFe;
    unsigned int n, first;
    pthread_mutex_lock(&usb->frontEndMutex);
    while (!h->count && !h->failed && !usb->shutdownRequested && !ngeneStopRequested)
        WaitBriefly(&usb->intermediateBufferEmpty, &usb->frontEndMutex);
    if (h->failed || usb->shutdownRequested || ngeneStopRequested) {
        pthread_mutex_unlock(&usb->frontEndMutex);
        return 0;
    }
    n = bytes < h->count ? bytes : h->count;
    first = HACKRF_QUEUE_BYTES - h->tail;
    if (first > n) first = n;
    memcpy(buffer, h->queue + h->tail, first);
    memcpy(buffer + first, h->queue, n - first);
    h->tail = (h->tail + n) % HACKRF_QUEUE_BYTES;
    h->count -= n;
    pthread_mutex_unlock(&usb->frontEndMutex);
    return n;
}

int CheckHackrfFrontend(TRxFe *fe)
{
    struct HackrfFrontend *h = fe->hackrf;
    int failure, report = 0;
    unsigned int highWater;
    pthread_mutex_lock(&fe->usbFe.frontEndMutex);
    failure = h->failed;
    highWater = h->highWater;
    if (failure && !h->reported) { h->reported = 1; report = 1; }
    pthread_mutex_unlock(&fe->usbFe.frontEndMutex);
    if (!failure && h->started && !ngeneStopRequested &&
        hackrf_is_streaming(h->device) != HACKRF_TRUE) {
        pthread_mutex_lock(&fe->usbFe.frontEndMutex);
        if (!fe->usbFe.shutdownRequested) {
            h->failed = failure = 3;
            if (!h->reported) { h->reported = 1; report = 1; }
            pthread_cond_broadcast(&fe->usbFe.intermediateBufferEmpty);
        }
        pthread_mutex_unlock(&fe->usbFe.frontEndMutex);
    }
    if (report) SwrxLog(NGENE_LOG_SEVERITY_ERROR,
            "HackRF RX stopped: %s (queue high-water %u/%u bytes)",
            failure == 2 ? "receiver queue overflow; continuity lost" :
            failure == 1 ? "invalid callback data" : "USB stream interrupted",
            highWater, HACKRF_QUEUE_BYTES);
    return failure ? FAILURE_RETURN : SUCCESS_RETURN;
}

void ServiceHackrfFrontend(TRxFe *fe)
{
    /* libhackrf owns USB event servicing. This worker only monitors lifecycle;
     * it must never enter SiGe's libusb event/copy loop. */
    pthread_mutex_lock(&fe->usbFe.frontEndMutex);
    if (!fe->usbFe.shutdownRequested && !ngeneStopRequested)
        WaitBriefly(&fe->usbFe.intermediateBufferFull, &fe->usbFe.frontEndMutex);
    pthread_mutex_unlock(&fe->usbFe.frontEndMutex);
}
