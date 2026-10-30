/* Exercise the Win32 adapters with real CRT files, clocks, threads and sockets. */
#include "native_compat.h"
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include "getopt.h"
/* Assertions in this test must execute in Release too. */
#undef assert
#define assert(condition)                                                                          \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "Failed at line %d: %s\n", __LINE__, #condition);                      \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static void *worker(void *argument)
{
    *(int *)argument = 42;
    return NULL;
}

int main(void)
{
    struct timespec before, after, wall, wait = {0, 2000000};
    pthread_t thread;
    int value = 0;
    void *aligned = NULL;
    FILE *file;
    char *line = NULL;
    size_t capacity = 0;
    SOCKET listener, sender, receiver;
    struct sockaddr_in address;
    int addressSize = sizeof(address);
    unsigned char input[20000], output[20000];
    size_t i, total = 0;
    int64_t offset = 17;
    struct option options[] = {{"port", required_argument, 0, 'p'}, {0, 0, 0, 0}};
    char *arguments[] = {"ngene", "--port=4999", "-tv", NULL};
    assert(NgClockGettime(CLOCK_MONOTONIC, &before) == 0);
    assert(NgNanosleep(&wait, NULL) == 0);
    assert(NgClockGettime(CLOCK_MONOTONIC, &after) == 0);
    assert(after.tv_sec > before.tv_sec ||
           (after.tv_sec == before.tv_sec && after.tv_nsec > before.tv_nsec));
    assert(NgClockGettime(CLOCK_REALTIME, &wall) == 0);
    assert(llabs((long long)wall.tv_sec - (long long)time(NULL)) <= 1);
    assert(NgPosixMemalign(&aligned, 16, 129) == 0 && ((uintptr_t)aligned % 16) == 0);
    memset(aligned, 0xa5, 129);
    free(aligned);
    assert(pthread_create(&thread, NULL, worker, &value) == 0);
    assert(pthread_join(thread, NULL) == 0 && value == 42);
    assert(getopt_long(3, arguments, "p:tv", options, NULL) == 'p' && !strcmp(optarg, "4999"));
    assert(getopt_long(3, arguments, "p:tv", options, NULL) == 't');
    assert(getopt_long(3, arguments, "p:tv", options, NULL) == 'v');
    assert(getopt_long(3, arguments, "p:tv", options, NULL) == -1);
    file = tmpfile();
    assert(file);
    assert(fputs("first\nlast", file) >= 0 && fseek(file, 0, SEEK_SET) == 0);
    assert(NgGetline(&line, &capacity, file) == 6 && !strcmp(line, "first\n"));
    assert(NgGetline(&line, &capacity, file) == 4 && !strcmp(line, "last"));
    assert(NgGetline(&line, &capacity, file) == -1);
    free(line);
    fclose(file);
    assert(NgSocketInit() == 0);
    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(listener != INVALID_SOCKET);
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(bind(listener, (struct sockaddr *)&address, sizeof(address)) == 0);
    assert(getsockname(listener, (struct sockaddr *)&address, &addressSize) == 0 &&
           listen(listener, 1) == 0);
    sender = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(sender != INVALID_SOCKET);
    assert(connect(sender, (struct sockaddr *)&address, sizeof(address)) == 0);
    receiver = accept(listener, NULL, NULL);
    assert(receiver != INVALID_SOCKET);
    {
        DWORD timeout = 2000;
        assert(setsockopt(receiver, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout,
                          sizeof(timeout)) == 0);
    }
    file = tmpfile();
    assert(file);
    assert(_setmode(fileno(file), _O_BINARY) != -1);
    for (i = 0; i < sizeof(input); i++)
        input[i] = (unsigned char)i;
    assert(fwrite(input, 1, sizeof(input), file) == sizeof(input) && fflush(file) == 0);
    assert(NgSocketSendfile(sender, fileno(file), &offset, sizeof(input)) == sizeof(input) - 17);
    assert(offset == sizeof(input));
    while (total < sizeof(input) - 17) {
        ssize_t amount = NgSocketRead(receiver, output + total, sizeof(input) - 17 - total);
        assert(amount > 0);
        total += (size_t)amount;
    }
    assert(memcmp(input + 17, output, total) == 0);
    assert(NgSocketSendfile(sender, fileno(file), &offset, 10) == 0);
    /* A sparse file exercises offsets above the 32-bit CRT limit. */
    offset = 0x100000011LL;
    assert(_lseeki64(fileno(file), offset, SEEK_SET) == offset);
    assert(_write(fileno(file), "xyz", 3) == 3);
    assert(NgSocketSendfile(sender, fileno(file), &offset, 3) == 3 && offset == 0x100000014LL);
    total = 0;
    while (total < 3) {
        ssize_t amount = NgSocketRead(receiver, output + total, 3 - total);
        assert(amount > 0);
        total += (size_t)amount;
    }
    assert(memcmp(output, "xyz", 3) == 0);
    assert(NgFtruncate(fileno(file), 0) == 0);
    fclose(file);
    closesocket(receiver);
    closesocket(sender);
    closesocket(listener);
    WSACleanup();
    puts(
        "Native clocks, aligned allocation, pthreads, options, getline, sockets and 64-bit file offsets passed.");
    return 0;
}
