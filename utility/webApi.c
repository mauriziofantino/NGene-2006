/**
 * @file webApi.c
 *
 * @author Maurizio Fantino
 *
 * @date 19/11/2026
 *
 * @brief Implementation of the web user interface: listener, answers, snapshots, commands.
 *
 * @note The functions answering the browser run on the thread started by InitWebApi(). They copy
 *       the payloads published by the main thread, under the mutex of the interface, and they never
 *       read the state of a channel nor the state of the position, and never write it.
 * @note The commands asked in a page are only queued here. ExecuteWebApiCommands() applies them on
 *       the main thread, at the beginning of an execution cycle, so no command can be applied while
 *       the channels of a cycle are elaborated (see receiver/ngene.c).
 */
#ifdef NGENE_NATIVE_WINDOWS
/** @brief WinSock needs an initialization before the first socket call (see platform/windows). */
#define NGENE_SOCKET_TYPE SOCKET
/** @brief Value of a socket that was not opened, on WinSock. */
#define NGENE_SOCKET_INVALID INVALID_SOCKET
/** @brief Name used to close one socket, on WinSock. */
#define NGENE_SOCKET_CLOSE(socketHandle) closesocket(socketHandle)
/** @brief Call initializing the socket layer, present only on Windows (see platform/windows). */
#define NGENE_SOCKET_STARTUP() NgSocketInit()
/** @brief Read of the request, one call at a time on WinSock. The helper of the compatibility layer
 *         accumulates the fragments until the whole requested amount arrives, which would hold the
 *         thread of the interface on a request whose body is shorter than the buffer asked for, as
 *         the answers of the interface close the connection and the pages send no body (see
 *         platform/windows/native_compat.c). */
#define NGENE_SOCKET_READ(socketHandle, buffer, amount) \
    (ssize_t)recv(socketHandle, (char *)(buffer), (int)(amount), 0)
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
/** @brief Type of a socket, on the BSD sockets of Linux. */
#define NGENE_SOCKET_TYPE int
/** @brief Value of a socket that was not opened, on the BSD sockets of Linux. */
#define NGENE_SOCKET_INVALID (-1)
/** @brief Name used to close one socket, on the BSD sockets of Linux. */
#define NGENE_SOCKET_CLOSE(socketHandle) close(socketHandle)
/** @brief There is nothing to start on Linux, so the macro is empty. */
#define NGENE_SOCKET_STARTUP() (0)
/** @brief Read of the request, on the BSD sockets of Linux. */
#define NGENE_SOCKET_READ(socketHandle, buffer, amount) \
    (ssize_t)recv(socketHandle, buffer, (size_t)(amount), 0)
#endif
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <pthread.h>
#include "ngeneConstants.h"
#include "ngeneDataStructure.h"
#include "ioFunctions.h"
#include "channelDataStructure.h"
#include "channelManager.h"
#include "trackingDataStructure.h"
#include "pvtDataStructure.h"
#include "dop.h"
#include "webApiDataStructure.h"
#include "webApiPages.h"
#include "webApi.h"
/** @brief Content type of the pages of the interface. */
#define WEB_UI_TYPE_HTML "text/html; charset=utf-8"
/** @brief Content type of the status documents of the interface. */
#define WEB_UI_TYPE_JSON "application/json; charset=utf-8"
/** @brief Rows of the page of the log, as asked by the browser. */
#define WEB_UI_LOG_ANSWER_ROWS 24
/**
 * @brief AppendWebText appends text to an answer, stopping at the capacity of the answer.
 * @param buffer is the answer under construction
 * @param capacity is the size of the buffer above, in characters
 * @param usedCount points to the number of characters used so far
 * @param text is the text to append
 * @return the number of characters written, that is 0 when the text does not fit
 */
static unsigned int AppendWebText(char *buffer, unsigned int capacity, unsigned int *usedCount,
                                  const char *text)
{
    unsigned int textLength;
    unsigned int available;
    textLength = (unsigned int)strlen(text);
    if (*usedCount >= capacity) {
        return 0;
    }
    available = capacity - *usedCount;
    if (textLength >= available) {
        /* the answer is truncated on purpose, so the browser receives a partial document rather
         * than the buffer over its capacity (see utility/webApiDataStructure.h) */
        textLength = available - 1;
    }
    memcpy(&(buffer[*usedCount]), text, (size_t)(textLength));
    *usedCount += textLength;
    buffer[*usedCount] = '\0';
    return textLength;
}
/**
 * @brief AppendWebJsonString appends one string to a JSON document, escaping what JSON needs.
 * @param buffer is the answer under construction
 * @param capacity is the size of the buffer above, in characters
 * @param usedCount points to the number of characters used so far
 * @param text is the string to append, without the quotes of JSON
 * @return SUCCESS_RETURN when the string was appended, FAILURE_RETURN when it was truncated
 */
static int AppendWebJsonString(char *buffer, unsigned int capacity, unsigned int *usedCount,
                               const char *text)
{
    unsigned int index;
    char written[2];
    int escaped;
    written[1] = '\0';
    AppendWebText(buffer, capacity, usedCount, "\"");
    for (index = 0; text[index] != '\0'; index++) {
        escaped = FALSE_FLAG;
        if (text[index] == '"') {
            AppendWebText(buffer, capacity, usedCount, "\\\"");
            escaped = TRUE_FLAG;
        } else if (text[index] == '\\') {
            AppendWebText(buffer, capacity, usedCount, "\\\\");
            escaped = TRUE_FLAG;
        } else if (text[index] == '\n') {
            AppendWebText(buffer, capacity, usedCount, "\\n");
            escaped = TRUE_FLAG;
        } else if (text[index] == '\r' || text[index] == '\t') {
            escaped = TRUE_FLAG;
        }
        if (escaped == FALSE_FLAG) {
            written[0] = text[index];
            AppendWebText(buffer, capacity, usedCount, written);
        }
    }
    AppendWebText(buffer, capacity, usedCount, "\"");
    return SUCCESS_RETURN;
}
/**
 * @brief AppendWebNumber appends one number to a JSON document.
 * @param buffer is the answer under construction
 * @param capacity is the size of the buffer above, in characters
 * @param usedCount points to the number of characters used so far
 * @param number is the value to append
 */
static void AppendWebNumber(char *buffer, unsigned int capacity, unsigned int *usedCount,
                            long long int number)
{
    char rendered[32];
    sprintf(rendered, "%lld", number);
    AppendWebText(buffer, capacity, usedCount, rendered);
}
/**
 * @brief AppendWebDouble appends one number with decimals to a JSON document.
 * @details A value that is not finite is written as null, so the page can distinguish the absence
 *          of an estimation from a value of zero (see tracking and pvt data structures).
 * @param buffer is the answer under construction
 * @param capacity is the size of the buffer above, in characters
 * @param usedCount points to the number of characters used so far
 * @param value is the value to append
 * @param decimals is the number of decimals to print
 */
