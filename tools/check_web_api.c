/**
 * @file check_web_api.c
 *
 * @author Maurizio Fantino
 *
 * @date 19/11/2026
 *
 * @brief Check of the web user interface: the request line, the routes, the payloads, the commands.
 *
 * @note No listener is opened and no browser is involved: the parser and the answer builder of the
 *       interface are exercised directly, so this check runs like the other ones (see utility/webApi.h).
 * @note The function of the receiver that a command calls is stubbed here, as the other checks stub
 *       what they do not want to link (see tools/check_tracking_holdover.c).
 */
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "ngeneConstants.h"
#include "ngeneConfiguration.h"
#include "ngeneDataStructure.h"
#include "channelManager.h"
#include "ioFunctions.h"
#include "webApiDataStructure.h"
#include "webApi.h"
/** @brief Number of the conditions checked, reported at the end. */
static unsigned int checksPerformed;
/** @brief Number of the conditions that failed, reported at the end. */
static unsigned int checksFailed;
/** @brief Rows of one array of commands taken from the queue of the interface. */
static char takenCommands[WEB_UI_COMMANDS_MAXIMUM][WEB_UI_COMMAND_LENGTH];

void HardRestartChannels(TNGeneConfiguration *cfg, TNGeneData *rx)
{
    /* stub of the receiver function: this check never restarts a channel (see
     * receiver/channelManager.c) */
    (cfg);
    (rx);
}

int SwrxLog(int severity, const char *format, ...)
{
    /* stub of the log of the interface: nothing is printed while the check runs (see
     * utility/ioFunctions.c) */
    (severity);
    (format);
    return SUCCESS_RETURN;
}
/**
 * @brief CheckWebApi asserts one condition of the interface and counts the result.
 * @param condition is the condition checked, zero when it failed
 * @param description names the condition on the standard output
 */
static void CheckWebApi(int condition, const char *description)
{
    checksPerformed++;
    if (condition == 0) {
        checksFailed++;
        printf("FAIL: %s\n", description);
    } else {
        printf("ok  : %s\n", description);
    }
}
/**
 * @brief CountWebApiCharacter counts one character of a document, so the check can tell a closed
 *        document of a document cut by the capacity of the answer (see utility/webApi.c).
 * @param document is the document served by the interface
 * @param character is the character to count
 * @return the number of occurrences found
 */
static unsigned int CountWebApiCharacter(char *document, char character)
{
    unsigned int index;
    unsigned int foundCount;
    foundCount = 0;
    for (index = 0; document[index] != '\0'; index++) {
        if (document[index] == character)
            foundCount++;
    }
    return foundCount;
}
/**
 * @brief IsWebApiDocumentClosed tells whether the brackets of a JSON document are balanced.
 * @param body is the document served by the interface
 * @return one when the document is closed, zero when it was cut
 */
