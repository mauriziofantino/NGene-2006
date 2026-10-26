/* Exercise production HackRF RX lifecycle/queue with the official vendor ABI.
 * Only hardware calls are simulated; no radio is required for CTest. */
#include <hackrf.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "hackrfFrontend.h"
#include "ioFunctions.h"
#include "signalHandler.h"

volatile sig_atomic_t ngeneStopRequested;
static hackrf_sample_block_cb_fn callback;
static void *callbackContext;
static hackrf_device_list_t deviceList;
static int stage, failAt, closed, exited, stopped, streaming, callbackResult;
static uint32_t rate, divider, filterHz, lnaGain, vgaGain;
static uint64_t frequency;
static unsigned char ampValue;
static int loggedOverflow, readFinished;
static unsigned char block[262144];
static char output[262144];
#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "HackRF check failed at line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static int Step(void) { return ++stage == failAt ? HACKRF_ERROR_USB_API_VERSION : HACKRF_SUCCESS; }
int ADDCALL hackrf_init(void) { return Step(); }
int ADDCALL hackrf_exit(void) { exited++; return HACKRF_SUCCESS; }
hackrf_device_list_t *ADDCALL hackrf_device_list(void) { return &deviceList; }
void ADDCALL hackrf_device_list_free(hackrf_device_list_t *list) { REQUIRE(list == &deviceList); }
int ADDCALL hackrf_device_list_open(hackrf_device_list_t *list, int index, hackrf_device **device)
{
    int result = Step();
    REQUIRE(list == &deviceList && index == 0);
    if (result == HACKRF_SUCCESS) *device = (hackrf_device *)(uintptr_t)1;
    return result;
}
int ADDCALL hackrf_open_by_serial(const char *serial, hackrf_device **device)
{ (void)serial; return hackrf_device_list_open(&deviceList, 0, device); }
int ADDCALL hackrf_close(hackrf_device *device)
{ REQUIRE(device != NULL); closed++; return HACKRF_SUCCESS; }
int ADDCALL hackrf_set_sample_rate_manual(hackrf_device *device, uint32_t hz, uint32_t div)
{ (void)device; rate = hz; divider = div; return Step(); }
int ADDCALL hackrf_set_baseband_filter_bandwidth(hackrf_device *device, uint32_t hz)
{ (void)device; filterHz = hz; return Step(); }
int ADDCALL hackrf_set_freq(hackrf_device *device, uint64_t hz)
{ (void)device; frequency = hz; return Step(); }
int ADDCALL hackrf_set_lna_gain(hackrf_device *device, uint32_t gain)
{ (void)device; lnaGain = gain; return Step(); }
int ADDCALL hackrf_set_vga_gain(hackrf_device *device, uint32_t gain)
{ (void)device; vgaGain = gain; return Step(); }
int ADDCALL hackrf_set_amp_enable(hackrf_device *device, uint8_t enabled)
{ (void)device; ampValue = enabled; return Step(); }
int ADDCALL hackrf_set_antenna_enable(hackrf_device *device, uint8_t enabled)
{ (void)device; REQUIRE(enabled == 1); return Step(); }
uint32_t ADDCALL hackrf_compute_baseband_filter_bw(uint32_t bandwidth)
{ return bandwidth == 5000000 ? bandwidth : 0; }
const char *ADDCALL hackrf_error_name(enum hackrf_error error)
{ (void)error; return "simulated vendor error"; }
int ADDCALL hackrf_start_rx(hackrf_device *device, hackrf_sample_block_cb_fn cb, void *context)
{
    int result = Step();
    (void)device;
    if (result == HACKRF_SUCCESS) { callback = cb; callbackContext = context; streaming = 1; }
    return result;
}
int ADDCALL hackrf_stop_rx(hackrf_device *device)
{ (void)device; stopped++; streaming = 0; return HACKRF_SUCCESS; }
int ADDCALL hackrf_is_streaming(hackrf_device *device)
{ (void)device; return streaming ? HACKRF_TRUE : HACKRF_SUCCESS; }
int SwrxLog(int severity, const char *format, ...)
{
    (void)severity;
    if (strstr(format, "HackRF RX stopped")) loggedOverflow++;
    return 0;
}
static void Initialize(TRxFe *fe, TNGeneConfiguration *cfg, TUsbFePars *pars)
{
    memset(fe, 0, sizeof(*fe));
    memset(cfg, 0, sizeof(*cfg));
    memset(pars, 0, sizeof(*pars));
    pthread_mutex_init(&fe->usbFe.frontEndMutex, NULL);
    pthread_cond_init(&fe->usbFe.intermediateBufferFull, NULL);
    pthread_cond_init(&fe->usbFe.intermediateBufferEmpty, NULL);
    cfg->fs = 8183800; cfg->fif = 38400; cfg->nativeIq = 1;
    cfg->fePars = pars; pars->resolution = 8; pars->dopplerCorrection = -1;
    /* No test settings file: use production defaults independently of cwd. */
    strcpy(cfg->configurationPath, "no-such-ngene-test-folder/");
    stage = failAt = closed = exited = stopped = loggedOverflow = 0;
    callback = NULL; callbackContext = NULL; streaming = 0;
    deviceList.devicecount = 1;
    ngeneStopRequested = 0;
}
static void Destroy(TRxFe *fe)
{
    CloseHackrfFrontend(fe);
    REQUIRE(fe->hackrf == NULL);
    pthread_mutex_destroy(&fe->usbFe.frontEndMutex);
    pthread_cond_destroy(&fe->usbFe.intermediateBufferFull);
    pthread_cond_destroy(&fe->usbFe.intermediateBufferEmpty);
}
static int Submit(int length, int capacity, unsigned char *bytes)
{
    hackrf_transfer transfer = {0};
    transfer.rx_ctx = callbackContext;
    transfer.buffer = bytes; transfer.valid_length = length; transfer.buffer_length = capacity;
    callbackResult = callback(&transfer);
    return callbackResult;
}
static void *BlockedReader(void *context)
{
    TRxFe *fe = context;
    REQUIRE(ReadHackrfFrontend(fe, output, 2) == 0);
    pthread_mutex_lock(&fe->usbFe.frontEndMutex);
    readFinished = 1;
    pthread_mutex_unlock(&fe->usbFe.frontEndMutex);
    return NULL;
}
static void VerifySettings(void)
{
    static const char *invalid[] = {
        "[LNA gain (dB)]\n33\n", "[VGA gain (dB)]\n63\n",
        "[RF amplifier]\n2\n", "[Antenna power]\n-1\n",
        "[LNA gain (dB)]\n32oops\n", "[Serial number]\nnot-a-serial\n",
        "[Center frequency (Hz)]\n0\n", "[Center frequency (Hz)]\n1575420000oops\n",
        "[LNA gain (dB)]\n", "[RF amplifier]\n0\n[RF amplifier]\n1\n"
    };
#ifdef NGENE_NATIVE_WINDOWS
    char tempPath[MAX_PATH], folder[MAX_PATH], filename[MAX_PATH];
#else
    char folder[] = "/tmp/ngene-hackrf-XXXXXX", filename[256];
#endif
    TRxFe fe;
    TNGeneConfiguration cfg;
    TUsbFePars pars;
    FILE *file;
    unsigned int k;
#ifdef NGENE_NATIVE_WINDOWS
    REQUIRE(GetTempPathA(sizeof(tempPath), tempPath) != 0);
    REQUIRE(GetTempFileNameA(tempPath, "ngr", 0, folder) != 0);
    REQUIRE(DeleteFileA(folder) && CreateDirectoryA(folder, NULL));
#else
    REQUIRE(mkdtemp(folder) != NULL);
#endif
    snprintf(filename, sizeof(filename), "%s/HackRF.conf", folder);
    for (k = 0; k < sizeof(invalid) / sizeof(invalid[0]); k++) {
        Initialize(&fe, &cfg, &pars);
        snprintf(cfg.configurationPath, sizeof(cfg.configurationPath), "%s/", folder);
        file = fopen(filename, "w"); REQUIRE(file != NULL);
        fputs(invalid[k], file); fclose(file);
        REQUIRE(OpenHackrfFrontend(&cfg, &fe) == FAILURE_RETURN && stage == 0);
        Destroy(&fe);
    }
    Initialize(&fe, &cfg, &pars);
    snprintf(cfg.configurationPath, sizeof(cfg.configurationPath), "%s/", folder);
    file = fopen(filename, "w"); REQUIRE(file != NULL);
    fputs("# [Serial number] this comment must be ignored\n[LNA gain (dB)]\n40\n"
          "[VGA gain (dB)]\n62\n[RF amplifier]\n1\n[Antenna power]\n1\n"
          "[Center frequency (Hz)]\n1575420000\n"
          "[Serial number]\n00000000000000000000000000000001\n", file);
    fclose(file);
    REQUIRE(OpenHackrfFrontend(&cfg, &fe) == SUCCESS_RETURN);
    REQUIRE(lnaGain == 40 && vgaGain == 62 && ampValue == 1);
    REQUIRE(cfg.fif == 0 && frequency == UINT64_C(1575420000));
    REQUIRE(StartHackrfFrontend(&fe) == SUCCESS_RETURN);
    Destroy(&fe);
#ifdef NGENE_NATIVE_WINDOWS
    REQUIRE(DeleteFileA(filename) && RemoveDirectoryA(folder));
#else
    REQUIRE(unlink(filename) == 0 && rmdir(folder) == 0);
#endif
}
int main(void)
{
    TRxFe fe;
    TNGeneConfiguration cfg;
    TUsbFePars pars;
    unsigned int k, round, position = 0;
    int failure;
    pthread_t thread;
    VerifySettings();
    for (k = 0; k < sizeof(block); k++) block[k] = (unsigned char)(k * 73 + k / 256);
    /* Every configuration hardware call must unwind exactly what it acquired. */
    for (failure = 1; failure <= 8; failure++) {
        Initialize(&fe, &cfg, &pars);
        failAt = failure;
        REQUIRE(OpenHackrfFrontend(&cfg, &fe) == FAILURE_RETURN && fe.hackrf == NULL);
        REQUIRE(exited == (failure > 1) && closed == (failure > 2));
        Destroy(&fe);
    }
    Initialize(&fe, &cfg, &pars);
    cfg.fs = 16367600;
    REQUIRE(OpenHackrfFrontend(&cfg, &fe) == FAILURE_RETURN && stage == 0);
    Destroy(&fe);
    for (failure = 0; failure <= 2; failure += 2) {
        Initialize(&fe, &cfg, &pars);
        deviceList.devicecount = failure;
        REQUIRE(OpenHackrfFrontend(&cfg, &fe) == FAILURE_RETURN);
        REQUIRE(closed == 0 && exited == 1);
        Destroy(&fe);
    }
    Initialize(&fe, &cfg, &pars);
    REQUIRE(OpenHackrfFrontend(&cfg, &fe) == SUCCESS_RETURN);
    REQUIRE(rate == 8183800 && divider == 1 && filterHz == 5000000);
    REQUIRE(frequency == UINT64_C(1575458400) && lnaGain == 32 && vgaGain == 32 && ampValue == 0);
    failAt = stage + 1;
    REQUIRE(StartHackrfFrontend(&fe) == FAILURE_RETURN);
    REQUIRE(stopped == 0);
    Destroy(&fe);
    REQUIRE(closed == 1 && exited == 1 && stopped == 0);

    Initialize(&fe, &cfg, &pars);
    REQUIRE(OpenHackrfFrontend(&cfg, &fe) == SUCCESS_RETURN);
    REQUIRE(StartHackrfFrontend(&fe) == SUCCESS_RETURN);
    REQUIRE(Submit(0, 0, NULL) == 0);
    /* Thousands of odd/even reads and queue wraparounds preserve all CI8 bits. */
    for (round = 0; round < 40; round++) {
        unsigned int remaining = sizeof(block);
        REQUIRE(Submit(sizeof(block), sizeof(block), block) == 0);
        position = 0;
        while (remaining) {
            unsigned int request = 1 + (remaining * 17u) % 17011u;
            unsigned int actual;
            if (request > remaining) request = remaining;
            actual = ReadHackrfFrontend(&fe, output, request);
            REQUIRE(actual == request && memcmp(output, block + position, actual) == 0);
            position += actual; remaining -= actual;
        }
    }
    REQUIRE(CheckHackrfFrontend(&fe) == SUCCESS_RETURN);
    /* Callback must return immediately on full queue, then wake a receiver. */
    for (round = 0; round < 4; round++) REQUIRE(Submit(sizeof(block), sizeof(block), block) == 0);
    REQUIRE(Submit(2, 2, block) != 0);
    REQUIRE(CheckHackrfFrontend(&fe) == FAILURE_RETURN);
    REQUIRE(CheckHackrfFrontend(&fe) == FAILURE_RETURN && loggedOverflow == 1);
    REQUIRE(ReadHackrfFrontend(&fe, output, 2) == 0);
    Destroy(&fe);
    REQUIRE(stopped == 1 && closed == 1 && exited == 1);
    for (failure = 0; failure < 4; failure++) {
        Initialize(&fe, &cfg, &pars);
        REQUIRE(OpenHackrfFrontend(&cfg, &fe) == SUCCESS_RETURN);
        REQUIRE(StartHackrfFrontend(&fe) == SUCCESS_RETURN);
        REQUIRE(Submit(failure == 0 ? -1 : failure == 1 ? 1 : 4,
                       failure == 2 ? 2 : 4, failure == 3 ? NULL : block) != 0);
        REQUIRE(CheckHackrfFrontend(&fe) == FAILURE_RETURN);
        Destroy(&fe);
    }
    /* A disconnected stream and an explicit stop release an empty-queue wait. */
    for (failure = 0; failure < 2; failure++) {
        Initialize(&fe, &cfg, &pars);
        REQUIRE(OpenHackrfFrontend(&cfg, &fe) == SUCCESS_RETURN);
        REQUIRE(StartHackrfFrontend(&fe) == SUCCESS_RETURN);
        readFinished = 0;
        REQUIRE(pthread_create(&thread, NULL, BlockedReader, &fe) == 0);
        if (failure) REQUIRE(StopHackrfFrontend(&fe) == SUCCESS_RETURN);
        else { streaming = 0; REQUIRE(CheckHackrfFrontend(&fe) == FAILURE_RETURN); }
        REQUIRE(pthread_join(thread, NULL) == 0 && readFinished);
        Destroy(&fe);
        REQUIRE(stopped == 1 && closed == 1 && exited == 1);
    }
    puts("HackRF native RX configuration, cleanup, queue order, overflow, disconnect and shutdown passed.");
    return 0;
}