static void AppendWebDouble(char *buffer, unsigned int capacity, unsigned int *usedCount,
                            double value, int decimals)
{
    char rendered[64];
    char format[32];
    sprintf(format, "%%.%df", decimals);
    if (!isfinite(value)) {
        AppendWebText(buffer, capacity, usedCount, "null");
        return;
    }
    sprintf(rendered, format, value);
    AppendWebText(buffer, capacity, usedCount, rendered);
}
/**
 * @brief GetWebApiCommandList answers the page asking which commands the interface accepts.
 * @return the list of commands as a JSON document, see ExecuteWebApiCommands()
 */
char *GetWebApiCommandList(void)
{
    return "{\"commands\":["
           "{\"name\":\"stop\",\"help\":\"ends the elaboration, as the failure of the front end does\"},"
           "{\"name\":\"pause\",\"help\":\"skips the execution cycles until resume\"},"
           "{\"name\":\"resume\",\"help\":\"elaborates the execution cycles again\"},"
           "{\"name\":\"restart-channels\",\"help\":\"releases the tracked satellites and restarts "
           "them, see receiver/channelManager.c\"},"
           "{\"name\":\"clear-log\",\"help\":\"empties the rows of the log kept for the page\"},"
           "{\"name\":\"verbose on\",\"help\":\"keeps also the NOTE messages in the rows\"},"
           "{\"name\":\"verbose off\",\"help\":\"keeps only the messages of severity above NOTE\"}]}";
}
/**
 * @brief BuildWebApiLogAnswer renders the rows of the log, most recent last.
 * @param web points to the state of the interface
 * @param body is the buffer receiving the answer
 * @return SUCCESS_RETURN always, the answer is truncated on purpose when it is long
 */
static int BuildWebApiLogAnswer(TWebApiData *web, char *body)
{
    unsigned int usedCount;
    unsigned int storedRows;
    unsigned int rowsToWrite;
    unsigned int rowCursor;
    unsigned int rowToWrite;
    unsigned int index;
    pthread_mutex_lock(&(web->stateMutex));
    usedCount = 0;
    body[0] = '\0';
    AppendWebText(body, WEB_UI_RESPONSE_BYTES, &(usedCount), "{\"rows\":[");
    storedRows = web->logRowStored;
    rowsToWrite = (storedRows < (unsigned int)WEB_UI_LOG_ANSWER_ROWS) ? storedRows : WEB_UI_LOG_ANSWER_ROWS;
    for (index = 0; index < rowsToWrite; index++) {
        /* the rows of the ring are traversed from the oldest one of the answer */
        rowCursor = (web->logRowNewest + (unsigned int)WEB_UI_LOG_ROWS - index - 1) %
                    (unsigned int)WEB_UI_LOG_ROWS;
        if (web->logRows[rowCursor][0] == '\0')
            continue;
        rowToWrite = rowCursor;
        AppendWebText(body, WEB_UI_RESPONSE_BYTES, &(usedCount), "{\"severity\":");
        AppendWebNumber(body, WEB_UI_RESPONSE_BYTES, &(usedCount),
                        (long long int)(web->logRowSeverity[rowToWrite]));
        AppendWebText(body, WEB_UI_RESPONSE_BYTES, &(usedCount), ",\"text\":");
        AppendWebJsonString(body, WEB_UI_RESPONSE_BYTES, &(usedCount),
                            &(web->logRows[rowToWrite][0]));
        AppendWebText(body, WEB_UI_RESPONSE_BYTES, &(usedCount), "},");
    }
    if (usedCount > 0) {
        /* the separator left by the last row written is given back, so the closing brackets below
         * overwrite it and the document of the log is closed (see AppendWebText) */
        if (body[usedCount - 1] == ',')
            usedCount--;
    }
    AppendWebText(body, WEB_UI_RESPONSE_BYTES, &(usedCount), "]}");
    pthread_mutex_unlock(&(web->stateMutex));
    return SUCCESS_RETURN;
}
/**
 * @brief ParseWebApiRequest extracts the method, the path and the query of one request line.
 * @return SUCCESS_RETURN when a request line was found, FAILURE_RETURN otherwise
 */
int ParseWebApiRequest(char *request, char *method, char *path, char *query)
{
    unsigned int requestIndex;
    unsigned int methodIndex;
    unsigned int pathIndex;
    unsigned int queryIndex;
    int stage;
    if (request == NULL || method == NULL || path == NULL || query == NULL)
        return FAILURE_RETURN;
    method[0] = '\0';
    path[0] = '\0';
    query[0] = '\0';
    requestIndex = 0;
    methodIndex = 0;
    pathIndex = 0;
    queryIndex = 0;
    /* stage 0 reads the method, stage 1 the path, stage 2 the query, stage 3 stops at the version */
    stage = 0;
    while (requestIndex < (unsigned int)WEB_UI_REQUEST_BYTES && request[requestIndex] != '\0') {
        if (request[requestIndex] == '\r' || request[requestIndex] == '\n')
            break;
        if (stage == 0) {
            if (request[requestIndex] == ' ')
                stage = 1;
            else {
                if (methodIndex < 7) {
                    method[methodIndex] = request[requestIndex];
                    methodIndex++;
                    method[methodIndex] = '\0';
                }
            }
        } else if (stage == 1) {
            if (request[requestIndex] == '?')
                stage = 2;
            else if (request[requestIndex] == ' ')
                stage = 3;
            else if (pathIndex < (unsigned int)(WEB_UI_COMMAND_LENGTH - 1)) {
                path[pathIndex] = request[requestIndex];
                pathIndex++;
                path[pathIndex] = '\0';
            }
        } else if (stage == 2) {
            if (request[requestIndex] == ' ')
                stage = 3;
            else if (queryIndex < (unsigned int)(WEB_UI_COMMAND_LENGTH - 1)) {
                query[queryIndex] = request[requestIndex];
                queryIndex++;
                query[queryIndex] = '\0';
            }
        } else {
            break;
        }
        requestIndex++;
    }
    if (method[0] == '\0' || path[0] == '\0')
        return FAILURE_RETURN;
    return SUCCESS_RETURN;
}
/**
 * @brief BuildWebApiHistoryAnswer renders the graph of the elaborations of one channel.
 * @details The channel is selected by the query of the request, as history.json?channel=2 (see
 *          utility/webApi.c). Points are given by the input sample of the elaboration, so the page
 *          can draw the Doppler and the correlation against the sample position.
 * @param web points to the state of the interface
 * @param query is the query of the request
 * @return SUCCESS_RETURN when the channel asked exists, FAILURE_RETURN otherwise
 */
