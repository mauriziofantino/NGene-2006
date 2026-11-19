/**
 * @file webApiDataStructure.h
 *
 * @author Maurizio Fantino
 *
 * @date 19/11/2026
 *
 * @brief State of the web user interface: snapshots, graphs, log rows and command queue.
 *
 * @note Every buffer is fixed size and embedded in TWebApiData, so the interface allocates
 *       nothing while the receiver elaborates the samples (see utility/webApi.c).
 * @note The listener socket is kept as a void pointer because the type of a socket differs
 *       between the BSD sockets of Linux and the WinSock of Windows (see platform/windows).
 */
#ifndef UTILITY_WEB_API_DATA_STRUCTURE_H_INCLUDED
#define UTILITY_WEB_API_DATA_STRUCTURE_H_INCLUDED
#include <pthread.h>
#include "ngeneConstants.h"
/** @brief Port served by default when the option --web is given without a number. */
#define WEB_UI_DEFAULT_PORT 8080
/** @brief Highest port accepted on the command line. */
#define WEB_UI_PORT_MAXIMUM 65535
/** @brief Lowest port accepted on the command line. */
#define WEB_UI_PORT_MINIMUM 1024
/** @brief Highest number of channels that can appear in one page. */
#define WEB_UI_CHANNELS_MAXIMUM 24
/** @brief Number of past elaborations kept for each channel and drawn as a graph. */
#define WEB_UI_HISTORY_SAMPLES 240
/** @brief Number of log lines kept for the page of the interface. */
#define WEB_UI_LOG_ROWS 48
/** @brief Length of one log line kept by the interface, see ngeneConstants.h. */
#define WEB_UI_LOG_ROW_LENGTH (MAX_ROW_LENGTH + 1)
/** @brief Number of commands the browser may ask for before the main thread applies them. */
#define WEB_UI_COMMANDS_MAXIMUM 8
/** @brief Length of one command line, including the argument of the command. */
#define WEB_UI_COMMAND_LENGTH 64
/** @brief Bytes of the request read from the socket, request line and headers included. */
#define WEB_UI_REQUEST_BYTES (6 * 1024)
/** @brief Bytes of the body of one answer, that is of one page or one JSON document. */
#define WEB_UI_RESPONSE_BYTES (24 * 1024)
/** @struct WebApiCommandStruct @brief One command asked by the browser (see ExecuteWebApiCommands()). */
typedef struct WebApiCommandStruct {
    char text[WEB_UI_COMMAND_LENGTH]; /**< command and its argument, as typed in the page */
} TWebApiCommand;
/** @struct WebApiChannelHistoryStruct @brief Graph of the elaborations of one channel. */
typedef struct WebApiChannelHistoryStruct {
    double doppler[WEB_UI_HISTORY_SAMPLES];
    /**< carrier frequency minus the nominal value, in Hz (see receiver/channelDataStructure.h) */
    double correlation[WEB_UI_HISTORY_SAMPLES]; /**< prompt in-phase correlation (see tracking) */
    unsigned long long sample[WEB_UI_HISTORY_SAMPLES]; /**< input sample of the elaboration */
    int channelState[WEB_UI_HISTORY_SAMPLES]; /**< state of the channel at that elaboration */
    unsigned long long newestIndex; /**< index of the most recent elaboration stored */
    unsigned int storedCount; /**< number of elaborations stored so far */
} TWebApiChannelHistory;
/** @struct WebApiDataStruct @brief State of the web user interface. */
typedef struct WebApiDataStruct {
    int enabled; /**< TRUE_FLAG when the interface was selected on the command line */
    int port; /**< port of the loopback listener (see WEB_UI_DEFAULT_PORT) */
    int runningFlag; /**< FALSE_FLAG ends the wait of the server thread */
    int pausedFlag; /**< TRUE_FLAG makes the main thread skip one execution cycle (see pause) */
    int stopElaborationFlag; /**< TRUE_FLAG makes the main thread exit the cycle loop (see stop) */
    int verboseFlag; /**< TRUE_FLAG keeps also the NOTE messages in the rows of the log */
    int stateInitialized; /**< TRUE_FLAG when the fields below were initialized */
    unsigned long long elaborationCount; /**< cycles published by PublishWebApiChannels() */
    char channelsRows[WEB_UI_RESPONSE_BYTES]; /**< array of the rows of the channels published by the
                                      main thread, leaving the scalars of the document to the answer
                                      below, so a pause asked in a page stays visible (see utility/webApi.c) */
    char pvtPayload[4096]; /**< /pvt.json published by the main thread */
    TWebApiChannelHistory histories[WEB_UI_CHANNELS_MAXIMUM];
    /**< graphs of the channels, appended at each published elaboration */
    unsigned int logRowNewest; /**< row written most recently by DrawWebApiLog() */
    unsigned int logRowStored; /**< number of log rows stored so far */
    char logRows[WEB_UI_LOG_ROWS][WEB_UI_LOG_ROW_LENGTH]; /**< log lines kept for the page */
    int logRowSeverity[WEB_UI_LOG_ROWS]; /**< severity of the log line kept in this row */
    unsigned int commandCount; /**< number of commands in the queue below */
    TWebApiCommand commandQueue[WEB_UI_COMMANDS_MAXIMUM];
    /**< commands asked by the browser and not applied yet */
    pthread_mutex_t stateMutex; /**< guards the payloads, the history, the log and the queue */
    int serverThreadStarted; /**< TRUE_FLAG when the thread answering the browser was started */
    void *listenerSocketHandle; /**< listener socket, kept as void* to avoid the platform type */
    pthread_t serverThread; /**< thread answering the requests of the browser */
} TWebApiData;
#endif /* UTILITY_WEB_API_DATA_STRUCTURE_H_INCLUDED */
