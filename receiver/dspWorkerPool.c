/**
 * @file dspWorkerPool.c
 *
 * @author Maurizio Fantino
 *
 * @date 19/11/2026
 *
 * @brief Workers, scratch arrays and queue of the channels of one execution cycle (see
 *        receiver/dspWorkerPool.h). The scratch of the workers belongs to the pool structure:
 *        InitDspWorkerPool() only allocates the arrays it contains, during the initialization
 *        of the receiver (see receiver/dspContext.c).
 */
#include <pthread.h>
#include "ngeneConfiguration.h"
#include "ngeneDataStructure.h"
#include "ngeneConstants.h"
#include "ioFunctions.h"
#include "channelManager.h"
#include "dspWorkerPool.h"

/** @brief Scratch context of the calling DSP worker thread, NULL on other threads. */
NGENE_THREAD_LOCAL TDspContext *ngeneDspOverride = NULL;

/** @brief Body of one worker: elaborates the channels it is given until the shutdown. */
static void *ElaborateChannelsOnWorkerThread(void *threadArgument)
{
    TDspWorkerThreadArgument *workerArgument =
        (TDspWorkerThreadArgument *)(threadArgument); /* passed by InitDspWorkerPool() */
    TDspWorkerPool *pool = workerArgument->pool;
    int workerIndex = workerArgument->workerIndex;
    int jobIndex = 0;
    if (InitXmmsBackup() == SUCCESS_RETURN) {
        /* the classic SIMD kernels keep a backup of the XMM registers of the executing thread,
         * and the scratch of this worker is the one its elaboration must use */
        ngeneDspOverride = &(pool->dspContexts[workerIndex]);
        while (pool->shutdownFlag == FALSE_FLAG) {
            pthread_mutex_lock(&(pool->jobsMutex));
            while ((pool->shutdownFlag == FALSE_FLAG) && (pool->nextJobIndex >= pool->jobCount)) {
                pthread_cond_wait(&(pool->jobsAvailable), &(pool->jobsMutex));
            }
            if (pool->shutdownFlag != FALSE_FLAG) {
                pthread_mutex_unlock(&(pool->jobsMutex));
                break;
            }
            jobIndex = pool->nextJobIndex;
            pool->nextJobIndex++;
            pthread_mutex_unlock(&(pool->jobsMutex));
            ComputeChannelCycle(pool->cycleConfig, pool->cycleReceiver,
                                &(pool->cycleReceiver->channelArray[jobIndex]));
            pthread_mutex_lock(&(pool->jobsMutex));
            pool->completedJobs++;
            if (pool->completedJobs >= pool->jobCount) {
                pthread_cond_signal(&(pool->cycleCompleted));
            }
            pthread_mutex_unlock(&(pool->jobsMutex));
        }
        DeallocateXmmsBackup();
    }
    else {
        SwrxLog(NGENE_LOG_SEVERITY_ERROR,
                "Impossible to allocate the XMM backup of DSP worker %d", workerIndex);
    }
    return NULL;
}