static int BuildWebApiHistoryAnswer(TWebApiData *web, char *query, char *body)
{
    int askedChannel;
    unsigned int usedCount;
    unsigned int storedPoints;
    unsigned int pointIndex;
    unsigned int slot;
    TWebApiChannelHistory *history;
    askedChannel = -1;
    if (query != NULL)
        sscanf(query, "channel=%d", &(askedChannel));
    if (askedChannel < 0 || askedChannel >= WEB_UI_CHANNELS_MAXIMUM)
        return FAILURE_RETURN;
    pthread_mutex_lock(&(web->stateMutex));
    history = &(web->histories[askedChannel]);
    usedCount = 0;
    body[0] = '\0';
    AppendWebText(body, WEB_UI_RESPONSE_BYTES, &(usedCount), "{\"channel\":");
    AppendWebNumber(body, WEB_UI_RESPONSE_BYTES, &(usedCount), (long long int)askedChannel);
    AppendWebText(body, WEB_UI_RESPONSE_BYTES, &(usedCount), ",\"points\":[");
    storedPoints = history->storedCount;
    for (pointIndex = 0; pointIndex < storedPoints; pointIndex++) {
        /* the ring of the history is traversed from the oldest point stored, see
         * TWebApiChannelHistoryStruct (see utility/webApiDataStructure.h) */
        slot = (unsigned int)((history->newestIndex +
                              (unsigned long long int)(WEB_UI_HISTORY_SAMPLES) -
                              (unsigned long long int)(storedPoints - pointIndex - 1)) %
                              (unsigned long long int)(WEB_UI_HISTORY_SAMPLES));
        AppendWebText(body, WEB_UI_RESPONSE_BYTES, &(usedCount), "[");
        AppendWebNumber(body, WEB_UI_RESPONSE_BYTES, &(usedCount),
                        (long long int)(history->sample[slot]));
        AppendWebText(body, WEB_UI_RESPONSE_BYTES, &(usedCount), ",");
        AppendWebDouble(body, WEB_UI_RESPONSE_BYTES, &(usedCount), history->doppler[slot], 3);
        AppendWebText(body, WEB_UI_RESPONSE_BYTES, &(usedCount), ",");
        AppendWebDouble(body, WEB_UI_RESPONSE_BYTES, &(usedCount), history->correlation[slot], 1);
        AppendWebText(body, WEB_UI_RESPONSE_BYTES, &(usedCount), "],");
    }
    if (usedCount > 0 && body[usedCount - 1] == ',') {
        /* the separator of the last point is given back, so the closing brackets below overwrite it
         * (see AppendWebText) */
        usedCount--;
    }
    AppendWebText(body, WEB_UI_RESPONSE_BYTES, &(usedCount), "]}");
    pthread_mutex_unlock(&(web->stateMutex));
    return SUCCESS_RETURN;
}
/**
 * @brief HandleWebApiRequest builds the body of the answer for one path of the interface.
 * @return SUCCESS_RETURN when the path is known, FAILURE_RETURN otherwise
 */
int HandleWebApiRequest(TWebApiData *web, char *path, char *query, char *contentType, char *body)
{
    unsigned int usedCount;
    if (web == NULL || path == NULL || contentType == NULL || body == NULL)
        return FAILURE_RETURN;
    usedCount = 0;
    body[0] = '\0';
    if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
        memcpy(contentType, WEB_UI_TYPE_HTML, strlen(WEB_UI_TYPE_HTML) + 1);
        AppendWebText(body, WEB_UI_RESPONSE_BYTES, &(usedCount), WEB_UI_PAGE_MAIN);
        return SUCCESS_RETURN;
    }
    if (strcmp(path, "/channels") == 0) {
        memcpy(contentType, WEB_UI_TYPE_HTML, strlen(WEB_UI_TYPE_HTML) + 1);
        AppendWebText(body, WEB_UI_RESPONSE_BYTES, &(usedCount), WEB_UI_PAGE_CHANNELS);
        return SUCCESS_RETURN;
    }
    if (strcmp(path, "/pvt") == 0) {
        memcpy(contentType, WEB_UI_TYPE_HTML, strlen(WEB_UI_TYPE_HTML) + 1);
        AppendWebText(body, WEB_UI_RESPONSE_BYTES, &(usedCount), WEB_UI_PAGE_PVT);
        return SUCCESS_RETURN;
    }
    if (strcmp(path, "/status/channels.json") == 0) {
        memcpy(contentType, WEB_UI_TYPE_JSON, strlen(WEB_UI_TYPE_JSON) + 1);
        pthread_mutex_lock(&(web->stateMutex));
        /* the scalars of the document are written here, so the page sees the flag of the pause even
         * while the elaboration of the channels is stopped and no row is published (see
         * PublishWebApiChannels) */
        AppendWebText(body, WEB_UI_RESPONSE_BYTES, &(usedCount), "{\"cycles\":");
        AppendWebNumber(body, WEB_UI_RESPONSE_BYTES, &(usedCount),
                        (long long int)(web->elaborationCount));
        AppendWebText(body, WEB_UI_RESPONSE_BYTES, &(usedCount), ",\"paused\":");
        AppendWebNumber(body, WEB_UI_RESPONSE_BYTES, &(usedCount),
                        (long long int)(web->pausedFlag));
        AppendWebText(body, WEB_UI_RESPONSE_BYTES, &(usedCount), ",\"channels\":");
        AppendWebText(body, WEB_UI_RESPONSE_BYTES, &(usedCount), web->channelsRows);
        AppendWebText(body, WEB_UI_RESPONSE_BYTES, &(usedCount), "}");
        pthread_mutex_unlock(&(web->stateMutex));
        return SUCCESS_RETURN;
    }
    if (strcmp(path, "/status/pvt.json") == 0) {
        memcpy(contentType, WEB_UI_TYPE_JSON, strlen(WEB_UI_TYPE_JSON) + 1);
        pthread_mutex_lock(&(web->stateMutex));
        AppendWebText(body, WEB_UI_RESPONSE_BYTES, &(usedCount), web->pvtPayload);
        pthread_mutex_unlock(&(web->stateMutex));
        return SUCCESS_RETURN;
    }
    if (strcmp(path, "/status/log.json") == 0) {
        memcpy(contentType, WEB_UI_TYPE_JSON, strlen(WEB_UI_TYPE_JSON) + 1);
        BuildWebApiLogAnswer(web, body);
        return SUCCESS_RETURN;
    }
    if (strcmp(path, "/status/history.json") == 0) {
        memcpy(contentType, WEB_UI_TYPE_JSON, strlen(WEB_UI_TYPE_JSON) + 1);
        if (BuildWebApiHistoryAnswer(web, query, body) == FAILURE_RETURN) {
            memcpy(body, "{\"points\":[]}", 13);
            return FAILURE_RETURN;
        }
        return SUCCESS_RETURN;
    }
    if (strcmp(path, "/status/commands.json") == 0) {
        memcpy(contentType, WEB_UI_TYPE_JSON, strlen(WEB_UI_TYPE_JSON) + 1);
        AppendWebText(body, WEB_UI_RESPONSE_BYTES, &(usedCount), GetWebApiCommandList());
        return SUCCESS_RETURN;
    }
    return FAILURE_RETURN;
}
/**
 * @brief GetWebApiChannelStateName renders one state of the state machine as one word.
 * @param channelState is the state of the channel, see receiver/channelDataStructure.h
 * @return the name of the state, as the page of the channels shows it
 */
