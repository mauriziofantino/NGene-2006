/**
 * @file signalHandler.h
 *
 * @author Mario Nicola
 *
 * @date 12/08/2014
 *
 * @version 1.0.0
 *
 * @brief Header for the file signalHandler.c (former signol.c by Maurizio Fantino)
 */
#ifndef SIGNAL_HANDLER_H_INCLUDED
#define SIGNAL_HANDLER_H_INCLUDED
#include <signal.h>
/** @brief Signal-safe stop request observed by blocking frontend waits. */
extern volatile sig_atomic_t ngeneStopRequested;
/**
 * @brief Register operating-system and user stop-signal handlers.
 *
 * @param cfg points to the receiver configuration
 * @param run points to a variable which is used to manage the receiver elaboration
 */
void StartListeningSignals(TNGeneConfiguration *cfg, int *run);
/**
 * @brief This function performs the corresponding actions when the signal is caught
 *
 * @param signol represents the received signal
 */
void SignalHandler(int signol);
#endif /* SIGNAL_HANDLER_H_INCLUDED */
