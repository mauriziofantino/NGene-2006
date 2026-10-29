/* Finite native RX capture using the production frontend. Keeps all CI8 bits,
 * checks stream status and stops cleanly; never changes receiver settings. */
#include <stdarg.h>
#include <stdlib.h>
#include <signal.h>
#include <math.h>
#include "fft.h"
#include "hackrfFrontend.h"
#include "ioFunctions.h"
#include "signalHandler.h"
volatile sig_atomic_t ngeneStopRequested;
static TRxFe frontend;
static uint64_t deadline;
static int streamFailed;
static uint64_t MonotonicMilliseconds(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000u + (unsigned long)now.tv_nsec / 1000000u;
}
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
    while (!ngeneStopRequested && MonotonicMilliseconds() < deadline) {
        const struct timespec pause = {0, 20000000};
        if (CheckHackrfFrontend(&frontend) != SUCCESS_RETURN) {
            streamFailed = 1;
            break;
        }
        nanosleep(&pause, NULL);
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
    uint64_t rails[2] = {0}, frames = 0;
    short fftInput[8192], fftOutput[8192];
    double spectrum[4096] = {0};
    TFftTable fft = {0};
    unsigned int n, k, seconds;
    uint64_t start;
    if (argc != 4) {
        fprintf(stderr, "Usage: ngene_hackrf_capture CONFIGURATION_FOLDER OUTPUT_CI8 SECONDS\n");
        return 1;
    }
    seconds = (unsigned int)strtoul(argv[3], NULL, 10);
    if (!seconds || seconds > 600)
        return 1;
    snprintf(cfg.configurationPath, sizeof(cfg.configurationPath), "%s/", argv[1]);
    cfg.fs = 8183800;
    cfg.fif = 38400;
    cfg.nativeIq = 1;
    cfg.fePars = &parameters;
    parameters.resolution = 8;
    parameters.dopplerCorrection = -1;
    if (InitFftTable(&cfg, &fft) != SUCCESS_RETURN)
        return 1;
    pthread_mutex_init(&frontend.usbFe.frontEndMutex, NULL);
    pthread_cond_init(&frontend.usbFe.intermediateBufferEmpty, NULL);
    pthread_cond_init(&frontend.usbFe.intermediateBufferFull, NULL);
    if (OpenHackrfFrontend(&cfg, &frontend) != SUCCESS_RETURN)
        return 1;
    file = fopen(argv[2], "wb");
    if (!file) {
        CloseHackrfFrontend(&frontend);
        return 1;
    }
    setvbuf(file, NULL, _IOFBF, 1048576);
    if (StartHackrfFrontend(&frontend) != SUCCESS_RETURN) {
        fclose(file);
        CloseHackrfFrontend(&frontend);
        return 1;
    }
    start = MonotonicMilliseconds();
    deadline = start + seconds * 1000u;
    if (pthread_create(&monitor, NULL, Monitor, NULL)) {
        StopHackrfFrontend(&frontend);
        CloseHackrfFrontend(&frontend);
        fclose(file);
        return 1;
    }
    while ((n = ReadHackrfFrontend(&frontend, bytes, sizeof(bytes))) != 0) {
        if (fwrite(bytes, 1, n, file) != n) {
            ngeneStopRequested = 1;
            streamFailed = 1;
            break;
        }
        for (k = 0; k < n; k++) {
            int value = (signed char)bytes[k];
            unsigned int component = (unsigned int)((total + k) & 1);
            sum[component] += value;
            squares[component] += value * value;
            if (value == -128 || value == 127)
                rails[component]++;
            fftInput[(total + k) % 8192] = (short)(value * 128);
            if ((total + k) % 8192 == 8191) {
                unsigned bin;
                PerformFft(&cfg, &fft, fftInput, fftOutput, 12, -12);
                for (bin = 0; bin < 4096; bin++) {
                    double r = fftOutput[2 * bin], i = fftOutput[2 * bin + 1];
                    spectrum[bin] += r * r + i * i;
                }
                frames++;
            }
        }
        total += n;
    }
    ngeneStopRequested = 1;
    pthread_join(monitor, NULL);
    StopHackrfFrontend(&frontend);
    CloseHackrfFrontend(&frontend);
    fclose(file);
    printf("Saved %llu bytes, %.6f signal seconds, %.3f wall seconds\n", (unsigned long long)total,
           total / 16367600.0, (MonotonicMilliseconds() - start) / 1000.0);
    if (total)
        printf("Component means %.5f %.5f, mean squares %.5f %.5f\n", sum[0] / (total / 2.0),
               sum[1] / (total / 2.0), squares[0] / (total / 2.0), squares[1] / (total / 2.0));
    if (total) {
        double mi = sum[0] / (total / 2.0), mq = sum[1] / (total / 2.0);
        double rms = sqrt((squares[0] + squares[1]) / (double)total - (mi * mi + mq * mq) / 2);
        printf("Centred ADC RMS %.5f counts, ADC rails I %.8f%% Q %.8f%%\n", rms,
               200.0 * rails[0] / total, 200.0 * rails[1] / total);
    }
    if (frames) {
        unsigned bin, maxBin = 0, bins = 0;
        double average = 0, maximum = 0;
        for (bin = 1; bin < 4096; bin++) {
            int signedBin = bin < 2048 ? (int)bin : (int)bin - 4096;
            if (abs(signedBin) < 3 || abs(signedBin) > 1000)
                continue;
            average += spectrum[bin];
            bins++;
            if (spectrum[bin] > maximum) {
                maximum = spectrum[bin];
                maxBin = bin;
            }
        }
        average /= bins;
        printf(
            "Complex FFT 4096, %llu windows: DC/mean %.2f dB; strongest non-DC bin %.1f Hz, %.2f dB above mean (+/-2 MHz)\n",
            (unsigned long long)frames, 10 * log10(spectrum[0] / average),
            (maxBin < 2048 ? (int)maxBin : (int)maxBin - 4096) * cfg.fs / 4096,
            10 * log10(maximum / average));
    }
    DeinitFftTable(&fft);
    return streamFailed ? 1 : 0;
}
