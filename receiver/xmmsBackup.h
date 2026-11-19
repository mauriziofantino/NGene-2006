/**
 * @file xmmsBackup.h
 *
 * @brief Per-thread backup of the XMM registers used by the classic SIMD kernels.
 *
 * @note The backup buffer is now thread storage: every thread that executes a
 *       kernel based on the classic SIMD ABI (arrays, data_shift, sige, sigeV3)
 *       must own its own backup, allocated with InitXmmsBackup() from that thread.
 *       Stage 1 of the multi-core DSP pool: while the DSP remains single threaded
 *       the behaviour is identical to the previous single global buffer.
 */
#ifndef RECEIVER_XMMS_BACKUP_H_INCLUDED
#define RECEIVER_XMMS_BACKUP_H_INCLUDED
#ifndef NGENE_THREAD_LOCAL
#if defined(__GNUC__) || defined(__clang__)
/* GCC and clang extension: no additional header or library is required. */
#define NGENE_THREAD_LOCAL __thread
#elif defined(_MSC_VER)
#define NGENE_THREAD_LOCAL __declspec(thread)
#else
/* No thread storage available: keep the historical single global. Valid only
 * while a single thread executes the SIMD kernels. */
#define NGENE_THREAD_LOCAL
#endif
#endif
/** @brief Backup of the XMM registers, one buffer per executing thread. */
/* Order extern first: GCC rejects a thread-storage specifier before extern. */
extern NGENE_THREAD_LOCAL char *ngeneXmmsBackup;
/** @brief Allocates the calling thread's register backup, if not allocated yet. */
int InitXmmsBackup(void);
/** @brief Releases the calling thread's register backup. */
void DeallocateXmmsBackup(void);
#endif /* RECEIVER_XMMS_BACKUP_H_INCLUDED */
