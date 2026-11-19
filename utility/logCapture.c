/**
 * @file logCapture.c
 *
 * @author Maurizio Fantino
 *
 * @date 19/11/2026
 *
 * @brief Queue of the log messages deferred by the elaboration of one channel (see
 *        utility/logCapture.h). The queue is embedded in the channel, so the memory of
 *        the log of one channel is bounded and allocated with the channel itself.
 */
#include <stdio.h>
#include <string.h>
#include "ngeneConstants.h"
#include "ioFunctions.h"
#include "logCapture.h"

/** @brief Capture target of the calling thread, NULL when the log is immediate. */
NGENE_THREAD_LOCAL TLogCapture *ngeneLogCaptureTarget = NULL;

void InitLogCapture(TLogCapture *capture)
{
    capture->messageCount = 0;
    capture->overflowCount = 0;
}

void BeginLogCapture(TLogCapture *capture)
{
    ngeneLogCaptureTarget = capture;
}

void EndLogCapture(void)
{
    ngeneLogCaptureTarget = NULL;
}

int AppendLogCaptureEntry(int severity, const char *message)
{
    TLogCapture *capture = ngeneLogCaptureTarget;
    unsigned int messageLength;
    if (capture == NULL)
    {
        return FAILURE_RETURN; /* nothing is diverted on this thread */
    }
    if (capture->messageCount >= (unsigned int)LOG_CAPTURE_MAXIMUM_MESSAGES)
    {
        capture->overflowCount++;
        return FAILURE_RETURN; /* the caller emits the message, see SwrxLog() */
    }
    messageLength = (unsigned int)strlen(message);
    /* SwrxLog() renders the message in MAX_ROW_LENGTH characters, so the text always fits
     * the queue of one message. A longer message is not queued. */
    if (messageLength >= (unsigned int)LOG_CAPTURE_MESSAGE_SIZE)
    {
        capture->overflowCount++;
        return FAILURE_RETURN;
    }
    capture->severities[capture->messageCount] = severity;
    memcpy(&(capture->messages[capture->messageCount][0]), message, (size_t)(messageLength + 1));
    capture->messageCount++;
    return SUCCESS_RETURN;
}

int FlushLogCapture(TLogCapture *capture)
{
    unsigned int index;
    if (capture == NULL)
    {
        return FAILURE_RETURN; /* no queue to replay */
    }
    for (index = 0; index < capture->messageCount; index++)
    {
        /* the severity is preserved, so the routing chosen by the interface does not change */
        SwrxLog(capture->severities[index], "%s", capture->messages[index]);
    }
    if (capture->overflowCount > 0)
    {
        SwrxLog(NGENE_LOG_SEVERITY_WARNING,
                "Channel elaboration produced more than %d log messages: %u emitted immediately",
                LOG_CAPTURE_MAXIMUM_MESSAGES, capture->overflowCount);
    }
    capture->messageCount = 0;
    capture->overflowCount = 0;
    return SUCCESS_RETURN;
}
