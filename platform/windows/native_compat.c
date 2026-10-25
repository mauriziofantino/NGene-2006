#include "native_compat.h"
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include "getopt.h"

int NgClockGettime(int clockId, struct timespec *out)
{
    if (!out) { errno=EINVAL; return -1; }
    if (clockId == CLOCK_MONOTONIC) {
        LARGE_INTEGER counter, frequency;
        QueryPerformanceCounter(&counter);
        QueryPerformanceFrequency(&frequency);
        out->tv_sec = (time_t)(counter.QuadPart/frequency.QuadPart);
        out->tv_nsec = (long)((counter.QuadPart%frequency.QuadPart)*1000000000LL/frequency.QuadPart);
    } else if (clockId == CLOCK_REALTIME) {
        FILETIME filetime;
        ULARGE_INTEGER ticks;
        GetSystemTimePreciseAsFileTime(&filetime);
        ticks.LowPart=filetime.dwLowDateTime; ticks.HighPart=filetime.dwHighDateTime;
        ticks.QuadPart-=116444736000000000ULL;
        out->tv_sec=(time_t)(ticks.QuadPart/10000000ULL);
        out->tv_nsec=(long)((ticks.QuadPart%10000000ULL)*100ULL);
    } else { errno=EINVAL; return -1; }
    return 0;
}

int NgGettimeofday(struct timeval *out, void *zone)
{
    struct timespec now;
    (void)zone;
    if (!out || NgClockGettime(CLOCK_REALTIME,&now)) return -1;
    out->tv_sec=(long)now.tv_sec; out->tv_usec=now.tv_nsec/1000;
    return 0;
}

int NgPosixMemalign(void **out, size_t alignment, size_t size)
{
    if (!out || alignment<sizeof(void *) || (alignment&(alignment-1))) return EINVAL;
    /* The x64 Microsoft CRT guarantees 16-byte alignment for malloc. NGene
     * requests 16 bytes; using malloc preserves its ordinary free() calls. */
    if (alignment>MEMORY_ALLOCATION_ALIGNMENT) return ENOMEM;
    *out=malloc(size ? size : alignment);
    return *out ? 0 : ENOMEM;
}

ssize_t NgGetline(char **line, size_t *capacity, FILE *file)
{
    size_t length=0;
    int ch;
    if (!line || !capacity || !file) { errno=EINVAL; return -1; }
    if (!*line || *capacity<2) {
        char *next=realloc(*line,256);
        if (!next) return -1;
        *line=next; *capacity=256;
    }
    while ((ch=fgetc(file))!=EOF) {
        if (length+1>=*capacity) {
            size_t enlarged=*capacity*2;
            char *next=realloc(*line,enlarged);
            if (!next) return -1;
            *line=next; *capacity=enlarged;
        }
        (*line)[length++]=(char)ch;
        if (ch=='\n') break;
    }
    (*line)[length]='\0';
    return length ? (ssize_t)length : -1;
}

unsigned int NgSleep(unsigned int seconds) { Sleep(seconds*1000u); return 0; }
int NgFtruncate(int file, int64_t length)
{
    errno_t error = _chsize_s(file, length);
    if (error) { errno = error; return -1; }
    return 0;
}
int NgNanosleep(const struct timespec *duration, struct timespec *remaining)
{
    LONGLONG ticks;
    HANDLE timer;
    LARGE_INTEGER due;
    if (!duration || duration->tv_sec<0 || duration->tv_nsec<0 || duration->tv_nsec>=1000000000L) { errno=EINVAL; return -1; }
    ticks=(LONGLONG)duration->tv_sec*10000000LL+(duration->tv_nsec+99)/100;
    if (!ticks) { SwitchToThread(); return 0; }
    timer=CreateWaitableTimerW(NULL,TRUE,NULL);
    if (!timer) return -1;
    due.QuadPart=-ticks;
    if (!SetWaitableTimer(timer,&due,0,NULL,NULL,FALSE)) { CloseHandle(timer); return -1; }
    WaitForSingleObject(timer,INFINITE);
    CloseHandle(timer);
    if (remaining) memset(remaining,0,sizeof(*remaining));
    return 0;
}

