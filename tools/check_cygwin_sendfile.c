/** @file check_cygwin_sendfile.c
 * @brief Check byte preservation and explicit offsets in the Windows GUI adapter.
 */
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include "../platform/cygwin_sendfile.h"

/** @brief Exercise offset, EOF and payload handling without a GUI application.
 * @return Zero on a byte-for-byte match.
 */
int main(void)
{
    unsigned char input[20000], output[20000];
    int sockets[2];
    FILE *file = tmpfile();
    off_t offset = 17;
    size_t i, total = 0, length = sizeof(input) - 17;
    if (!file || socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0) return 1;
    for (i = 0; i < sizeof(input); i++) input[i] = (unsigned char)i;
    if (fwrite(input, 1, sizeof(input), file) != sizeof(input) || fflush(file)) return 1;
    if (NgCygwinSendfile(sockets[0], fileno(file), &offset, sizeof(input)) != (ssize_t)length ||
        offset != sizeof(input)) return 1;
    while (total < length) {
        ssize_t received = read(sockets[1], output + total, length - total);
        if (received <= 0) return 1;
        total += (size_t)received;
    }
    if (memcmp(input + 17, output, length) != 0 ||
        NgCygwinSendfile(sockets[0], fileno(file), &offset, 10) != 0) return 1;
    close(sockets[0]);
    close(sockets[1]);
    fclose(file);
    puts("GUI file payload, explicit offset and EOF passed.");
    return 0;
}