static const char *GetWebApiChannelStateName(int channelState)
{
    switch (channelState) {
    case POWERON_STATE:
        return "power-on";
    case COARSE_ACQUISITION_STATE:
        return "coarse";
    case CODE_REFINEMENT_STATE:
        return "code refinement";
    case DOPPLER_REFINEMENT_STATE:
        return "doppler refinement";
    case CONFIRMATION_STATE:
        return "confirmation";
    case TRACKING_STATE:
        return "tracking";
    default:
        return "unused";
    }
}
/**
 * @brief GetWebApiTrackingStatusName renders one status of the tracking loops as one word.
 * @param trkStatus is the status of the tracking, see tracking/trackingDataStructure.h
 * @return the name of the status, as the page of the channels shows it
 */
static const char *GetWebApiTrackingStatusName(int trkStatus)
{
    switch (trkStatus) {
    case TRACKING_LOCKED:
        return "locked";
    case TRACKING_LOST:
        return "lost";
    case TRACKING_INITIAL_TRANSIENT:
        return "transient";
    case TRACKING_PLL_CHECK_FAILED:
        return "PLL check failed";
    case TRACKING_CN0_CHECK_FAILED:
        return "CNo check failed";
    case TRACKING_PLL_FALSE_LOCK_CHECK:
        return "false lock";
    default:
        return "-";
    }
}
/**
 * @brief PublishWebApiChannels snapshots the state of the channels of one execution cycle.
 */
void PublishWebApiChannels(TNGeneConfiguration *cfg, TNGeneData *rx)
{
    TWebApiData *web = &(rx->webApiData);
    unsigned int usedCount;
    int channelIndex;
    int channelCount;
    TChannel *ch;
    TTracking *trk;
    unsigned int slot;
    TWebApiChannelHistory *history;
    if (web->enabled == FALSE_FLAG || web->stateInitialized == FALSE_FLAG)
        return;
    channelCount = cfg->numberOfChannels;
    if (channelCount > WEB_UI_CHANNELS_MAXIMUM)
        channelCount = WEB_UI_CHANNELS_MAXIMUM;
    pthread_mutex_lock(&(web->stateMutex));
    usedCount = 0;
    /* the rows of the channels are published as an array: the scalars of the document are written by
     * the answer below, where the flag of the pause is read at the moment the browser asks for it (see
     * HandleWebApiRequest) */
    AppendWebText(web->channelsRows, WEB_UI_RESPONSE_BYTES, &(usedCount), "[");
    for (channelIndex = 0; channelIndex < channelCount; channelIndex++) {
        ch = &(rx->channelArray[channelIndex]);
        trk = &(ch->tracking);
        AppendWebText(web->channelsRows, WEB_UI_RESPONSE_BYTES, &(usedCount), "{");
        AppendWebText(web->channelsRows, WEB_UI_RESPONSE_BYTES, &(usedCount), "\"index\":");
        AppendWebNumber(web->channelsRows, WEB_UI_RESPONSE_BYTES, &(usedCount),
                        (long long int)(channelIndex));
        AppendWebText(web->channelsRows, WEB_UI_RESPONSE_BYTES, &(usedCount), ",\"name\":");
        AppendWebJsonString(web->channelsRows, WEB_UI_RESPONSE_BYTES, &(usedCount),
                            ch->prnName);
        AppendWebText(web->channelsRows, WEB_UI_RESPONSE_BYTES, &(usedCount), ",\"state\":");
        AppendWebJsonString(web->channelsRows, WEB_UI_RESPONSE_BYTES, &(usedCount),
                            GetWebApiChannelStateName(ch->chState));
        AppendWebText(web->channelsRows, WEB_UI_RESPONSE_BYTES, &(usedCount), ",\"tracking\":");
        AppendWebJsonString(web->channelsRows, WEB_UI_RESPONSE_BYTES, &(usedCount),
                            GetWebApiTrackingStatusName(trk->trkStatus));
        AppendWebText(web->channelsRows, WEB_UI_RESPONSE_BYTES, &(usedCount), ",\"doppler\":");
        AppendWebDouble(web->channelsRows, WEB_UI_RESPONSE_BYTES, &(usedCount), ch->dopplerShift,
                        3);
        AppendWebText(web->channelsRows, WEB_UI_RESPONSE_BYTES, &(usedCount), ",\"carrier\":");
        AppendWebDouble(web->channelsRows, WEB_UI_RESPONSE_BYTES, &(usedCount),
                        ch->carrierFrequency, 3);
        AppendWebText(web->channelsRows, WEB_UI_RESPONSE_BYTES, &(usedCount), ",\"prompt\":");
        AppendWebDouble(web->channelsRows, WEB_UI_RESPONSE_BYTES, &(usedCount),
                        (double)(trk->pIForCno), 1);
        AppendWebText(web->channelsRows, WEB_UI_RESPONSE_BYTES, &(usedCount), ",\"usedForPvt\":");
        AppendWebNumber(web->channelsRows, WEB_UI_RESPONSE_BYTES, &(usedCount),
                        (long long int)(ch->pvtInfo.usedForPVT));
        AppendWebText(web->channelsRows, WEB_UI_RESPONSE_BYTES, &(usedCount), ",\"trkCounter\":");
        /* the counter of the tracking loops is published only while the channel elaborates the
         * samples in closed loop, as the field keeps no meaning before the tracking of the channel
         * begins (see tracking/trackingDataStructure.h) */
        if (ch->chState == TRACKING_STATE) {
            AppendWebNumber(web->channelsRows, WEB_UI_RESPONSE_BYTES, &(usedCount),
                            (long long int)(trk->trkCounter));
        } else {
            AppendWebNumber(web->channelsRows, WEB_UI_RESPONSE_BYTES, &(usedCount),
                            (long long int)0);
        }
        AppendWebText(web->channelsRows, WEB_UI_RESPONSE_BYTES, &(usedCount),
                      ",\"sample\":");
        AppendWebNumber(web->channelsRows, WEB_UI_RESPONSE_BYTES, &(usedCount),
                        (long long int)(rx->buf.discardedSamples + ch->bufferOffset + ch->blocksize));
        AppendWebText(web->channelsRows, WEB_UI_RESPONSE_BYTES, &(usedCount), "},");
        /* the graph of the channel gains one point per elaboration, see the history of the
         * interface (see utility/webApiDataStructure.h) */
        history = &(web->histories[channelIndex]);
        if (history->storedCount < (unsigned int)WEB_UI_HISTORY_SAMPLES) {
            slot = history->storedCount;
            history->storedCount++;
        } else {
            slot = (unsigned int)((history->newestIndex + 1) %
                                  (unsigned long long int)WEB_UI_HISTORY_SAMPLES);
        }
        history->newestIndex = (unsigned long long int)slot;
        history->sample[slot] = rx->buf.discardedSamples + ch->bufferOffset + ch->blocksize;
        history->doppler[slot] = ch->dopplerShift;
        history->correlation[slot] = (double)(trk->pIForCno);
        history->channelState[slot] = ch->chState;
    }
    if (usedCount > 0 && web->channelsRows[usedCount - 1] == ',') {
        /* the separator of the last channel is given back, so the bracket closing the array of the
         * rows overwrites it (see AppendWebText) */
        usedCount--;
    }
    AppendWebText(web->channelsRows, WEB_UI_RESPONSE_BYTES, &(usedCount), "]");
    web->elaborationCount++;
    pthread_mutex_unlock(&(web->stateMutex));
}
/**
 * @brief PublishWebApiPvt snapshots the result of the position, velocity and time estimation.
 */