int InitDspWorkerPool(TNGeneConfiguration *cfg, TDspWorkerPool *pool, int requestedWorkers)
{
    int workerIndex;
    int errorsFound = 0;
    pool->contextCount = 0;
    pool->threadCount = 0;
    pool->workerCount = DSP_WORKERS_MINIMUM;
    pool->cycleConfig = NULL;
    pool->cycleReceiver = NULL;
    pool->nextJobIndex = 0;
    pool->jobCount = 0;
    pool->completedJobs = 0;
    pool->shutdownFlag = FALSE_FLAG;
    if (requestedWorkers < DSP_WORKERS_MINIMUM) {
        requestedWorkers = DSP_WORKERS_MINIMUM;
    }
    if (requestedWorkers > DSP_WORKERS_MAXIMUM) {
        SwrxLog(NGENE_LOG_SEVERITY_NOTE, "Selected DSP workers %d limited to %d", requestedWorkers,
                DSP_WORKERS_MAXIMUM);
        requestedWorkers = DSP_WORKERS_MAXIMUM;
    }
    pthread_mutex_init(&(pool->jobsMutex), NULL);
    pthread_cond_init(&(pool->jobsAvailable), NULL);
    pthread_cond_init(&(pool->cycleCompleted), NULL);
    /* one worker elaborates the channels on the main thread: no thread is started, and the
     * scratch of the receiver is used, as the historical execution did */
    if (requestedWorkers > DSP_WORKERS_MINIMUM) {
        for (workerIndex = 0; workerIndex < requestedWorkers; workerIndex++) {
            if (InitDspContext(cfg, &(pool->dspContexts[workerIndex])) != SUCCESS_RETURN) {
                SwrxLog(NGENE_LOG_SEVERITY_ERROR, "Impossible to allocate the DSP scratch of worker %d",
                        workerIndex);
                break;
            }
            pool->contextCount++;
        }
        if (pool->contextCount < requestedWorkers) {
            errorsFound++;
        }
        else {
            for (workerIndex = 0; workerIndex < requestedWorkers; workerIndex++) {
                pool->workerArguments[workerIndex].pool = pool;
                pool->workerArguments[workerIndex].workerIndex = workerIndex;
                if (pthread_create(&(pool->workerThreads[pool->threadCount]), NULL,
                                   ElaborateChannelsOnWorkerThread,
                                   &(pool->workerArguments[workerIndex])) != 0) {
                    errorsFound++;
                }
                else {
                    pool->threadCount++;
                }
            }
            if (pool->threadCount == 0) {
                SwrxLog(NGENE_LOG_SEVERITY_ERROR, "Impossible to start the DSP worker threads");
                errorsFound++;
            }
            else if (pool->threadCount < requestedWorkers) {
                /* the host did not provide all the threads: a smaller pool is used, the elaboration
                 * of the channels is not affected, only the distribution of the load is */
                SwrxLog(NGENE_LOG_SEVERITY_NOTE, "DSP workers reduced from %d to %d", requestedWorkers,
                        pool->threadCount);
                pool->workerCount = pool->threadCount;
            }
            else {
                pool->workerCount = requestedWorkers;
            }
        }
        if (errorsFound > 0) {
            DeinitDspWorkerPool(pool);
            return FAILURE_RETURN;
        }
    }
    return SUCCESS_RETURN;
}

void DeinitDspWorkerPool(TDspWorkerPool *pool)
{
    int workerIndex;
    pthread_mutex_lock(&(pool->jobsMutex));
    pool->shutdownFlag = TRUE_FLAG;
    pthread_cond_broadcast(&(pool->jobsAvailable));
    pthread_mutex_unlock(&(pool->jobsMutex));
    for (workerIndex = 0; workerIndex < pool->threadCount; workerIndex++) {
        pthread_join(pool->workerThreads[workerIndex], NULL);
    }
    for (workerIndex = 0; workerIndex < pool->contextCount; workerIndex++) {
        DeinitDspContext(&(pool->dspContexts[workerIndex]));
    }
    pool->contextCount = 0;
    pool->threadCount = 0;
    pool->workerCount = DSP_WORKERS_MINIMUM;
    pool->cycleConfig = NULL;
    pool->cycleReceiver = NULL;
    pool->nextJobIndex = 0;
    pool->jobCount = 0;
    pool->completedJobs = 0;
    pthread_mutex_destroy(&(pool->jobsMutex));
    pthread_cond_destroy(&(pool->jobsAvailable));
    pthread_cond_destroy(&(pool->cycleCompleted));
}

void DispatchDspJobs(TDspWorkerPool *pool, TNGeneConfiguration *cfg, TNGeneData *rx, int jobCount)
{
    int channelIndex;
    if (jobCount > cfg->numberOfChannels) {
        jobCount = cfg->numberOfChannels;
    }
    if (jobCount < 0) {
        jobCount = 0;
    }
    if ((pool->workerCount <= DSP_WORKERS_MINIMUM) || (pool->threadCount == 0)) {
        /* one worker: the channels of the execution cycle are elaborated on the main thread, in
         * the order of the channel index and with the scratch of the receiver */
        for (channelIndex = 0; channelIndex < jobCount; channelIndex++) {
            ComputeChannelCycle(cfg, rx, &(rx->channelArray[channelIndex]));
        }
    }
    else {
        pthread_mutex_lock(&(pool->jobsMutex));
        pool->cycleConfig = cfg;
        pool->cycleReceiver = rx;
        pool->nextJobIndex = 0;
        pool->completedJobs = 0;
        pool->jobCount = jobCount;
        pthread_cond_broadcast(&(pool->jobsAvailable));
        /* the main thread waits here: the shared state of the receiver is committed only after the
         * channels of this execution cycle have been elaborated (see
         * receiver/channelManager.c) */
        while (pool->completedJobs < pool->jobCount) {
            pthread_cond_wait(&(pool->cycleCompleted), &(pool->jobsMutex));
        }
        pool->cycleConfig = NULL;
        pool->cycleReceiver = NULL;
        pthread_mutex_unlock(&(pool->jobsMutex));
    }
}
