/** @file cygwin_sendfile.h
 * @brief POSIX file-to-socket copy for Cygwin, which lacks Linux sendfile.
 */
#ifndef NGENE_CYGWIN_SENDFILE_H
#define NGENE_CYGWIN_SENDFILE_H
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>

/** @brief Copy up to count bytes, advancing the explicit file offset.
 * @param output Destination socket descriptor.
 * @param input Source file descriptor.
 * @param offset Source offset, updated after each successful write.
 * @param count Maximum number of bytes to copy.
 * @return Bytes copied, or -1 if an error occurs before any bytes are copied.
 */
static ssize_t NgCygwinSendfile(int output, int input, off_t *offset, size_t count)
{
    char buffer[16384];
    size_t total = 0;
    while (total < count) {
        size_t amount = count - total;
        ssize_t received;
        size_t sent = 0;
        if (amount > sizeof(buffer)) amount = sizeof(buffer);
        received = pread(input, buffer, amount, *offset);
        if (received < 0 && errno == EINTR) continue;
        if (received <= 0) return total ? (ssize_t)total : received;
        while (sent < (size_t)received) {
            ssize_t result = write(output, buffer + sent, (size_t)received - sent);
            if (result < 0 && errno == EINTR) continue;
            if (result <= 0) return total ? (ssize_t)total : -1;
            sent += (size_t)result;
            total += (size_t)result;
            *offset += result;
        }
    }
    return (ssize_t)total;
}
#endif