void PublishWebApiPvt(TNGeneConfiguration *cfg, TNGeneData *rx)
{
    TWebApiData *web = &(rx->webApiData);
    TPvtResults *pvtResults;
    unsigned int usedCount;
    double speed;
    int channelIndex;
    int usedForPvtCount;
    if (web->enabled == FALSE_FLAG || web->stateInitialized == FALSE_FLAG)
        return;
    pvtResults = &(rx->pvtResults);
    usedForPvtCount = 0;
    for (channelIndex = 0; channelIndex < cfg->numberOfChannels; channelIndex++) {
        if (rx->channelArray[channelIndex].pvtInfo.usedForPVT == TRUE_FLAG)
            usedForPvtCount++;
    }
    speed = sqrt(pvtResults->ecefVelocity[0] * pvtResults->ecefVelocity[0] +
                 pvtResults->ecefVelocity[1] * pvtResults->ecefVelocity[1] +
                 pvtResults->ecefVelocity[2] * pvtResults->ecefVelocity[2]);
    pthread_mutex_lock(&(web->stateMutex));
    usedCount = 0;
    AppendWebText(web->pvtPayload, (unsigned int)sizeof(web->pvtPayload), &(usedCount),
                  "{\"valid\":");
    AppendWebNumber(web->pvtPayload, (unsigned int)sizeof(web->pvtPayload), &(usedCount),
                    (long long int)(pvtResults->pvtValid));
    AppendWebText(web->pvtPayload, (unsigned int)sizeof(web->pvtPayload), &(usedCount), ",\"lat\":");
    AppendWebDouble(web->pvtPayload, (unsigned int)sizeof(web->pvtPayload), &(usedCount),
                    pvtResults->geodPosition[1], 7);
    AppendWebText(web->pvtPayload, (unsigned int)sizeof(web->pvtPayload), &(usedCount), ",\"lon\":");
    AppendWebDouble(web->pvtPayload, (unsigned int)sizeof(web->pvtPayload), &(usedCount),
                    pvtResults->geodPosition[0], 7);
    AppendWebText(web->pvtPayload, (unsigned int)sizeof(web->pvtPayload), &(usedCount), ",\"alt\":");
    AppendWebDouble(web->pvtPayload, (unsigned int)sizeof(web->pvtPayload), &(usedCount),
                    pvtResults->geodPosition[2], 2);
    AppendWebText(web->pvtPayload, (unsigned int)sizeof(web->pvtPayload), &(usedCount),
                  ",\"speed\":");
    AppendWebDouble(web->pvtPayload, (unsigned int)sizeof(web->pvtPayload), &(usedCount), speed, 2);
    AppendWebText(web->pvtPayload, (unsigned int)sizeof(web->pvtPayload), &(usedCount),
                  ",\"biasGps\":");
    AppendWebDouble(web->pvtPayload, (unsigned int)sizeof(web->pvtPayload), &(usedCount),
                    pvtResults->clockBiasGps, 3);
    AppendWebText(web->pvtPayload, (unsigned int)sizeof(web->pvtPayload), &(usedCount),
                  ",\"biasGalileo\":");
    AppendWebDouble(web->pvtPayload, (unsigned int)sizeof(web->pvtPayload), &(usedCount),
                    pvtResults->clockBiasGalileo, 3);
    AppendWebText(web->pvtPayload, (unsigned int)sizeof(web->pvtPayload), &(usedCount),
                  ",\"drift\":");
    AppendWebDouble(web->pvtPayload, (unsigned int)sizeof(web->pvtPayload), &(usedCount),
                    pvtResults->clockDrift, 4);
    AppendWebText(web->pvtPayload, (unsigned int)sizeof(web->pvtPayload), &(usedCount),
                  ",\"gdop\":");
    AppendWebDouble(web->pvtPayload, (unsigned int)sizeof(web->pvtPayload), &(usedCount),
                    pvtResults->dop[GDOP_INDEX_IN_DOP_ARRAY], 2);
    AppendWebText(web->pvtPayload, (unsigned int)sizeof(web->pvtPayload), &(usedCount),
                  ",\"usedForPvt\":");
    AppendWebNumber(web->pvtPayload, (unsigned int)sizeof(web->pvtPayload), &(usedCount),
                    (long long int)(usedForPvtCount));
    AppendWebText(web->pvtPayload, (unsigned int)sizeof(web->pvtPayload), &(usedCount), ",\"sample\":");
    AppendWebNumber(web->pvtPayload, (unsigned int)sizeof(web->pvtPayload), &(usedCount),
                    (long long int)(pvtResults->referenceSample));
    AppendWebText(web->pvtPayload, (unsigned int)sizeof(web->pvtPayload), &(usedCount), "}");
    pthread_mutex_unlock(&(web->stateMutex));
}
/**
 * @brief DrawWebApiLog keeps one line of the log for the page of the interface.
 */
