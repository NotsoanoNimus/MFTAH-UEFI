#include "../include/mftah_uefi.h"
#include "../include/drivers/threading.h"


/* Mutex to carefully synchronize certain thread operations. */
STATIC BOOLEAN ThreadMutex = FALSE;


/* Maintain an open handle to a loaded MP service protocol from the DXE. */
STATIC EFI_MP_SERVICES_PROTOCOL *mEfiMpServicesProtocol = NULL;

/* Keep a local structure present for all threading operations. */
STATIC MFTAH_SYSTEM_MP_CTX VOLATILE mSystemMultiprocessingContext = {0};

/* A simple test to discover whether the discovered MpServices driver provided
    by the firmware _actually_ provides the MP support it claims to. */
STATIC BOOLEAN ThreadingLitmusTest(VOID);


/* Internal method to refresh MP states for mSystemMultiprocessingContext. */
STATIC
EFI_STATUS
EFIAPI
RefreshMPs(VOID)
{
    MUTEX_SYNC(ThreadMutex);
    MUTEX_LOCK(ThreadMutex);

    EFI_STATUS Status = EFI_SUCCESS;
    EFI_PROCESSOR_INFORMATION CurrentProcessorInfo = {0};
    MFTAH_SYSTEM_MP *OldMPsList = NULL;
    
    MFTAH_SYSTEM_MP *ListOfSystemMPs = (MFTAH_SYSTEM_MP *)
        AllocateZeroPool(sizeof(MFTAH_SYSTEM_MP) * mSystemMultiprocessingContext.MpCount);
    if (NULL == ListOfSystemMPs) {
        MUTEX_UNLOCK(ThreadMutex);
        return EFI_OUT_OF_RESOURCES;
    }

    for (UINTN i = 0; i < mSystemMultiprocessingContext.MpCount; ++i) {
        Status = mEfiMpServicesProtocol->GetProcessorInfo(mEfiMpServicesProtocol,
                                                          i,
                                                          &CurrentProcessorInfo);
        if (EFI_ERROR(Status)) {
            MUTEX_UNLOCK(ThreadMutex);
            return EFI_ABORTED;
        }

        ListOfSystemMPs[i].IsBSP = !!(CurrentProcessorInfo.StatusFlag & PROCESSOR_AS_BSP_BIT);
        ListOfSystemMPs[i].IsEnabled = !!(CurrentProcessorInfo.StatusFlag & PROCESSOR_ENABLED_BIT);
        ListOfSystemMPs[i].IsHealthy = !!(CurrentProcessorInfo.StatusFlag & PROCESSOR_HEALTH_STATUS_BIT);
        ListOfSystemMPs[i].IsWorking = mSystemMultiprocessingContext.MpList[i].IsWorking;
        ListOfSystemMPs[i].ProcessorNumber = i;
    }

    OldMPsList = mSystemMultiprocessingContext.MpList;
    mSystemMultiprocessingContext.MpList = ListOfSystemMPs;
    FreePool(OldMPsList);

    MUTEX_UNLOCK(ThreadMutex);
    return EFI_SUCCESS;
}