static int IsWebApiDocumentClosed(char *body)
{
    if (CountWebApiCharacter(body, '[') != CountWebApiCharacter(body, ']'))
        return 0;
    if (CountWebApiCharacter(body, '{') != CountWebApiCharacter(body, '}'))
        return 0;
    if (strlen(body) < 2)
        return 0;
    /* the documents of the interface are objects, so the last character must be the closing brace */
    if (body[strlen(body) - 1] != '}')
        return 0;
    return 1;
}
int main(void)
{
    TWebApiData web;
    TNGeneConfiguration cfg;
    char method[8];
    char path[WEB_UI_COMMAND_LENGTH];
    char query[WEB_UI_COMMAND_LENGTH];
    char contentType[64];
    char body[WEB_UI_RESPONSE_BYTES];
    unsigned int takenCount;
    unsigned int pointIndex;
    cfg.webUiPort = WEB_UI_DEFAULT_PORT;
    cfg.userInterface = RX_UI_WEB;
    pthread_mutex_init(&(web.stateMutex), NULL);
    web.stateInitialized = TRUE_FLAG;
    web.enabled = TRUE_FLAG;
    web.verboseFlag = TRUE_FLAG;
    web.pausedFlag = FALSE_FLAG;
    web.commandCount = 0;
    web.logRowStored = 0;
    web.logRowNewest = 0;
    /* the request line of a browser is read as method, path and query */
    CheckWebApi(ParseWebApiRequest("GET /status/log.json HTTP/1.1\r\nHost: x\r\n", method, path,
                                   query) == SUCCESS_RETURN,
                "the request line of a browser is understood");
    CheckWebApi(strcmp(method, "GET") == 0, "the method of the request is kept");
    CheckWebApi(strcmp(path, "/status/log.json") == 0, "the path of the request is kept");
    CheckWebApi(ParseWebApiRequest("POST /command?text=stop HTTP/1.1\r\n", method, path, query) ==
                    SUCCESS_RETURN,
                "a request with a query is understood");
    CheckWebApi(strcmp(query, "text=stop") == 0, "the query of the request is kept");
    CheckWebApi(ParseWebApiRequest("\r\n", method, path, query) == FAILURE_RETURN,
                "an empty request is refused");
    /* the pages of the interface are served, and no other path is */
    CheckWebApi(HandleWebApiRequest(&web, "/", "", contentType, body) == SUCCESS_RETURN,
                "the page of the summary is served");
    CheckWebApi(strstr(body, "NGene Classic") != NULL, "the page of the summary names the receiver");
    CheckWebApi(strstr(contentType, "text/html") != NULL, "a page is served as HTML");
    CheckWebApi(HandleWebApiRequest(&web, "/channels", "", contentType, body) == SUCCESS_RETURN,
                "the page of the channels is served");
    CheckWebApi(strstr(body, "prompt correlation") != NULL,
                "the page of the channels draws the graph of the elaborations");
    CheckWebApi(HandleWebApiRequest(&web, "/pvt", "", contentType, body) == SUCCESS_RETURN,
                "the page of the position is served");
    CheckWebApi(HandleWebApiRequest(&web, "/etc/passwd", "", contentType, body) == FAILURE_RETURN,
                "a path outside the interface is refused");
    /* the rows of the channels are published by the main thread, and the scalars of the document are
     * written by the answer, so the page sees the pause as it was asked (see utility/webApi.c) */
    pthread_mutex_lock(&(web.stateMutex));
    memcpy(&(web.channelsRows[0]), "[{\"index\":0,\"name\":\"GPS16\"}]", 28);
    web.elaborationCount = 7;
    pthread_mutex_unlock(&(web.stateMutex));
    CheckWebApi(HandleWebApiRequest(&web, "/status/channels.json", "", contentType, body) ==
                    SUCCESS_RETURN,
                "the state of the channels is served as JSON");
    CheckWebApi(strstr(body, "\"cycles\":7") != NULL, "the number of published elaborations is served");
    CheckWebApi(strstr(body, "GPS16") != NULL, "the rows published by the main thread are served");
    CheckWebApi(strstr(body, "\"paused\":0") != NULL, "the flag of the pause is served as it stands");
    CheckWebApi(strstr(contentType, "application/json") != NULL, "the status is served as JSON");
    CheckWebApi(IsWebApiDocumentClosed(body) == 1, "the document of the channels is closed as JSON");
    web.pausedFlag = TRUE_FLAG;
    CheckWebApi(HandleWebApiRequest(&web, "/status/channels.json", "", contentType, body) == SUCCESS_RETURN,
                "the state of the channels is served also during the pause");
    CheckWebApi(strstr(body, "\"paused\":1") != NULL,
                "the pause asked in a page is served while no channel is elaborated");
    web.pausedFlag = FALSE_FLAG;
    pthread_mutex_lock(&(web.stateMutex));
    memcpy(&(web.pvtPayload[0]), "{\"valid\":1,\"lat\":44.2435050}", 29);
    pthread_mutex_unlock(&(web.stateMutex));
    CheckWebApi(HandleWebApiRequest(&web, "/status/pvt.json", "", contentType, body) == SUCCESS_RETURN,
                "the estimation of the position is served as JSON");
    CheckWebApi(strstr(body, "\"lat\":44.2435050") != NULL, "the payload of the position is not rewritten");
    /* the rows of the log are kept in a ring, and the severity of the NOTE rows is respected */
    CheckWebApi(DrawWebApiLog(&web, "tracking locked", NGENE_LOG_SEVERITY_INFO) == SUCCESS_RETURN,
                "one line of the log is kept");
    CheckWebApi(DrawWebApiLog(&web, "fix computed", NGENE_LOG_SEVERITY_IMPORTANT_INFO) ==
                    SUCCESS_RETURN,
                "a second line of the log is kept");
    CheckWebApi(DrawWebApiLog(&web, "a note with verbose on", NGENE_LOG_SEVERITY_NOTE) ==
                    SUCCESS_RETURN,
                "a NOTE line is kept when verbose is on");
    web.verboseFlag = FALSE_FLAG;
    CheckWebApi(DrawWebApiLog(&web, "a note with verbose off", NGENE_LOG_SEVERITY_NOTE) ==
                    FAILURE_RETURN,
                "a NOTE line is refused when verbose is off");
    CheckWebApi(HandleWebApiRequest(&web, "/status/log.json", "", contentType, body) == SUCCESS_RETURN,
                "the rows of the log are served");
    CheckWebApi(strstr(body, "fix computed") != NULL, "the most recent row of the log is served");
    CheckWebApi(strstr(body, "verbose off") == NULL, "the row refused was not kept");
    CheckWebApi(IsWebApiDocumentClosed(body) == 1, "the document of the log is closed as JSON");
    /* the history of one channel is the graph drawn by the page of the channels */
    for (pointIndex = 0; pointIndex < 4; pointIndex++) {
        web.histories[2].sample[pointIndex] = 1000 + pointIndex;
        web.histories[2].doppler[pointIndex] = 100.0 + (double)pointIndex;
        web.histories[2].correlation[pointIndex] = 40.0 + (double)pointIndex;
    }
    web.histories[2].newestIndex = 3;
    web.histories[2].storedCount = 4;
    CheckWebApi(HandleWebApiRequest(&web, "/status/history.json", "channel=2", contentType, body) ==
                    SUCCESS_RETURN,
                "the history of one channel is served");
    CheckWebApi(strstr(body, "[1000,100.000,40.0]") != NULL,
                "the oldest point of the history is served first");
    CheckWebApi(IsWebApiDocumentClosed(body) == 1, "the document of the history is closed as JSON");
    CheckWebApi(HandleWebApiRequest(&web, "/status/history.json", "channel=99", contentType, body) ==
                    FAILURE_RETURN,
                "the history of a channel that does not exist is refused");
    /* the commands asked in a page are queued, then applied on the main thread */
    CheckWebApi(EnqueueWebApiCommand(&web, "pause") == SUCCESS_RETURN, "one command is queued");
    CheckWebApi(EnqueueWebApiCommand(&web, "resume") == SUCCESS_RETURN, "a second command is queued");
    CheckWebApi(EnqueueWebApiCommand(&web, "restart-channels") == SUCCESS_RETURN,
                "a command restarting the channels is queued");
    CheckWebApi(EnqueueWebApiCommand(&web, "") == FAILURE_RETURN, "an empty command is refused");
    takenCount = TakeWebApiCommands(&web, takenCommands);
    CheckWebApi(takenCount == 3, "the three commands queued are taken at once");
    CheckWebApi(ApplyWebApiCommand(&web, &cfg, NULL, takenCommands[0]) == FALSE_FLAG,
                "the command pause does not end the elaboration");
    CheckWebApi(web.pausedFlag == TRUE_FLAG, "the command pause stops the elaboration of the channels");
    CheckWebApi(ApplyWebApiCommand(&web, &cfg, NULL, takenCommands[1]) == FALSE_FLAG,
                "the command resume does not end the elaboration");
    CheckWebApi(web.pausedFlag == FALSE_FLAG, "the command resume elaborates the channels again");
    CheckWebApi(ApplyWebApiCommand(&web, &cfg, NULL, takenCommands[2]) == FALSE_FLAG,
                "the command restarting the channels does not end the elaboration");
    CheckWebApi(EnqueueWebApiCommand(&web, "stop") == SUCCESS_RETURN,
                "a command asking to stop the receiver is queued");
    takenCount = TakeWebApiCommands(&web, takenCommands);
    CheckWebApi(takenCount == 1, "the command stop is taken from the queue");
    CheckWebApi(ApplyWebApiCommand(&web, &cfg, NULL, takenCommands[0]) == TRUE_FLAG,
                "the command stop asks to end the elaboration");
    /* ExecuteWebApiCommands() is not exercised here: it reads the state of the receiver to find
     * the interface, so it belongs to the receiver (see receiver/ngene.c) */
    for (pointIndex = 0; pointIndex < WEB_UI_COMMANDS_MAXIMUM; pointIndex++)
        EnqueueWebApiCommand(&web, "clear-log");
    CheckWebApi(EnqueueWebApiCommand(&web, "clear-log") == FAILURE_RETURN,
                "the queue of the commands is bounded");
    printf("\nchecks: %u performed, %u failed\n", checksPerformed, checksFailed);
    pthread_mutex_destroy(&(web.stateMutex));
    return (checksFailed == 0) ? 0 : 1;
}