int DrawWebApiLog(TWebApiData *web, char *logString, int severity)
{
    unsigned int rowToWrite;
    if (web == NULL || logString == NULL || web->stateInitialized == FALSE_FLAG)
        return FAILURE_RETURN;
    if (severity == NGENE_LOG_SEVERITY_NOTE && web->verboseFlag == FALSE_FLAG)
        return FAILURE_RETURN;
    if (strlen(logString) >= (size_t)WEB_UI_LOG_ROW_LENGTH)
        return FAILURE_RETURN;
    pthread_mutex_lock(&(web->stateMutex));
    rowToWrite = web->logRowNewest;
    memcpy(&(web->logRows[rowToWrite][0]), logString, strlen(logString) + 1);
    web->logRowSeverity[rowToWrite] = severity;
    web->logRowNewest = (web->logRowNewest + 1) % (unsigned int)WEB_UI_LOG_ROWS;
    if (web->logRowStored < (unsigned int)WEB_UI_LOG_ROWS)
        web->logRowStored++;
    pthread_mutex_unlock(&(web->stateMutex));
    return SUCCESS_RETURN;
}
/**
 * @brief EnqueueWebApiCommand stores a command asked by the browser.
 */
int EnqueueWebApiCommand(TWebApiData *web, char *command)
{
    if (web == NULL || command == NULL || web->stateInitialized == FALSE_FLAG)
        return FAILURE_RETURN;
    if (strlen(command) == 0 || strlen(command) >= (size_t)WEB_UI_COMMAND_LENGTH)
        return FAILURE_RETURN;
    pthread_mutex_lock(&(web->stateMutex));
    if (web->commandCount >= (unsigned int)WEB_UI_COMMANDS_MAXIMUM) {
        pthread_mutex_unlock(&(web->stateMutex));
        return FAILURE_RETURN;
    }
    memcpy(&(web->commandQueue[web->commandCount].text[0]), command, strlen(command) + 1);
    web->commandCount++;
    pthread_mutex_unlock(&(web->stateMutex));
    return SUCCESS_RETURN;
}
/**
 * @brief TakeWebApiCommands moves the commands queued by the browser out of the queue.
 */
unsigned int TakeWebApiCommands(TWebApiData *web, char taken[][WEB_UI_COMMAND_LENGTH])
{
    unsigned int takenCount;
    unsigned int commandIndex;
    if (web->stateInitialized == FALSE_FLAG)
        return 0;
    pthread_mutex_lock(&(web->stateMutex));
    takenCount = web->commandCount;
    for (commandIndex = 0; commandIndex < takenCount; commandIndex++) {
        memcpy(&(taken[commandIndex][0]), &(web->commandQueue[commandIndex].text[0]),
               (size_t)WEB_UI_COMMAND_LENGTH);
    }
    web->commandCount = 0;
    pthread_mutex_unlock(&(web->stateMutex));
    return takenCount;
}
/**
 * @brief ApplyWebApiCommand applies one command asked in a page of the interface.
 */
int ApplyWebApiCommand(TWebApiData *web, TNGeneConfiguration *cfg, TNGeneData *rx, char *command)
{
    int rowIndex;
    if (strcmp(command, "stop") == 0) {
        SwrxLog(NGENE_LOG_SEVERITY_WARNING,
                "Receiver stopped on the request of the web interface (see utility/webApi.c)");
        return TRUE_FLAG;
    }
    if (strcmp(command, "pause") == 0) {
        web->pausedFlag = TRUE_FLAG;
        SwrxLog(NGENE_LOG_SEVERITY_WARNING, "Elaboration of the channels paused on the request of the "
                                            "web interface");
        return FALSE_FLAG;
    }
    if (strcmp(command, "resume") == 0) {
        web->pausedFlag = FALSE_FLAG;
        SwrxLog(NGENE_LOG_SEVERITY_WARNING, "Elaboration of the channels resumed on the request of the "
                                            "web interface");
        return FALSE_FLAG;
    }
    if (strcmp(command, "restart-channels") == 0) {
        /* the function of the receiver is called on the main thread only, as the transitions of the
         * state machine of the channels are taken there (see receiver/channelManager.c) */
        HardRestartChannels(cfg, rx);
        SwrxLog(NGENE_LOG_SEVERITY_WARNING, "Channels restarted on the request of the web interface");
        return FALSE_FLAG;
    }
    if (strcmp(command, "clear-log") == 0) {
        pthread_mutex_lock(&(web->stateMutex));
        web->logRowStored = 0;
        web->logRowNewest = 0;
        for (rowIndex = 0; rowIndex < WEB_UI_LOG_ROWS; rowIndex++)
            web->logRows[rowIndex][0] = '\0';
        pthread_mutex_unlock(&(web->stateMutex));
        return FALSE_FLAG;
    }
    if (strcmp(command, "verbose on") == 0) {
        web->verboseFlag = TRUE_FLAG;
        return FALSE_FLAG;
    }
    if (strcmp(command, "verbose off") == 0) {
        web->verboseFlag = FALSE_FLAG;
        return FALSE_FLAG;
    }
    SwrxLog(NGENE_LOG_SEVERITY_WARNING, "Command not offered by the web interface: %s", command);
    return FALSE_FLAG;
}
/**
 * @brief ExecuteWebApiCommands drains the queue and applies every command of the interface.
 */
int ExecuteWebApiCommands(TNGeneConfiguration *cfg, TNGeneData *rx)
{
    TWebApiData *web = &(rx->webApiData);
    char taken[WEB_UI_COMMANDS_MAXIMUM][WEB_UI_COMMAND_LENGTH];
    unsigned int takenCount;
    unsigned int commandIndex;
    int mustStop;
    takenCount = TakeWebApiCommands(web, taken);
    mustStop = FALSE_FLAG;
    for (commandIndex = 0; commandIndex < takenCount; commandIndex++) {
        if (ApplyWebApiCommand(web, cfg, rx, taken[commandIndex]) == TRUE_FLAG)
            mustStop = TRUE_FLAG;
    }
    if (mustStop == TRUE_FLAG)
        web->stopElaborationFlag = TRUE_FLAG;
    return mustStop;
}
/**
 * @brief DecodeWebQueryText decodes the text of one command of the page.
 * @details The page sends the command as the query of a POST request, as text=stop, so the percent
 *          escapes and the plus sign are converted back here (see utility/webApi.c).
 * @param query is the query of the request
 * @param decoded is the buffer receiving the command, see WEB_UI_COMMAND_LENGTH
 * @return the number of characters decoded
 */
