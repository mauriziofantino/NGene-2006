/* Finite native RX capture using the production frontend. Keeps all CI8 bits,
 * checks stream status and stops cleanly; never changes receiver settings. */
#include <stdarg.h>
#include <signal.h>
#include "hackrfFrontend.h"
#include "ioFunctions.h"
#include "signalHandler.h"

volatile sig_atomic_t ngeneStopRequested;
static TRxFe frontend;
static ULONGLONG deadline;
static int streamFailed;
int SwrxLog(int severity, const char *format, ...)
{
    va_list arguments;
    (void)severity;
    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);
    fputc('\n', stderr);
    return 0;
}
static void *Monitor(void *unused)
{
    (void)unused;
    while (!ngeneStopRequested && GetTickCount64() < deadline) {
        if (CheckHackrfFrontend(&frontend) != SUCCESS_RETURN) { streamFailed = 1; break; }
        Sleep(20);
    }
    pthread_mutex_lock(&frontend.usbFe.frontEndMutex);
    frontend.usbFe.shutdownRequested = 1;
    pthread_cond_broadcast(&frontend.usbFe.intermediateBufferEmpty);
    pthread_mutex_unlock(&frontend.usbFe.frontEndMutex);
    return NULL;
}
int main(int argc, char **argv)
{
    TNGeneConfiguration cfg = {0};
    TUsbFePars parameters = {0};
    pthread_t monitor;
    FILE *file;
    char bytes[262144];
    uint64_t total = 0;
    int64_t sum[2] = {0}, squares[2] = {0};
    unsigned int n, k, seconds;
    ULONGLONG start;
    if (argc != 4) { fprintf(stderr, "Usage: ngene_hackrf_capture CONFIGURATION_FOLDER OUTPUT_CI8 SECONDS\n"); return 1; }
    seconds = (unsigned int)strtoul(argv[3], NULL, 10);
    if (!seconds || seconds > 600) return 1;
    snprintf(cfg.configurationPath, sizeof(cfg.configurationPath), "%s/", argv[1]);
    cfg.fs = 8183800; cfg.fif = 38400; cfg.nativeIq = 1;
    cfg.fePars = &parameters;
    parameters.resolution = 8; parameters.dopplerCorrection = -1;
    pthread_mutex_init(&frontend.usbFe.frontEndMutex, NULL);
    pthread_cond_init(&frontend.usbFe.intermediateBufferEmpty, NULL);
    pthread_cond_init(&frontend.usbFe.intermediateBufferFull, NULL);
    if (OpenHackrfFrontend(&cfg, &frontend) != SUCCESS_RETURN) return 1;
    file = fopen(argv[2], "wb");
    if (!file) { CloseHackrfFrontend(&frontend); return 1; }
    setvbuf(file, NULL, _IOFBF, 1048576);
    start = GetTickCount64(); deadline = start + seconds * 1000u;
    if (StartHackrfFrontend(&frontend) != SUCCESS_RETURN) {
        fclose(file); CloseHackrfFrontend(&frontend); return 1;
    }
    if (pthread_create(&monitor, NULL, Monitor, NULL)) {
        StopHackrfFrontend(&frontend); CloseHackrfFrontend(&frontend); fclose(file); return 1;
    }
    while ((n = ReadHackrfFrontend(&frontend, bytes, sizeof(bytes))) != 0) {
        if (fwrite(bytes, 1, n, file) != n) { ngeneStopRequested = 1; streamFailed = 1; break; }
        for (k = 0; k < n; k++) {
            int value = (signed char)bytes[k];
            unsigned int component = (unsigned int)((total + k) & 1);
            sum[component] += value; squares[component] += value * value;
        }
        total += n;
    }
    ngeneStopRequested = 1;
    pthread_join(monitor, NULL);
    StopHackrfFrontend(&frontend);
    CloseHackrfFrontend(&frontend);
    fclose(file);
    printf("Saved %llu bytes, %.6f signal seconds, %.3f wall seconds\n",
           (unsigned long long)total, total / 16367600.0, (GetTickCount64()-start)/1000.0);
    if (total) printf("Component means %.5f %.5f, mean squares %.5f %.5f\n",
                      sum[0] / (total/2.0), sum[1] / (total/2.0),
                      squares[0] / (total/2.0), squares[1] / (total/2.0));
    return streamFailed ? 1 : 0;
}