EFI_STATUS
EFIAPI
InitializeThreading(VOID)
{
    EFI_STATUS Status = EFI_SUCCESS;

    UINTN NumberOfProcessors = 0;
    UINTN NumberOfEnabledProcessors = 0;
    UINTN BspProcessorNumber = 0;

    MFTAH_SYSTEM_MP *ListOfSystemMPs = NULL;
    EFI_PROCESSOR_INFORMATION CurrentProcessorInfo = {0};

    UINT32 HealthyStatus = (0x0 | PROCESSOR_HEALTH_STATUS_BIT);

    /* First, get the protocol to determine if MP is supported. */
    Status = BS->LocateProtocol(&gEfiMpServicesProtocolGuid,
                                NULL,
                                (VOID **)&mEfiMpServicesProtocol);
    if (EFI_ERROR(Status) || NULL == mEfiMpServicesProtocol) {
        EFI_WARNINGLN("Fetching MP protocol returned code '%d' or NULL.", Status);
        mEfiMpServicesProtocol = NULL;
        return Status;
    }

    DPRINTLN("Getting system MP count.");
    ERRCHECK(
        mEfiMpServicesProtocol->GetNumberOfProcessors(mEfiMpServicesProtocol,
                                                      &NumberOfProcessors,
                                                      &NumberOfEnabledProcessors)
    );

    DPRINTLN(
        "-- Found '%d' multiprocessor objects ('%d' enabled) to utilize.",
        NumberOfProcessors,
        NumberOfEnabledProcessors
    );

    DPRINTLN("Getting BSP MP number.");
    ERRCHECK(
        mEfiMpServicesProtocol->WhoAmI(mEfiMpServicesProtocol, &BspProcessorNumber)
    );

    DPRINTLN("--- Found BSP processor number: '%d'", BspProcessorNumber);

    ListOfSystemMPs = (MFTAH_SYSTEM_MP *)
        AllocateZeroPool(sizeof(MFTAH_SYSTEM_MP) * NumberOfProcessors);
    if (NULL == ListOfSystemMPs) return EFI_OUT_OF_RESOURCES;

    /* Iterate each processor information block and extract useful information from each. */
    DPRINTLN("Extracting processor information from each MP.");
    for (UINTN i = 0; i < NumberOfProcessors; ++i) {
        DPRINTLN("-- Read logical processor #%d", i);
        Status = mEfiMpServicesProtocol->GetProcessorInfo(mEfiMpServicesProtocol,
                                                          i,
                                                          &CurrentProcessorInfo);
        switch (Status) {
            case EFI_SUCCESS:
                break;
            case EFI_NOT_FOUND:
                EFI_WARNINGLN("Processor handle #%d does not exist on this system. Skipping.", i);
                NumberOfProcessors--;
                continue;
            case EFI_DEVICE_ERROR:
                EFI_DANGERLN("The calling processor attempting to enumerate system MPs in an AP. This is illegal!");
                break;
            case EFI_INVALID_PARAMETER:
                EFI_DANGERLN("A buffer with insufficient size was provided during system MP enumeration.");
                break;
            default:
                EFI_DANGERLN("An unknown error occurred while enumerating system MPs: '%d'", Status);
                break;
        }
        if (EFI_ERROR(Status)) {
            EFI_DANGERLN("-- Multiprocessing support disabled.");
            FreePool(ListOfSystemMPs);
            return Status;
        }

        if (!(CurrentProcessorInfo.StatusFlag & PROCESSOR_AS_BSP_BIT)) {
            Status = mEfiMpServicesProtocol->EnableDisableAP(mEfiMpServicesProtocol,
                                                             i,
                                                             TRUE,
                                                             &HealthyStatus);
            switch (Status) {
                case EFI_SUCCESS:
                    DPRINTLN("-- Enabled MP #%d", i);
                    break;
                case EFI_UNSUPPORTED:
                    EFI_DANGERLN("Failed to enable MP #%d.", i);
                    break;
                case EFI_DEVICE_ERROR:
                    EFI_WARNINGLN("Cannot enable an MP from an AP.");
                    break;
                case EFI_NOT_FOUND:
                    EFI_WARNINGLN("Processor handle #%d does not exist on this system. Not enabling.", i);
                    break;
                case EFI_INVALID_PARAMETER:
                    EFI_WARNINGLN("The BSP processor is already enabled.");
                    break;
                default: break;
            }
        }

        DPRINTLN(
            "---- MP Info: ID 0x%x (P%u:C%u:T%u) %a / %a / %a",
            CurrentProcessorInfo.ProcessorId,
            CurrentProcessorInfo.Location.Package,
            CurrentProcessorInfo.Location.Core,
            CurrentProcessorInfo.Location.Thread,
            (CurrentProcessorInfo.StatusFlag & PROCESSOR_AS_BSP_BIT)
                ? "BSP" : "AP",
            (CurrentProcessorInfo.StatusFlag & PROCESSOR_ENABLED_BIT)
                ? "Enabled" : "Disabled",
            (CurrentProcessorInfo.StatusFlag & PROCESSOR_HEALTH_STATUS_BIT)
                ? "Healthy" : "Unhealthy"
        );

        ListOfSystemMPs[i].IsBSP = !!(CurrentProcessorInfo.StatusFlag & PROCESSOR_AS_BSP_BIT);
        ListOfSystemMPs[i].IsEnabled = !!(CurrentProcessorInfo.StatusFlag & PROCESSOR_ENABLED_BIT);
        ListOfSystemMPs[i].IsHealthy = !!(CurrentProcessorInfo.StatusFlag & PROCESSOR_HEALTH_STATUS_BIT);
        ListOfSystemMPs[i].IsWorking = FALSE;
        ListOfSystemMPs[i].ProcessorNumber = i;
    }

    DPRINTLN(
        "-- Got %u processors, BSP @%u, and a list at %p.",
        NumberOfProcessors,
        BspProcessorNumber,
        ListOfSystemMPs
    );
    MEMDUMP(ListOfSystemMPs, sizeof(MFTAH_SYSTEM_MP) * NumberOfProcessors);

    /* Populate the static thread tracker variable. */
    mSystemMultiprocessingContext.MpCount = NumberOfProcessors;
    mSystemMultiprocessingContext.BspProcessorNumber = BspProcessorNumber;
    mSystemMultiprocessingContext.MpList = ListOfSystemMPs;

    /* Finally, perform a test of threading in action. If it doesn't actually work,
        then it needs to be disabled despite the presence of the driver. */
    if (!ThreadingLitmusTest()) {
        EFI_DANGERLN("-- Multiprocessing support disabled.");
        FreePool(ListOfSystemMPs);
        return EFI_LOAD_ERROR;
    }

    return EFI_SUCCESS;
}


