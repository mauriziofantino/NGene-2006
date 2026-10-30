#ifndef NGENE_FRONTEND_STATUS_H
#define NGENE_FRONTEND_STATUS_H
#include <stdio.h>
#include <string.h>

/* Keep the operational calibration state separate from transient receiver
 * messages. Timestamped warning/error logs use the same retained payload. */
static inline int FrontendStatusUpdate(char *destination, size_t length, const char *message)
{
    const char *status;
    if (!destination || !length || !message)
        return 0;
    status = strstr(message, "HackRF autotune");
    if (!status)
        return 0;
    snprintf(destination, length, "%s", status);
    return 1;
}
#endif