static unsigned int DecodeWebQueryText(char *query, char *decoded)
{
    unsigned int decodedCount;
    unsigned int queryIndex;
    unsigned int firstDigit;
    unsigned int secondDigit;
    decodedCount = 0;
    queryIndex = 0;
    if (query == NULL || decoded == NULL)
        return 0;
    /* skip the name of the query field, as the page uses text= */
    while (query[queryIndex] != '\0' && query[queryIndex] != '=')
        queryIndex++;
    if (query[queryIndex] == '=')
        queryIndex++;
    while (query[queryIndex] != '\0' && decodedCount < (unsigned int)(WEB_UI_COMMAND_LENGTH - 1)) {
        if (query[queryIndex] == '%') {
            firstDigit = (unsigned int)((isalpha((int)(query[queryIndex + 1])) ?
                                             (tolower((int)(query[queryIndex + 1])) - 'a' + 10) :
                                             ((int)(query[queryIndex + 1]) - '0')));
            secondDigit = (unsigned int)((isalpha((int)(query[queryIndex + 2])) ?
                                              (tolower((int)(query[queryIndex + 2])) - 'a' + 10) :
                                              ((int)(query[queryIndex + 2]) - '0')));
            decoded[decodedCount] = (char)(firstDigit * 16 + secondDigit);
            queryIndex += 3;
        } else if (query[queryIndex] == '+') {
            decoded[decodedCount] = ' ';
            queryIndex++;
        } else {
            decoded[decodedCount] = query[queryIndex];
            queryIndex++;
        }
        decodedCount++;
    }
    decoded[decodedCount] = '\0';
    return decodedCount;
}
/**
 * @brief SendWebApiResponse writes one answer on the socket of the browser.
 * @details The answer closes the connection, so the browser opens a new socket at each poll of the
 *          page and the interface needs no buffering between the requests (see utility/webApi.c).
 * @param clientSocket is the socket of one request
 * @param statusText is the status line of the answer
 * @param contentType is the content type of the body
 * @param body is the body of the answer
 */
static void SendWebApiResponse(NGENE_SOCKET_TYPE clientSocket, char *statusText,
                               const char *contentType, char *body)
{
    char header[256];
    unsigned int bodyLength;
    unsigned int sentBytes;
    int writtenBytes;
    bodyLength = (unsigned int)strlen(body);
    sprintf(header, "HTTP/1.1 %s\r\n"
                    "Content-Type: %s\r\n"
                    "Cache-Control: no-store\r\n"
                    "Access-Control-Allow-Origin: *\r\n"
                    "Connection: close\r\n"
                    "Content-Length: %u\r\n\r\n",
            statusText, contentType, bodyLength);
    sentBytes = 0;
    while (sentBytes < (unsigned int)strlen(header)) {
        writtenBytes = send(clientSocket, &(header[sentBytes]), (int)(strlen(header) - sentBytes), 0);
        if (writtenBytes <= 0)
            return;
        sentBytes += (unsigned int)writtenBytes;
    }
    sentBytes = 0;
    while (sentBytes < bodyLength) {
        writtenBytes = send(clientSocket, &(body[sentBytes]), (int)(bodyLength - sentBytes), 0);
        if (writtenBytes <= 0)
            return;
        sentBytes += (unsigned int)writtenBytes;
    }
}
/**
 * @brief ServeWebApiRequests answers the browser until the receiver ends the execution.
 * @details The thread only reads the payloads published by the main thread and queues the commands
 *          asked in a page, so no state of the receiver is touched here (see utility/webApi.h).
 * @param webApiDataPointer points to the state of the interface, given by InitWebApi()
 * @return NULL always, as the thread of the receiver has no result to report
 */
static void *ServeWebApiRequests(void *webApiDataPointer)
{
    TWebApiData *web = (TWebApiData *)(webApiDataPointer);
    NGENE_SOCKET_TYPE listenerSocket;
    NGENE_SOCKET_TYPE clientSocket;
    char request[WEB_UI_REQUEST_BYTES];
    char method[8];
    char path[WEB_UI_COMMAND_LENGTH];
    char query[WEB_UI_COMMAND_LENGTH];
    char contentType[64];
    char body[WEB_UI_RESPONSE_BYTES];
    char commandText[WEB_UI_COMMAND_LENGTH];
    int stillServing;
    long long int readAmount;
    unsigned long long bytesRead;
    listenerSocket = (NGENE_SOCKET_TYPE)(uintptr_t)(web->listenerSocketHandle);
    while (TRUE_FLAG) {
        pthread_mutex_lock(&(web->stateMutex));
        stillServing = web->runningFlag;
        pthread_mutex_unlock(&(web->stateMutex));
        if (stillServing == FALSE_FLAG)
            break;
        /* the call below waits for one connection of the browser, so the thread of the interface
         * costs nothing between the polls of a page, which the pages do at about one request per
         * second (see WEB_UI_SCRIPT of utility/webApiPages.h). When the receiver asks the interface
         * to stop, DeinitWebApi opens one connection to the listener, so the waiting call returns and
         * the loop below ends (see DeinitWebApi) */
        clientSocket = accept(listenerSocket, NULL, NULL);
        if (clientSocket == NGENE_SOCKET_INVALID)
            continue;
        request[0] = '\0';
        bytesRead = 0;
        while (bytesRead < (unsigned long long)(WEB_UI_REQUEST_BYTES - 1)) {
            /* the request of a browser arrives in one packet most of the times, and the loop below
             * reads more only when the request line is not terminated yet */
            readAmount = NGENE_SOCKET_READ(
                clientSocket, &(request[bytesRead]),
                (size_t)(WEB_UI_REQUEST_BYTES - 1 - (size_t)bytesRead));
            if (readAmount <= 0)
                break;
            bytesRead += (unsigned long long)readAmount;
            request[bytesRead] = '\0';
            if (strstr(request, "\r\n") != NULL && strstr(request, "\r\n\r\n") != NULL)
                break;
        }
        if (ParseWebApiRequest(request, method, path, query) == SUCCESS_RETURN) {
            if (strcmp(path, "/command") == 0) {
                /* the command is only queued, and it is applied by the main thread (see ExecuteWebApiCommands) */
                DecodeWebQueryText(query, commandText);
                if (EnqueueWebApiCommand(web, commandText) == SUCCESS_RETURN)
                    SendWebApiResponse(clientSocket, "200 OK", WEB_UI_TYPE_JSON,
                                       "{\"accepted\":1}");
                else
                    SendWebApiResponse(clientSocket, "200 OK", WEB_UI_TYPE_JSON,
                                       "{\"accepted\":0,\"reason\":\"queue full\"}");
            } else if (HandleWebApiRequest(web, path, query, contentType, body) == SUCCESS_RETURN)
                SendWebApiResponse(clientSocket, "200 OK", contentType, body);
            else
                SendWebApiResponse(clientSocket, "404 Not Found", WEB_UI_TYPE_JSON,
                                   "{\"error\":\"path not offered by the interface\"}");
        }
        else
            SendWebApiResponse(clientSocket, "400 Bad Request", WEB_UI_TYPE_JSON,
                               "{\"error\":\"request line not understood\"}");
        NGENE_SOCKET_CLOSE(clientSocket);
    }
    return NULL;
}
/**
 * @brief WakeWebApiServer opens and closes one connection to the listener.
 * @details The thread of the interface waits in accept() between the requests of the pages, so the
 *          flag asking it to stop is not seen until a connection arrives (see ServeWebApiRequests).
 * @param web points to the state of the interface
 */