const char *NgNativeHome(void)
{
    static char directory[32768];
    char *last;
    DWORD length;
    length=GetModuleFileNameA(NULL,directory,sizeof(directory));
    if (!length || length>=sizeof(directory)) return NULL;
    last=strrchr(directory,'\\');
    if (last) *last='\0';
    return directory;
}

int NgSocketInit(void) { WSADATA data; return WSAStartup(MAKEWORD(2,2),&data); }
ssize_t NgSocketRead(SOCKET socket, void *buffer, size_t size)
{
    size_t received=0;
    /* WinSock has no SO_RCVLOWAT. Preserve the GUI's requested minimum
     * byte count by accumulating fragments until completion, EOF or timeout. */
    while (received<size) {
        size_t amount=size-received;
        int result=recv(socket,(char *)buffer+received,(int)(amount>INT_MAX ? INT_MAX : amount),0);
        if (result==SOCKET_ERROR && WSAGetLastError()==WSAEINTR) continue;
        if (result==SOCKET_ERROR) { errno=EIO; return received ? (ssize_t)received : -1; }
        if (!result) break;
        received+=(size_t)result;
    }
    return (ssize_t)received;
}
ssize_t NgSocketWrite(SOCKET socket, const void *buffer, size_t size)
{
    size_t sent=0;
    while (sent<size) {
        size_t amount=size-sent;
        int result=send(socket,(const char *)buffer+sent,(int)(amount>INT_MAX ? INT_MAX : amount),0);
        if (result==SOCKET_ERROR && WSAGetLastError()==WSAEINTR) continue;
        if (result<=0) { errno=EIO; return sent ? (ssize_t)sent : -1; }
        sent+=(size_t)result;
    }
    return (ssize_t)sent;
}
ssize_t NgSocketSendfile(SOCKET socket, int input, int64_t *offset, size_t count)
{
    char buffer[16384];
    size_t total=0;
    if (_lseeki64(input,*offset,SEEK_SET)<0) return -1;
    while (total<count) {
        size_t amount=count-total;
        int received;
        ssize_t sent;
        if (amount>sizeof(buffer)) amount=sizeof(buffer);
        received=_read(input,buffer,(unsigned int)amount);
        if (received<=0) return total ? (ssize_t)total : received;
        sent=NgSocketWrite(socket,buffer,(size_t)received);
        if (sent<=0) return total ? (ssize_t)total : -1;
        total+=(size_t)sent; *offset+=sent;
        if (sent!=received) return (ssize_t)total;
    }
    return (ssize_t)total;
}

char *optarg;
int optind=1, opterr=1, optopt;
int getopt_long(int argc, char *const argv[], const char *options,
                const struct option *longOptions, int *longIndex)
{
    static const char *cursor;
    const char *spec;
    int value;
    optarg=NULL;
    if (!cursor || !*cursor) {
        const char *argument;
        if (optind>=argc) return -1;
        argument=argv[optind];
        if (argument[0]!='-' || !argument[1]) return -1;
        optind++;
        if (!strcmp(argument,"--")) return -1;
        if (argument[1]=='-') {
            const char *name=argument+2, *equal=strchr(name,'=');
            size_t length=equal ? (size_t)(equal-name) : strlen(name);
            int k;
            for(k=0;longOptions[k].name;k++) {
                const struct option *option=&longOptions[k];
                if (strlen(option->name)!=length || strncmp(name,option->name,length)) continue;
                if (longIndex) *longIndex=k;
                if (option->has_arg) {
                    if (equal) optarg=(char *)equal+1;
                    else if (option->has_arg==required_argument && optind<argc) optarg=argv[optind++];
                    else if (option->has_arg==required_argument) return '?';
                } else if (equal) return '?';
                if (option->flag) { *option->flag=option->val; return 0; }
                return option->val;
            }
            return '?';
        }
        cursor=argument+1;
    }
    value=(unsigned char)*cursor++;
    spec=strchr(options,value);
    if (!spec) { optopt=value; return '?'; }
    if (spec[1]==':') {
        if (*cursor) { optarg=(char *)cursor; cursor=NULL; }
        else if (optind<argc) { optarg=argv[optind++]; cursor=NULL; }
        else { cursor=NULL; optopt=value; return '?'; }
    }
    return value;
}
