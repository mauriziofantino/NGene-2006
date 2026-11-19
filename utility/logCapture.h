/**
 * @file logCapture.h
 *
 * @author Maurizio Fantino
 *
 * @date 19/11/2026
 *
 * @brief Queue of the log messages deferred by the elaboration of one channel.
 *
 * @note Stage 2 of the multi-core DSP pool (see receiver/dspWorkerPool.h). While the
 *       workers elaborate the channels of one execution cycle in parallel, the log
 *       must keep the sequence of messages of the single threaded execution. A
 *       worker therefore queues the messages of its own channel, and the main
 *       thread replays them in the order of the channel index, before the
 *       transitions of the state machine of that channel (see
 *       receiver/channelManager.c).
 *       The queue is embedded in the channel and sized at compile time: no
 *       dynamic allocation occurs during the execution (see utility/logCapture.c).
 */
#ifndef UTILITY_LOG_CAPTURE_H_INCLUDED
#define UTILITY_LOG_CAPTURE_H_INCLUDED
#include "ngeneConstants.h"
#include "xmmsBackup.h"
/** @brief Maximum number of messages one channel elaboration can produce. */
#define LOG_CAPTURE_MAXIMUM_MESSAGES 32
/** @brief Size of one queued message, including the terminating character. */
#define LOG_CAPTURE_MESSAGE_SIZE (MAX_ROW_LENGTH + 1)
/**
 * @struct LogCaptureStruct
 *
 * @brief Queue of the messages deferred by one channel elaboration.
 */
typedef struct LogCaptureStruct {
    int severities[LOG_CAPTURE_MAXIMUM_MESSAGES];
    /**< severity of each queued message, oldest first (see SwrxLog) */
    char messages[LOG_CAPTURE_MAXIMUM_MESSAGES][LOG_CAPTURE_MESSAGE_SIZE];
    /**< text of each queued message, already rendered by SwrxLog */
    unsigned int messageCount; /**< number of messages currently queued */
    unsigned int overflowCount; /**< messages emitted instead of queued, queue was full */
} TLogCapture;
/** @brief Capture target of the calling thread, NULL when the log is immediate. */
extern NGENE_THREAD_LOCAL TLogCapture *ngeneLogCaptureTarget;
/**
 * @brief InitLogCapture empties the queue of deferred messages of one channel.
 * @details The queue is embedded in the channel structure: this function only resets it,
 *          so no memory is allocated during the execution (see TChannel in
 *          receiver/channelDataStructure.h).
 * @param capture is the queue belonging to one channel
 */
void InitLogCapture(TLogCapture *capture);
/**
 * @brief BeginLogCapture diverts the log of the calling thread to a queue.
 * @details While the target is set, SwrxLog() appends to the queue instead of printing
 *          (see SwrxLog in utility/ioFunctions.c).
 * @param capture is the queue receiving the messages
 */
void BeginLogCapture(TLogCapture *capture);
/**
 * @brief EndLogCapture restores the immediate log of the calling thread.
 */
void EndLogCapture(void);
/**
 * @brief AppendLogCaptureEntry queues one message (called internally by SwrxLog).
 * @param severity is the severity of the message (see SwrxLog)
 * @param message is the message already rendered by SwrxLog
 * @return SUCCESS_RETURN when the message is queued, FAILURE_RETURN when the queue is
 *         full, in which case SwrxLog() emits the message as usual
 */
int AppendLogCaptureEntry(int severity, const char *message);
/**
 * @brief FlushLogCapture replays the queued messages in the order they were produced.
 * @param capture is the queue belonging to one channel
 * @return SUCCESS_RETURN when the function ends with success, FAILURE_RETURN elsewhere
 */
int FlushLogCapture(TLogCapture *capture);
#endif /* UTILITY_LOG_CAPTURE_H_INCLUDED */