static void WakeWebApiServer(TWebApiData *web)
{
    NGENE_SOCKET_TYPE wakeSocket;
    struct sockaddr_in wakeAddress;
    wakeSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (wakeSocket == NGENE_SOCKET_INVALID)
        return;
    wakeAddress.sin_family = AF_INET;
    wakeAddress.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    wakeAddress.sin_port = htons((unsigned short)(web->port));
    /* the connection is closed at once, as only its arrival makes accept() return: the request read
     * there is empty, so the thread answers it with the refusal of a request line not understood and
     * ends the loop on the flag of the interface (see ServeWebApiRequests) */
    connect(wakeSocket, (struct sockaddr *)(&wakeAddress),
            (socklen_t)sizeof(struct sockaddr_in));
    NGENE_SOCKET_CLOSE(wakeSocket);
}
/**
 * @brief InitWebApi binds the loopback listener and starts the thread answering the browser.
 */
int InitWebApi(TNGeneConfiguration *cfg, TWebApiData *web)
{
    NGENE_SOCKET_TYPE listenerSocket;
    struct sockaddr_in listenerAddress;
#ifdef NGENE_NATIVE_WINDOWS
    /* the option of SO_REUSEADDR is not asked on Windows, where the option makes bind() accept a port
     * that another program already listens on: the browser would then be answered by that program and
     * not by the interface of the receiver (see utility/webApi.c) */
#else
    int reuseAddressFlag;
#endif
    if (cfg->userInterface != RX_UI_WEB) {
        web->enabled = FALSE_FLAG;
        return SUCCESS_RETURN;
    }
    web->enabled = TRUE_FLAG;
    if (cfg->webUiPort < WEB_UI_PORT_MINIMUM || cfg->webUiPort > WEB_UI_PORT_MAXIMUM) {
        SwrxLog(NGENE_LOG_SEVERITY_ERROR, "Port %d is outside the range of the web interface",
                cfg->webUiPort);
        return FAILURE_RETURN;
    }
    if (web->stateInitialized == TRUE_FLAG) {
        SwrxLog(NGENE_LOG_SEVERITY_ERROR, "The web interface was already initialized on port %d",
                web->port);
        return FAILURE_RETURN;
    }
    web->port = cfg->webUiPort;
    web->runningFlag = TRUE_FLAG;
    web->pausedFlag = FALSE_FLAG;
    web->stopElaborationFlag = FALSE_FLAG;
    web->verboseFlag = cfg->verbose;
    web->serverThreadStarted = FALSE_FLAG;
    web->listenerSocketHandle = NULL;
    web->elaborationCount = 0;
    web->commandCount = 0;
    web->logRowStored = 0;
    web->logRowNewest = 0;
    pthread_mutex_init(&(web->stateMutex), NULL);
    /* the socket layer of Windows must be initialized before the first socket of the process */
    NGENE_SOCKET_STARTUP();
    listenerSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenerSocket == NGENE_SOCKET_INVALID) {
        SwrxLog(NGENE_LOG_SEVERITY_ERROR, "Impossible to open the socket of the web interface");
        return FAILURE_RETURN;
    }
#ifndef NGENE_NATIVE_WINDOWS
    reuseAddressFlag = TRUE_FLAG;
    setsockopt(listenerSocket, SOL_SOCKET, SO_REUSEADDR, (char *)(&reuseAddressFlag),
               (int)sizeof(int));
#endif
    listenerAddress.sin_family = AF_INET;
    /* the interface is served on the loopback address only: the receiver of a vehicle or of a
     * laboratory bench is not exposed to the network of a facility (see utility/webApi.c) */
    listenerAddress.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    listenerAddress.sin_port = htons((unsigned short)(web->port));
    if (bind(listenerSocket, (struct sockaddr *)(&listenerAddress),
             (int)sizeof(struct sockaddr_in)) != 0) {
        NGENE_SOCKET_CLOSE(listenerSocket);
        SwrxLog(NGENE_LOG_SEVERITY_ERROR, "Port %d cannot be used by the web interface", web->port);
        return FAILURE_RETURN;
    }
    if (listen(listenerSocket, 8) != 0) {
        NGENE_SOCKET_CLOSE(listenerSocket);
        SwrxLog(NGENE_LOG_SEVERITY_ERROR, "Impossible to listen on port %d", web->port);
        return FAILURE_RETURN;
    }
    web->listenerSocketHandle = (void *)(uintptr_t)listenerSocket;
    web->stateInitialized = TRUE_FLAG;
    if (pthread_create(&(web->serverThread), NULL, ServeWebApiRequests, web) != 0) {
        web->stateInitialized = FALSE_FLAG;
        NGENE_SOCKET_CLOSE(listenerSocket);
        web->listenerSocketHandle = NULL;
        SwrxLog(NGENE_LOG_SEVERITY_ERROR, "Impossible to start the thread of the web interface");
        return FAILURE_RETURN;
    }
    web->serverThreadStarted = TRUE_FLAG;
    /* the configuration points at the interface, so CloseNGene() can release the port when the
     * receiver exits on an error (see utility/closeManager.c) */
    cfg->webApiData = web;
    cfg->webUiInitialized = TRUE_FLAG;
    SwrxLog(NGENE_LOG_SEVERITY_IMPORTANT_INFO,
            "Web user interface served on http://127.0.0.1:%d with the interface of the channels, "
            "of the position and of the log",
            web->port);
    return SUCCESS_RETURN;
}
/**
 * @brief DeinitWebApi closes the listener and joins the thread of the interface.
 */
void DeinitWebApi(TWebApiData *web)
{
    NGENE_SOCKET_TYPE listenerSocket;
    if (web == NULL || web->stateInitialized == FALSE_FLAG)
        return;
    pthread_mutex_lock(&(web->stateMutex));
    web->runningFlag = FALSE_FLAG;
    pthread_mutex_unlock(&(web->stateMutex));
    if (web->serverThreadStarted == TRUE_FLAG) {
        /* the connection opened below makes the waiting accept() of the thread return, so the join
         * that follows does not wait for the next poll of a page (see ServeWebApiRequests) */
        WakeWebApiServer(web);
        pthread_join(web->serverThread, NULL);
    }
    web->serverThreadStarted = FALSE_FLAG;
    if (web->listenerSocketHandle != NULL) {
        listenerSocket = (NGENE_SOCKET_TYPE)(uintptr_t)(web->listenerSocketHandle);
        NGENE_SOCKET_CLOSE(listenerSocket);
        web->listenerSocketHandle = NULL;
    }
    pthread_mutex_destroy(&(web->stateMutex));
    web->stateInitialized = FALSE_FLAG;
}