BOOLEAN
EFIAPI
IsThreadingEnabled(VOID)
{
    return NULL != mEfiMpServicesProtocol || mSystemMultiprocessingContext.MpCount >= 1;
}


UINTN
EFIAPI
GetThreadLimit(VOID)
{
    /* Need to subtract the BSP, which never runs threads. */
    return IsThreadingEnabled()
        ? (mSystemMultiprocessingContext.MpCount - 1)
        : 0;
}


EFI_STATUS
EFIAPI
CreateThread(IN EFI_AP_PROCEDURE Method,
             IN VOID *Context,
             IN OUT MFTAH_THREAD *NewThread)
{
    EFI_STATUS Status = EFI_SUCCESS;

    if (FALSE == IsThreadingEnabled()) return EFI_LOAD_ERROR;

    if (
        NULL == Method
        || NULL == Context
        || NULL == NewThread
    ) {
        return EFI_INVALID_PARAMETER;
    }

    NewThread->Method = Method;
    NewThread->Context = Context;
    NewThread->Finished = FALSE;
    NewThread->Started = FALSE;

    Status = BS->CreateEvent((EVT_TIMER | EVT_NOTIFY_SIGNAL),
                             TPL_NOTIFY,
                             (EFI_EVENT_NOTIFY)FinishThread,
                             (VOID *)NewThread,
                             (EFI_EVENT *)&(NewThread->CompletionEvent));
    if (EFI_ERROR(Status)) {
        EFI_WARNINGLN("Unable to register thread completion event.");
        return Status;
    }

    return EFI_SUCCESS;
}


