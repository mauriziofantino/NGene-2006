/**
 * @file webApi.h
 *
 * @author Maurizio Fantino
 *
 * @date 19/11/2026
 *
 * @brief User interface served over HTTP, sibling of the textual (-t) and of the ncurses (-n) one.
 *
 * @note The interface is selected on the command line with -w or --web and the port to serve
 *       (see utility/argvParser.c). It is exclusive of the terminal interfaces, exactly as the
 *       textual and the ncurses ones are (see receiver/ngeneConfiguration.h).
 * @note The server thread of the interface (see utility/webApi.c) never reads and never writes the
 *       state of the receiver: the main thread publishes fixed snapshots guarded by a mutex, the
 *       server thread answers with those snapshots, and a request asking for a command only appends
 *       to a queue drained by the main thread at the beginning of an execution cycle (see
 *       receiver/ngene.c).
 * @note The request parser and the answer builder are exposed for the check of the interface, and
 *       they need no socket, so the check runs as the other ones do (see tools/check_web_api.c).
 */
#ifndef UTILITY_WEB_API_H_INCLUDED
#define UTILITY_WEB_API_H_INCLUDED
#include "ngeneConfiguration.h"
#include "ngeneDataStructure.h"
#include "webApiDataStructure.h"
/**
 * @brief InitWebApi binds the loopback listener and starts the thread answering the browser.
 * @details The buffers of the interface are part of the structure, so this function only initializes
 *          them, opens the listener on 127.0.0.1 and starts the server thread. The call is valid
 *          only when the interface was selected on the command line, and only once.
 * @param cfg points to the receiver configuration
 * @param web points to the state of the interface
 * @return SUCCESS_RETURN when the listener was opened and the thread started, FAILURE_RETURN when
 *          the port could not be used, as another receiver or another program holds it
 */
int InitWebApi(TNGeneConfiguration *cfg, TWebApiData *web);
/**
 * @brief DeinitWebApi closes the listener and joins the thread of the interface.
 * @param web points to the state of the interface
 */
void DeinitWebApi(TWebApiData *web);
/**
 * @brief PublishWebApiChannels snapshots the state of the channels of one execution cycle.
 * @details Call it on the main thread after the channels of the cycle have been committed, so the
 *          table served to the browser is the one the terminal interface would have shown. The
 *          graphs of the channels are extended here as well, one point per elaboration.
 * @param cfg points to the receiver configuration
 * @param rx points to the receiver base data structure
 */
void PublishWebApiChannels(TNGeneConfiguration *cfg, TNGeneData *rx);
/**
 * @brief PublishWebApiPvt snapshots the result of the position, velocity and time estimation.
 * @details Call it on the main thread where the ncurses interface calls NcCompleteLog().
 * @param cfg points to the receiver configuration
 * @param rx points to the receiver base data structure
 */
void PublishWebApiPvt(TNGeneConfiguration *cfg, TNGeneData *rx);
/**
 * @brief DrawWebApiLog keeps one line of the log for the page of the interface.
 * @details Call it from SwrxLog(), as NcDrawRxLog() is called from the same function.
 * @param web points to the state of the interface
 * @param logString is the line to keep
 * @param severity is the severity of the message, see ngeneConstants.h
 * @return SUCCESS_RETURN when the line was stored in the ring of the rows, FAILURE_RETURN when the
 *          interface is not initialized or the line is longer than one row
 */
int DrawWebApiLog(TWebApiData *web, char *logString, int severity);
/**
 * @brief EnqueueWebApiCommand stores a command asked by the browser.
 * @details The command is applied later by ExecuteWebApiCommands(), on the main thread.
 * @param web points to the state of the interface
 * @param command is the command line typed in the page
 * @return SUCCESS_RETURN when the command was queued, FAILURE_RETURN when the queue is full
 */
int EnqueueWebApiCommand(TWebApiData *web, char *command);
/**
 * @brief TakeWebApiCommands moves the commands queued by the browser out of the queue.
 * @details The main thread drains the queue here, so the browser never blocks the elaboration and
 *          the commands are never applied while the channels of a cycle are elaborated (see
 *          receiver/ngene.c).
 * @param web points to the state of the interface
 * @param taken is the array receiving the commands, sized WEB_UI_COMMANDS_MAXIMUM rows
 * @return the number of commands taken, at most WEB_UI_COMMANDS_MAXIMUM
 */
unsigned int TakeWebApiCommands(TWebApiData *web, char taken[][WEB_UI_COMMAND_LENGTH]);
/**
 * @brief ApplyWebApiCommand applies one command asked in a page of the interface.
 * @details The functions of the receiver that a command calls are called on the main thread only, as
 *          the transitions of the state machine of the channels are taken there (see
 *          receiver/channelManager.c).
 * @param web points to the state of the interface
 * @param cfg points to the receiver configuration
 * @param rx points to the receiver base data structure
 * @param command is the command to apply
 * @return TRUE_FLAG when the elaboration must end, that is for the command stop, FALSE_FLAG otherwise
 */
int ApplyWebApiCommand(TWebApiData *web, TNGeneConfiguration *cfg, TNGeneData *rx, char *command);
/**
 * @brief ExecuteWebApiCommands drains the queue and applies every command of the interface.
 * @details Call it on the main thread at the beginning of an execution cycle, before the channels
 *          are prepared (see receiver/ngene.c).
 * @param cfg points to the receiver configuration
 * @param rx points to the receiver base data structure
 * @return TRUE_FLAG when the elaboration must end, as the command stop asks for the same exit that
 *          the failure of the front end asks for, FALSE_FLAG otherwise
 */
int ExecuteWebApiCommands(TNGeneConfiguration *cfg, TNGeneData *rx);
/**
 * @brief ParseWebApiRequest extracts the method, the path and the query of one request line.
 * @param request is the text read from the socket
 * @param method is the buffer receiving the method, at least 8 characters
 * @param path is the buffer receiving the path of the request
 * @param query is the buffer receiving the query of the request, if any
 * @return SUCCESS_RETURN when a request line was found, FAILURE_RETURN otherwise
 */
int ParseWebApiRequest(char *request, char *method, char *path, char *query);
/**
 * @brief HandleWebApiRequest builds the body of the answer for one path of the interface.
 * @details The answer is text/html for a page and application/json for a status document. Unknown
 *          paths are refused, so the interface exposes only what is listed here. The query of the
 *          request selects the channel whose graph is asked (see utility/webApi.c).
 * @param web points to the state of the interface
 * @param path is the path asked by the browser
 * @param query is the query of the request, empty when there is none
 * @param contentType is the buffer receiving the content type of the answer
 * @param body is the buffer receiving the body of the answer
 * @return SUCCESS_RETURN when the path is known, FAILURE_RETURN otherwise
 */
int HandleWebApiRequest(TWebApiData *web, char *path, char *query, char *contentType, char *body);
/**
 * @brief GetWebApiCommandList answers the page asking which commands the interface accepts.
 * @param body is the buffer receiving the list of commands as JSON
 */
char *GetWebApiCommandList(void);
#endif /* UTILITY_WEB_API_H_INCLUDED */
