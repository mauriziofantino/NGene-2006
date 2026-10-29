#ifndef NGENE_NATIVE_COMPAT_H
#define NGENE_NATIVE_COMPAT_H
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <io.h>
#include <direct.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
typedef SSIZE_T ssize_t;
#endif
#ifdef interface
#undef interface
#endif
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
int NgClockGettime(int clockId, struct timespec *time);
int NgGettimeofday(struct timeval *time, void *zone);
int NgPosixMemalign(void **out, size_t alignment, size_t size);
ssize_t NgGetline(char **line, size_t *capacity, FILE *file);
unsigned int NgSleep(unsigned int seconds);
int NgNanosleep(const struct timespec *duration, struct timespec *remaining);
int NgFtruncate(int file, int64_t length);
const char *NgNativeHome(void);
int NgSocketInit(void);
ssize_t NgSocketRead(SOCKET socket, void *buffer, size_t size);
ssize_t NgSocketWrite(SOCKET socket, const void *buffer, size_t size);
ssize_t NgSocketSendfile(SOCKET socket, int input, int64_t *offset, size_t count);
#define clock_gettime NgClockGettime
#define gettimeofday NgGettimeofday
#define posix_memalign NgPosixMemalign
#define getline NgGetline
#define sleep NgSleep
#define nanosleep NgNanosleep
#define ftruncate NgFtruncate
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#define strdup _strdup
#define getcwd _getcwd
#define fileno _fileno
#define read _read
#define write _write
#define close _close
#define lseek _lseeki64
#define unlink _unlink
#define stat _stat64
#define fstat _fstat64
#define fseek _fseeki64
#define ftell _ftelli64
#ifndef S_ISDIR
#define S_ISDIR(mode) (((mode) & _S_IFMT) == _S_IFDIR)
#define S_ISREG(mode) (((mode) & _S_IFMT) == _S_IFREG)
#endif
#define bzero(pointer, size) memset((pointer), 0, (size))
#define SIGQUIT SIGBREAK
#endif