EFI_STATUS
EFIAPI
StartThread(IN MFTAH_THREAD *Thread,
            IN BOOLEAN Wait)
{
    EFI_STATUS Status = EFI_SUCCESS;

    if (FALSE == IsThreadingEnabled()) return EFI_LOAD_ERROR;

    if (Thread->Started) {
        DPRINTLN("StartThread: Already started.");
        return EFI_ALREADY_STARTED;
    }
    
    else if (Thread->Finished) {
        DPRINTLN("StartThread: Already finished.");
        return EFI_INVALID_PARAMETER;
    }

    do {
        RefreshMPs();

        for (UINTN i = 0; i < mSystemMultiprocessingContext.MpCount; ++i) {
            if (
                mSystemMultiprocessingContext.MpList[i].IsBSP
                || mSystemMultiprocessingContext.MpList[i].IsWorking
                // || !mSystemMultiprocessingContext.MpList[i].IsHealthy
            ) {
                DPRINTLN("StartThread: MP #%u is not available.", i);
                BS->Stall(5 * 1000);   /* 5ms delay */
                continue;
            }

            /* Kick off the operation and mark the AP as working. */
            DPRINTLN("StartThread: MP #%u is available. Assigning thread task.", i);
            ERRCHECK(
                mEfiMpServicesProtocol->StartupThisAP(mEfiMpServicesProtocol,
                                                      Thread->Method,
                                                      i,
                                                      Thread->CompletionEvent,
                                                      0,
                                                      (VOID *)Thread->Context,
                                                      NULL)
            );

            MUTEX_SYNC(ThreadMutex);
            MUTEX_LOCK(ThreadMutex);

            mSystemMultiprocessingContext.MpList[i].IsWorking = TRUE;

            MUTEX_UNLOCK(ThreadMutex);

            Thread->Started = TRUE;
            Thread->AssignedProcessorNumber = i;
            break;
        }
    } while (!Thread->Started && Wait);

    if (!Thread->Started) {
        return EFI_OUT_OF_RESOURCES;
    }

    return EFI_SUCCESS;
}


VOID
EFIAPI
FinishThread(IN EFI_EVENT EventSource,
             IN VOID *Thread)
{
    UINTN ProcNumber = 0;

    if (FALSE == IsThreadingEnabled()) return EFI_LOAD_ERROR;

    if (NULL == Thread) {
        EFI_WARNINGLN("FinishThread: The thread to close is NULL.");
        return;
    } else if (TRUE == ((MFTAH_THREAD *)Thread)->Finished) {
        DPRINTLN("FinishThread: The thread is already finished. Moving on...");
        return;
    }

    ProcNumber = ((MFTAH_THREAD *)Thread)->AssignedProcessorNumber;

    MUTEX_SYNC(ThreadMutex);
    MUTEX_LOCK(ThreadMutex);

    mSystemMultiprocessingContext.MpList[ProcNumber].IsWorking = FALSE;

    MUTEX_UNLOCK(ThreadMutex);

    ((MFTAH_THREAD *)Thread)->Finished = TRUE;
    DPRINTLN("Finished thread on MP #%d.", ProcNumber);

    RefreshMPs();
}


VOID
EFIAPI
JoinThread(IN MFTAH_THREAD *Thread)
{
    if (FALSE == IsThreadingEnabled()) return EFI_LOAD_ERROR;

    while (Thread->Started && !Thread->Finished) {
        BS->Stall(EFI_SECONDS_TO_MICROSECONDS(0.1));
    }

    RefreshMPs();
}


VOID
EFIAPI
DestroyThread(IN MFTAH_THREAD *Thread)
{
    if (FALSE == IsThreadingEnabled()) return EFI_LOAD_ERROR;

    while (Thread->Started && !Thread->Finished) {
        BS->Stall(EFI_SECONDS_TO_MICROSECONDS(0.01));
    }

    FreePool(Thread);
    RefreshMPs();
}



#define STARTING_LITMUS_TEST_VALUE  123
#define EXPECTED_LITMUS_TEST_VALUE  456

