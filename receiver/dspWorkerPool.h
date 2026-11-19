/**
 * @file dspWorkerPool.h
 *
 * @author Maurizio Fantino
 *
 * @date 19/11/2026
 *
 * @brief Bounded pool of workers elaborating the channels of one execution cycle.
 *
 * @note Stage 2 of the multi-core DSP pool. RunChannelsCycle() (see receiver/channelManager.c)
 *       prepares the channels of one execution cycle on the main thread, dispatches the
 *       elaboration of each channel to the workers of this pool, then commits the channels
 *       on the main thread in the order of the channel index. Every worker owns one DSP scratch
 *       (see receiver/dspContext.c) and one backup of the XMM registers (see receiver/xmmsBackup.h)
 *       and queues the log of the channel it elaborates (see utility/logCapture.h). The number
 *       of workers comes from the command line (see utility/argvParser.c): one worker runs the
 *       elaboration on the main thread, which is the historical serial execution.
 */
#ifndef RECEIVER_DSP_WORKER_POOL_H_INCLUDED
#define RECEIVER_DSP_WORKER_POOL_H_INCLUDED
#include <pthread.h>
#include "dspContextDataStructure.h"
/** @brief Maximum number of DSP workers, whatever the user selects. */
#define DSP_WORKERS_MAXIMUM 4
/** @brief Minimum number of DSP workers: the jobs run then on the main thread. */
#define DSP_WORKERS_MINIMUM 1
/** @struct DspWorkerThreadArgumentStruct @brief Argument of one worker thread. */
typedef struct DspWorkerThreadArgumentStruct {
    struct DspWorkerPoolStruct *pool; /**< pool owning the thread */
    int workerIndex; /**< index of the scratch used by the thread */
} TDspWorkerThreadArgument;
/** @struct DspWorkerPoolStruct @brief Workers and queue of one execution cycle. */
typedef struct DspWorkerPoolStruct {
    TDspContext dspContexts[DSP_WORKERS_MAXIMUM];
    /**< scratch of each worker, embedded in the pool and initialized during InitDspWorkerPool() */
    pthread_t workerThreads[DSP_WORKERS_MAXIMUM];
    /**< thread of each started worker */
    int contextCount; /**< scratch arrays of the pool initialized by InitDspWorkerPool() */
    int threadCount; /**< number of started worker threads */
    int workerCount; /**< workers elaborating the channels, one means the main thread */
    struct NGeneConfigurationStruct *cycleConfig;
    /**< configuration of the execution cycle being elaborated */
    struct NGeneDataStruct *cycleReceiver;
    /**< receiver of the execution cycle being elaborated */
    int nextJobIndex; /**< index of the first channel not elaborated yet */
    int jobCount; /**< number of channels of the current execution cycle */
    int completedJobs; /**< number of channels elaborated in the current cycle */
    int shutdownFlag; /**< flag ending the wait of the worker threads */
    pthread_mutex_t jobsMutex; /**< guards the queue of the channels of the cycle */
    pthread_cond_t jobsAvailable; /**< notified when a cycle queues its channels */
    pthread_cond_t cycleCompleted; /**< notified when the channels of a cycle are elaborated */
    TDspWorkerThreadArgument workerArguments[DSP_WORKERS_MAXIMUM];
    /**< argument of each worker thread, pointing to this pool and to its scratch */
} TDspWorkerPool;
/**
 * @brief InitDspWorkerPool allocates the scratch of the workers and starts their threads.
 * @details All the memory of the pool is allocated here, during the initialization of the
 *          receiver. The worker threads wait for the channels of an execution cycle.
 * @param cfg points to the receiver configuration
 * @param pool points to the pool of the receiver
 * @param requestedWorkers is the number of workers selected by the user, clamped to the range
 *            [DSP_WORKERS_MINIMUM, DSP_WORKERS_MAXIMUM]
 * @return SUCCESS_RETURN when the pool is ready, FAILURE_RETURN otherwise
 */
int InitDspWorkerPool(struct NGeneConfigurationStruct *cfg, struct DspWorkerPoolStruct *pool,
                      int requestedWorkers);
/**
 * @brief DeinitDspWorkerPool stops the workers and releases their scratch arrays.
 * @param pool points to the pool of the receiver
 */
void DeinitDspWorkerPool(struct DspWorkerPoolStruct *pool);
/**
 * @brief DispatchDspJobs elaborates the channels of one execution cycle and waits for them.
 * @details With DSP_WORKERS_MINIMUM worker the channels are elaborated on the calling thread,
 *          in the order of the channel index, as the historical execution did. With more
 *          workers the channels are taken by the worker threads: an elaboration must touch
 *          only the data of its channel and the scratch of the worker executing it, since the
 *          channels of one execution cycle are elaborated at the same time (see
 *          receiver/channelManager.c).
 * @param pool points to the pool of the receiver
 * @param cfg points to the receiver configuration of the current execution cycle
 * @param rx points to the receiver of the current execution cycle
 * @param jobCount is the number of channels to elaborate
 */
void DispatchDspJobs(struct DspWorkerPoolStruct *pool, struct NGeneConfigurationStruct *cfg,
                     struct NGeneDataStruct *rx, int jobCount);
#endif /* RECEIVER_DSP_WORKER_POOL_H_INCLUDED */