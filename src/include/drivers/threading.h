#ifndef MFTAH_THREAD_H
#define MFTAH_THREAD_H

#include "../mftah_uefi.h"

INTERFACE_DECL(S_MFTAH_THREAD);

/* Mutex operations for threading. */
#define MUTEX_SYNC(x) \
    while (TRUE == (x)) { \
        DPRINTLN("Waiting for mutex..."); \
        BS->Stall(250 * 1000); \
    }
#define MUTEX_LOCK(x) \
    DPRINTLN("Locking mutex..."); \
    (x) = TRUE;
#define MUTEX_UNLOCK(x) \
    DPRINTLN("Unlocking mutex..."); \
    (x) = FALSE;



/**
 * A single multiprocessor (MP) object's current state, as tracked by the BSP (main thread).
 */
typedef
struct {
    UINTN       ProcessorNumber;
    BOOLEAN     IsBSP;
    BOOLEAN     IsEnabled;
    BOOLEAN     IsHealthy;
    BOOLEAN     IsWorking;
} MFTAH_SYSTEM_MP;

/**
 * A multiprocessing context object containing useful meta-information about system threading.
 */
typedef
struct {
    UINTN           BspProcessorNumber;
    UINTN           MpCount;
    MFTAH_SYSTEM_MP *MpList;
} MFTAH_SYSTEM_MP_CTX;



/**
 * Initialize the necessary static application variables and other platform information
 *  to realize threading capabilities where possible.
 * This is mostly useful for batchable tasks (like sequential block decryption).
 * 
 * @retval EFI_SUCCESS    The threading platform was initialized successfully.
 * @retval EFI_NOT_FOUND  It was not possible to initialize threading.
 */
EFI_STATUS
EFIAPI
InitializeThreading();


/**
 * Gets whether threading is currently enabled and/or supported by the platform.
 * 
 * This must always be called AFTER InitializeThreading().
 */
BOOLEAN
EFIAPI
IsThreadingEnabled();


/**
 * Return the maximum amount of simultaneous threads runnable on the current host.
 */
UINTN
EFIAPI
GetThreadLimit();


/**
 * Creates a new Thread object and returns its structure pointer to the caller.
 * 
 * @param[in]       Method  The method to call when executing the thread.
 * @param[in]       Context  The parameters structure to provide to the Method when called.
 * @param[in,out]   NewThread  Set to the newly allocated thread structure.
 * 
 * @retval  EFI_SUCCESS  An allocated meta-structure for the thread was assigned to NewThread.
 * @retval  EFI_NOT_FOUND  Threading is disabled or was not initialized.
 * @retval  EFI_INVALID_PARAMETER  The Method pointer is NULL.
 * @retval  EFI_OUT_OF_RESOURCES  The Thread structure could not be allocated due to resource contraints.
 * @retval  EFI_ABORTED  The thread could not be created for another reason.
 */
EFI_STATUS
EFIAPI
CreateThread(
    IN EFI_AP_PROCEDURE Method,
    IN VOID             *Context,
    IN OUT MFTAH_THREAD  *NewThread
);


/**
 * Dynamically assigns the thread to an available processor. This is a blocking
 *  call, and unavailability of system MPs will cause this method to wait until
 *  one becomes available (if 'Wait' is true).
 * 
 * @param[in]  Thread  The thread to start.
 * @param[in]  Wait  Whether to block the caller until the thread is assigned to an AP.
 * 
 * @retval  EFI_SUCCESS  The thread was successfully started.
 * @retval  EFI_NOT_FOUND  Threading is not available on this system.
 * @retval  EFI_ABORTED  The operation failed because the underlying protocol failed.
 */
EFI_STATUS
EFIAPI
StartThread(
    IN MFTAH_THREAD *Thread,
    IN BOOLEAN      Wait
);


/**
 * Called by UEFI event services once a thread is finished executing and is signaled.
 *  This needs to be appropriately registered via the Boot Services CreateEvent hook for thread contexts.
 * 
 * @param[in]  EventSource  The UEFI event causing the thread to finish.
 * @param[in]  Thread  The MFTAH_THREAD object to complete once the completion/finish event is signaled.
 */
VOID
EFIAPI
FinishThread(
    IN EFI_EVENT    EventSource,
    IN VOID         *Thread
);


/**
 * Blocks the caller until the target thread completes.
 * 
 * @param[in]  Thread  The thread to join to.
 */
VOID
EFIAPI
JoinThread(
    IN MFTAH_THREAD *Thread
);


/**
 * Destroys (i.e. frees) the data structure of the given thread.
 *  If the thread is still working, this call will block until it is not.
 * 
 * @param[in]  Thread  The thread to destroy and deallocate.
 */
VOID
EFIAPI
DestroyThread(
    IN MFTAH_THREAD *Thread
);



#endif   /* MFTAH_THREAD_H */