/**
 * @file xmmsBackup.c
 *
 * @brief Definition and per-thread management of the XMM registers' backup.
 */
#include <stdlib.h>
#include "ngeneConstants.h"
#include "xmmsBackup.h"

/* definition of the thread-local backup pointer (declared extern in xmmsBackup.h) */
NGENE_THREAD_LOCAL char *ngeneXmmsBackup;

int InitXmmsBackup(void)
{
    if (ngeneXmmsBackup != NULL)
        return SUCCESS_RETURN;
    if (posix_memalign((void **)&(ngeneXmmsBackup), POSIX_ALIGNMENT,
                       NUM_XMM_REGISTERS * POSIX_ALIGNMENT * sizeof(char)) != 0) {
        ngeneXmmsBackup = NULL;
        return FAILURE_RETURN;
    }
    return SUCCESS_RETURN;
}

void DeallocateXmmsBackup(void)
{
    free(ngeneXmmsBackup);
    ngeneXmmsBackup = NULL;
}