STATIC VOID LitmusChangeCanaryValue(VOID *Context)
{ UINTN *i = (UINTN *)Context; if (NULL != i) *i = EXPECTED_LITMUS_TEST_VALUE; }

STATIC
VOID
LitmusStartThread(EFI_EVENT EventHandle,
                  VOID *Context)
{
    MFTAH_THREAD *Thread = (MFTAH_THREAD *)Context;

    StartThread(Thread, TRUE);

    /* Wait a short time (100ms) and check the value of the canary. */
    BS->Stall(EFI_SECONDS_TO_MICROSECONDS(0.01));
    if (*((UINTN VOLATILE *)Thread->Context) == EXPECTED_LITMUS_TEST_VALUE) {
        BS->SignalEvent(EventHandle);
    }
}


STATIC
BOOLEAN
ThreadingLitmusTest(VOID)
{
    /* To test whether threading can actually work, all we have to do is
        create one reference value and have another thread modify it asynchronously.
        Instead of waiting on the value to change forever in the case that threading
        doesn't work, we can rely on UEFI's event system to time out after a certain
        period of determination. If the value comes back unmodified, then that means
        the thread didn't work. */
    UINTN VOLATILE Canary = STARTING_LITMUS_TEST_VALUE;

    EFI_STATUS Status = EFI_SUCCESS;

    EFI_EVENT RacingEvents[2] = {0};
    UINTN FiredEventIndex = -1U;

    /* Set up and start the thread which is supposed to change the canary. */
    MFTAH_THREAD *Thread = (MFTAH_THREAD *)AllocateZeroPool(sizeof(MFTAH_THREAD));
    if (NULL == Thread) return FALSE;

    Status = CreateThread(LitmusChangeCanaryValue, (VOID *)&Canary, Thread);
    if (EFI_ERROR(Status)) goto ThreadLitmusTest__Error;

    /* Create an event to kick off the thread. */
    Status = BS->CreateEvent((EVT_TIMER | EVT_NOTIFY_WAIT),
                             TPL_NOTIFY,
                             LitmusStartThread,
                             (VOID *)Thread,
                             &RacingEvents[0]);
    if (EFI_ERROR(Status)) goto ThreadLitmusTest__Error;

    /* Create a timeout event. */
    Status = BS->CreateEvent(EVT_TIMER,
                             TPL_NOTIFY,
                             NULL,
                             NULL,
                             &RacingEvents[1]);
    if (EFI_ERROR(Status)) goto ThreadLitmusTest__Error;

    /* Start the timer. */
    Status = BS->SetTimer(RacingEvents[1],
                          TimerRelative,
                          EFI_MILLISECONDS_TO_100NS(3000));
    if (EFI_ERROR(Status)) {
        BS->CloseEvent(RacingEvents[1]);
        goto ThreadLitmusTest__Error;
    }

    /* Wait for one of the two event to finish. Calling `WaitForEvent` should start the thread. */
    BS->WaitForEvent(2, RacingEvents, &FiredEventIndex);

    /* Wrap up both events regardless of which was fired. */
    for (UINTN i = 0; i < 2; ++i) BS->CloseEvent(RacingEvents[i]);
    FreePool(Thread);

    if (1 == FiredEventIndex) {
        /* A timeout has occurred, meaning the canary wasn't updated in the required
            time. This is separate from the below return in case logging is desired
            at a later time. */
        return FALSE;
    }

    /* As long as index 0 is actually the fired one, we're safe to use threading. */
    return (0 == FiredEventIndex);

ThreadLitmusTest__Error:
    for (UINTN i = 0; i < 2; ++i) {
        if (NULL != RacingEvents[i]) BS->CloseEvent(RacingEvents[i]);
    }
    FreePool(Thread);

    return FALSE;
}

#undef STARTING_LITMUS_TEST_VALUE
#undef EXPECTED_LITMUS_TEST_VALUE

