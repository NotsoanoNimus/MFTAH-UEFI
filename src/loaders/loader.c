#include "../include/loaders/loader.h"

#include "../include/loaders/disk.h"
#include "../include/loaders/exe.h"
#include "../include/loaders/elf.h"
#include "../include/loaders/bin.h"

#include "../include/drivers/displays.h"
#include "../include/drivers/mftah_adapter.h"
#include "../include/drivers/ramdisk.h"
#include "../include/drivers/threading.h"

#include "../include/core/input.h"
#include "../include/core/util.h"

#include "../include/mftah_uefi.h"



typedef
struct {
    CHAIN_TYPE              Type;
    EFI_EXECUTABLE_LOADER   *FormatHandle;
} LOADER_PAIR;


STATIC CONST CHAR8 *ProgressStatusMessage = NULL,
                   *InputErrorMessage = NULL;



/* NOTE: This needs to maintain this function signature to comply
    with the explicit MFTAH progress hook type. */
STATIC
VOID
ProgressWrapper(IN CONST UINTN *Current,
                IN CONST UINTN *OutOfTotal,
                IN VOID *Extra)
{
    DISPLAY->Progress(DISPLAY, ProgressStatusMessage, *Current, *OutOfTotal);
}


STATIC
VOID
StartLoadingAnimation(IN BOOLEAN VOLATILE *IsLoading)
{
    MFTAH_THREAD m = {0};
    EFI_STATUS Status = EFI_SUCCESS;

    Status = CreateThread(DISPLAY->AsyncLoadingAnimation, (VOID *)IsLoading, &m);
    if (EFI_ERROR(Status)) return;

    /* Kick off the thread and don't wait up for it. */
    StartThread(&m, FALSE);
}


STATIC
EFIAPI
mftah_status_t
SpawnMftahDecryptionWorkers(IN mftah_immutable_protocol_t Mftah,
                            IN mftah_work_order_t *WorkOrder,
                            IN immutable_ref_t Sha256Key,
                            IN immutable_ref_t InitializationVector,
                            IN mftah_progress_t *ProgressMeta OPTIONAL)
{
    // TODO: Branch based on threading enable flag.
    EFI_STATUS Status = EFI_SUCCESS;
    mftah_status_t MftahStatus = MFTAH_SUCCESS;
    mftah_progress_t ThreadProgress = {0};
    mftah_progress_t *ThreadProgressClone = NULL;
    mftah_work_order_t *WorkOrderClone = NULL;

    if (
        NULL == Mftah
        || NULL == WorkOrder
        || NULL == Sha256Key
        || NULL == InitializationVector
    ) {
        return MFTAH_INVALID_PARAMETER;
    }

    /* The library should never return a thread index higher than what is possible. */
    if (WorkOrder->thread_index >= MFTAH_MAX_THREAD_COUNT) {
        DISPLAY->Panic(DISPLAY,
                       "Encountered an invalid thread index while decrypting.",
                       EFI_ABORTED,
                       TRUE,
                       EFI_SECONDS_TO_MICROSECONDS(10));
    }

    if (!WorkOrder->length) return MFTAH_SUCCESS;

    /* Set up the progress hook. */
    ThreadProgress.context = NULL;
    ThreadProgress.hook = (FALSE != WorkOrder->suppress_progress)
        ? NULL
        : ProgressWrapper;
        // : (IsThreadingEnabled() ? SaveThreadProgress : ProgressWrapper);

    // if (!IsThreadingEnabled()) {
        /* This will synchronously run the operation. Each progress message is tracked individually. */
        CHAR8 *ProgressMessage =
            (CHAR8 *)AllocateZeroPool(sizeof(CHAR8) * (128 + 1));
        AsciiSPrint(ProgressMessage, 128, "Decrypting Block %u...", (WorkOrder->thread_index + 1));

        ProgressStatusMessage = ProgressMessage;

        // TODO! This fails in TEXT/NATIVE modes on certain payloads with code (20) (W-HMAC), but not in GRAPHICAL.
        MftahStatus = MFTAH_CRYPT_HOOK_DEFAULT(Mftah,
                                               WorkOrder,
                                               Sha256Key,
                                               InitializationVector,
                                               &ThreadProgress);

        FreePool(ProgressMessage);
        ProgressStatusMessage = NULL;

        return MftahStatus;
    // }

    return MFTAH_SUCCESS;
}


STATIC
VOID
MftahDecryptionSpin(IN UINT64 *QueuedBytes)
{
    // TODO: This will become easier to use when threading is figured out. For now, do nothing.
    UINT64 Progress = 0;
    // UINT64 TotalProgress = *QueuedBytes;

    return;
}


STATIC
EFI_STATUS
LoaderReadImageParts(IN LOADER_CONTEXT *Context,
                     IN EFI_HANDLE TargetHandle,
                     IN CHAR16 *PayloadPath)
{
    if (
        NULL == Context
        || NULL == TargetHandle
        || NULL == PayloadPath
        || 0 == StrLen(PayloadPath)
    ) return EFI_INVALID_PARAMETER;

    EFI_STATUS Status = EFI_SUCCESS;

    UINTN CurrentPart = 0;
    UINTN DiscoveredParts = 0;

    UINTN ImagePartSizes[10] = {0};
    UINTN TotalImageSize = 0;

    UINTN PayloadPathLength = StrLen(PayloadPath);

    /* Walk the payload fragments from .0 through and including .9. This breaks out wherever the
        chain of fragments end (the first one not found). If the .0 fragment is not found, exits
        with an EFI_NOT_FOUND error. */
    do {
        *((CHAR8 *)&(PayloadPath[PayloadPathLength - 1])) = ('0' + CurrentPart);

        Status = FileSizeFromPath(PayloadPath,
                                  TargetHandle,
                                  (TargetHandle == ENTRY_HANDLE),
                                  &(ImagePartSizes[CurrentPart]));
        if (EFI_NOT_FOUND == Status) {
            if (0 == CurrentPart) return EFI_NOT_FOUND;
            else break;
        } else if (EFI_ERROR(Status) || 0 == ImagePartSizes[CurrentPart]) return Status;

        TotalImageSize += ImagePartSizes[CurrentPart];
    } while (++CurrentPart < 10);

    /* At this point, we've discovered up to (not including) `CurrentPart` parts. */
    DiscoveredParts = CurrentPart;

    /* Reserve the total size. */
    Context->LoadedImageSize = TotalImageSize;

    Status = BS->AllocatePool(EfiReservedMemoryType, TotalImageSize, (VOID **)&(Context->LoadedImageBase));
    if (EFI_ERROR(Status)) return Status;
    else if ((EFI_PHYSICAL_ADDRESS)NULL == Context->LoadedImageBase) return EFI_OUT_OF_RESOURCES;

    /* Iterate the set of parts and load them into a consecutive block of memory. */
    UINT8 *LoadAddress = (UINT8 *)Context->LoadedImageBase;

    for (CurrentPart = 0; CurrentPart < DiscoveredParts; ++CurrentPart) {
        *((CHAR8 *)&(PayloadPath[PayloadPathLength - 1])) = ('0' + CurrentPart);

        CHAR8 *CurrentPath = (CHAR8 *)AllocateZeroPool(sizeof(CHAR8) * (128 + 1));
        if (NULL == CurrentPath) return EFI_OUT_OF_RESOURCES;

        AsciiSPrint(CurrentPath, 128, "Reading Part %u...", (CurrentPart + 1));
        ProgressStatusMessage = CurrentPath;

        Status = ReadFile(TargetHandle,
                          PayloadPath,
                          0U,
                          &LoadAddress,
                          &(ImagePartSizes[CurrentPart]),
                          (TargetHandle == ENTRY_HANDLE),
                          EfiReservedMemoryType,
                          0,
                          0,
                          ProgressWrapper);
        if (EFI_ERROR(Status)) return Status;

        LoadAddress = (UINT8 *)((EFI_PHYSICAL_ADDRESS)LoadAddress + ImagePartSizes[CurrentPart]);

        FreePool(CurrentPath);
        ProgressStatusMessage = NULL;
    }

    return EFI_SUCCESS;
}


STATIC
EFI_STATUS
LoaderReadImage(IN LOADER_CONTEXT *Context)
{
    EFI_STATUS Status = EFI_SUCCESS;
    UINTN at = 0, total = 100;

    if (NULL == Context) return EFI_INVALID_PARAMETER;

    ProgressStatusMessage = "Locating File...";

    /* Render the initial progress details and the stall art */
    DISPLAY->Progress(DISPLAY, ProgressStatusMessage, at, total);
    if (FALSE == CONFIG->Quick) DISPLAY->Stall(DISPLAY, 500);

    if (NULL == Context->Chain->PayloadPath || '\0' == *(Context->Chain->PayloadPath)) {
        return EFI_LOAD_ERROR;   /* bad/null filename */
    }

    /* Use the base image handle as the relative filesystem to load from. If a path
        is prefixed by a volume name, try to get that volume's handle instead. */
    EFI_HANDLE TargetHandle = ENTRY_HANDLE;

    CHAR8 *p = Context->Chain->PayloadPath;
    CHAR8 *s = p;

    for (; *p; ++p) {
        if (':' == *p) {
            s = p;
            break;
        }
    }

    if (s != Context->Chain->PayloadPath) {
        *s = '\0';   /* terminate the string here */
        ++s;   /* increment by one to set it to the filename */
        if ('\0' == *s) return EFI_LOAD_ERROR;

        /* Get the unicode version of the volume name string. */
        CHAR16 *VolumeName = AsciiStrToUnicode(Context->Chain->PayloadPath);

        ProgressStatusMessage = "Opening Volume...";
        at = 50; DISPLAY->Progress(DISPLAY, ProgressStatusMessage, at, total);
        if (FALSE == CONFIG->Quick) DISPLAY->Stall(DISPLAY, 500);

        Status = GetFileSystemHandleByVolumeName(VolumeName, &TargetHandle);
        FreePool(VolumeName);

        if (EFI_ERROR(Status)) return Status;
    }

    /* Set the context's device handle for chainloaded images,
            in case we're loading another EFI application. */
    Context->LoadedImageDevicePath = DevicePathFromHandle(TargetHandle);

    /* Do stuff with slight stalls between progress messages. */
    ProgressStatusMessage = "Reading Payload...";
    at = 0; DISPLAY->Progress(DISPLAY, ProgressStatusMessage, at, total);
    if (FALSE == CONFIG->Quick) DISPLAY->Stall(DISPLAY, 500);

    /* Set to FALSE by threads (if enabled) to stop the asynchronous loading animation. */
    BOOLEAN VOLATILE StillLoading = TRUE;

    /* If threading is enabled, we can keep the animation going during load
        operations, since the speed is limited by the media and not the CPU. */
    if (IsThreadingEnabled()) StartLoadingAnimation(&StillLoading);

    /* NOTE: Use the 's' starting point here because it's always set to the full file path. */
    CHAR16 *PayloadPath = AsciiStrToUnicode(s);
    if (NULL == PayloadPath) return EFI_OUT_OF_RESOURCES;

    /* Convert the path separators to the m$ version ('\'). Not doing this
        will cause `ReadFile` to return errors. */
    // TODO Move to the chain validation method from `config.c`
    for (CHAR16 *s = PayloadPath; *s; ++s) if (L'/' == *s) *s = L'\\';

    if (TRUE == Context->Chain->PayloadParts) {
        ERRCHECK(LoaderReadImageParts(Context, TargetHandle, PayloadPath));
    } else {
        Context->LoadedImageBase = 0;   /* set addr hint to NULL (so a new buffer is allocated) */
        ERRCHECK(ReadFile(TargetHandle,
                          PayloadPath,
                          0U,
                          (UINT8 **)&(Context->LoadedImageBase),
                          &(Context->LoadedImageSize),
                          (TargetHandle == ENTRY_HANDLE),
                          EfiReservedMemoryType,   /* always mark payload as reserved in e820/memmap */
                          RAM_DISK_BLOCK_SIZE,
                          (Context->Chain->IsMFTAH ? sizeof(mftah_payload_header_t) : 0),
                          ProgressWrapper));
    }

    FreePool(PayloadPath);

    /* Close out with a completed progress detail and a small stall. */
    StillLoading = FALSE;
    ProgressStatusMessage = "Success!";
    at = total;
    DISPLAY->Progress(DISPLAY, ProgressStatusMessage, at, total);
    if (FALSE == CONFIG->Quick) DISPLAY->Stall(DISPLAY, 1500);

    return EFI_SUCCESS;
}


/* NOTE: This is almost a carbon copy of the above method. */
STATIC
EFIAPI
EFI_STATUS
LoaderReadDataRamdisk(IN DATA_RAMDISK *Ramdisk,
                      IN LOADER_CONTEXT *Context)
{
    if (
        NULL == Ramdisk
        || NULL == Ramdisk->Path
        || 0 == AsciiStrLen(Ramdisk->Path)
    ) return EFI_INVALID_PARAMETER;

    EFI_STATUS Status = EFI_SUCCESS;
    // TODO Move MFTAH out to a decorator... Starting to get real messy
    mftah_status_t MftahStatus = MFTAH_SUCCESS;
    mftah_protocol_t *MftahProtocol = NULL;
    UINTN at = 0, total = 100;
    VOID *LoadedRamdiskBase = NULL;
    UINTN LoadedRamdiskSize = 0;
    EFI_DEVICE_PATH_PROTOCOL *RamdiskDevicePath = NULL;

    ProgressStatusMessage = "Locating Ramdisk...";
    DISPLAY->Progress(DISPLAY, ProgressStatusMessage, at, total);
    if (FALSE == CONFIG->Quick) DISPLAY->Stall(DISPLAY, 500);

    EFI_HANDLE TargetHandle = ENTRY_HANDLE;

    // TODO abstract to function. Keep it DRY
    CHAR8 *p = Ramdisk->Path;
    CHAR8 *s = p;

    for (; *p; ++p) {
        if (':' == *p) {
            s = p;
            break;
        }
    }

    if (s != Ramdisk->Path) {
        *s = '\0';   /* terminate the string here */
        ++s;   /* increment by one to set it to the filename */
        if ('\0' == *s) return EFI_LOAD_ERROR;

        /* Get the unicode version of the volume name string. */
        CHAR16 *VolumeName = AsciiStrToUnicode(Ramdisk->Path);

        ProgressStatusMessage = "Opening Volume...";
        at = 50; DISPLAY->Progress(DISPLAY, ProgressStatusMessage, at, total);
        if (FALSE == CONFIG->Quick) DISPLAY->Stall(DISPLAY, 500);

        Status = GetFileSystemHandleByVolumeName(VolumeName, &TargetHandle);
        FreePool(VolumeName);

        if (EFI_ERROR(Status)) return Status;
    }

    /* Do stuff with slight stalls between progress messages. */
    ProgressStatusMessage = "Reading Ramdisk...";
    at = 0; DISPLAY->Progress(DISPLAY, ProgressStatusMessage, at, total);
    if (FALSE == CONFIG->Quick) DISPLAY->Stall(DISPLAY, 500);

    /* Set to FALSE by threads (if enabled) to stop the asynchronous loading animation. */
    BOOLEAN VOLATILE StillLoading = TRUE;

    /* If threading is enabled, we can keep the animation going during load
        operations, since the speed is limited by the media and not the CPU. */
    if (IsThreadingEnabled()) StartLoadingAnimation(&StillLoading);

    /* NOTE: Use the 's' starting point here because it's always set to the full file path. */
    CHAR16 *PayloadPath = AsciiStrToUnicode(s);
    if (NULL == PayloadPath) return EFI_OUT_OF_RESOURCES;

    /* Convert the path separators to the m$ version ('\'). Not doing this
        will cause `ReadFile` to return errors. */
    for (CHAR16 *s = PayloadPath; *s; ++s) if (L'/' == *s) *s = L'\\';

    ERRCHECK(ReadFile(TargetHandle,
                      PayloadPath,
                      0U,
                      (UINT8 **)&LoadedRamdiskBase,
                      &LoadedRamdiskSize,
                      (TargetHandle == ENTRY_HANDLE),
                      EfiReservedMemoryType,   /* always mark payload as reserved in e820/memmap */
                      RAM_DISK_BLOCK_SIZE,
                      (Ramdisk->IsMFTAH ? sizeof(mftah_payload_header_t) : 0),
                      ProgressWrapper));
    FreePool(PayloadPath);

    StillLoading = FALSE;
    DISPLAY->ClearScreen(DISPLAY, CONFIG->Colors.Background);

    // TODO: MFTAH decrypt & decompression -- these types of decorators need to be moved to a more generic function/place
    // TODO This whole thing is sloppy and rushed for my own testing enjoyment.
    if (TRUE == Ramdisk->IsMFTAH) {
        if (NULL == Ramdisk->MFTAHKey || 0 == AsciiStrLen(Ramdisk->MFTAHKey)) {
            // TODO get key
            return EFI_INVALID_PASSWORD;
        }

        if (EFI_ERROR(MftahInit()) || NULL == (MftahProtocol = MftahGetInstance())) {
            EFI_DANGERLN("Failed to create a MFTAH protocol instance.");
            return EFI_OUT_OF_RESOURCES;
        }

        mftah_payload_t *PayloadWrapper = (mftah_payload_t *)AllocateZeroPool(sizeof(mftah_payload_t));
        if (NULL == PayloadWrapper) {
            EFI_DANGERLN("Failed to allocate a MFTAH payload meta object.");
            return EFI_OUT_OF_RESOURCES;
        }

        if (MFTAH_ERROR(
            MftahStatus = MftahProtocol->create_payload(MftahProtocol,
                                                       (immutable_ref_t)LoadedRamdiskBase,
                                                       (size_t)LoadedRamdiskSize,
                                                       PayloadWrapper,
                                                       NULL)
        )) {
            EFI_DANGERLN("Failed to create a MFTAH payload meta object.");
            FreePool(PayloadWrapper);
            return EFI_LOAD_ERROR;
        }

        if (MFTAH_ERROR(
            (MftahStatus = MftahProtocol->check_password(MftahProtocol,
                                                         PayloadWrapper,
                                                         Ramdisk->MFTAHKey,
                                                         AsciiStrLen(Ramdisk->MFTAHKey),
                                                         MFTAH_CRYPT_HOOK_DEFAULT,
                                                         NULL))
        )) {
            EFI_DANGERLN("Could not decrypt MFTAH ramdisk.");
            FreePool(PayloadWrapper);
            return EFI_INVALID_PASSWORD;
        }

        if (MFTAH_ERROR(
            MftahStatus = MftahProtocol->decrypt(MftahProtocol,
                                                 PayloadWrapper,
                                                 Ramdisk->MFTAHKey,
                                                 AsciiStrLen(Ramdisk->MFTAHKey),
                                                 SpawnMftahDecryptionWorkers,   /* TODO Use threading? */
                                                 MftahDecryptionSpin)
        )) {
            /* TODO: Better reasons/error messages. */
            EFI_DANGERLN("Failed to decrypt the MFTAH payload object. Code '%u'.", MftahStatus);
            FreePool(PayloadWrapper);
            return EFI_LOAD_ERROR;
        }

        /* Now that the payload is decrypted, lop off the initial 128-byte header and adjust. */
        LoadedRamdiskBase += sizeof(mftah_payload_header_t);
        LoadedRamdiskSize -= sizeof(mftah_payload_header_t);

        FreePool(PayloadWrapper);

        /* Clear the MFTAH Key location several times with garbage data. Freed later upon de-init. */
        SecureWipe(Ramdisk->MFTAHKey, AsciiStrLen(Ramdisk->MFTAHKey));
    }

    ERRCHECK(
        RAMDISK.Register((UINT64)LoadedRamdiskBase,
                         (UINT64)LoadedRamdiskSize,
                         &gEfiRamdiskVirtualDiskGuid,
                         NULL,
                         &RamdiskDevicePath)
    );

    /* Close out with a completed progress detail and a small stall. */
    ProgressStatusMessage = "Loaded!";
    at = total;
    DISPLAY->Progress(DISPLAY, ProgressStatusMessage, at, total);
    if (FALSE == CONFIG->Quick) DISPLAY->Stall(DISPLAY, 1500);

    return EFI_SUCCESS;
}


STATIC
EFIAPI
EFI_STATUS
LoaderGetAndValidateMftahKey(IN LOADER_CONTEXT *Context)
{
    EFI_STATUS Status = EFI_SUCCESS;
    mftah_status_t MftahStatus = MFTAH_SUCCESS;
    mftah_protocol_t *MftahProtocol = NULL;
    CHAR8 *p = NULL, *Password = NULL;
    UINTN PassLength = 0;
    EFI_KEY_DATA Key = {0};

    if (0 == Context->LoadedImageBase || 0 == Context->LoadedImageSize) {
        EFI_DANGERLN("Invalid base image parameters in context.");
        return EFI_BAD_BUFFER_SIZE;
    }

    Password = (CHAR8 *)AllocateZeroPool(sizeof(CHAR8) * (MFTAH_MAX_PW_LEN + 1));
    if (NULL == Password) {
        EFI_DANGERLN("Failed to allocate a MFTAH password buffer: out of memory.");
        return EFI_OUT_OF_RESOURCES;
    }

    if (EFI_ERROR(MftahInit()) || NULL == (MftahProtocol = MftahGetInstance())) {
        EFI_DANGERLN("Failed to create a MFTAH protocol instance.");
        return EFI_OUT_OF_RESOURCES;
    }

    Context->MftahPayloadWrapper = (mftah_payload_t *)AllocateZeroPool(sizeof(mftah_payload_t));
    if (NULL == Context->MftahPayloadWrapper) {
        EFI_DANGERLN("Failed to allocate a MFTAH payload meta object.");
        return EFI_OUT_OF_RESOURCES;
    }

    MftahStatus = MftahProtocol->create_payload(MftahProtocol,
                                                (immutable_ref_t)Context->LoadedImageBase,
                                                (size_t)Context->LoadedImageSize,
                                                Context->MftahPayloadWrapper,
                                                NULL);
    if (MFTAH_ERROR(MftahStatus)) {
        EFI_DANGERLN("Failed to create a MFTAH payload meta object.");
        FreePool(Context->MftahPayloadWrapper);
        return EFI_LOAD_ERROR;
    }

    if (NULL != Context->Chain->MFTAHKey && 0 < AsciiStrLen(Context->Chain->MFTAHKey)) {
        /* NOTE: This will make the password validation on autoboots much slower, since
            the HMAC is calculated before checking the password in the MFTAH `decrypt`
            method. But ehh, not a big deal. */
        FreePool(Password);
        return EFI_SUCCESS;
    }

    /* Set the 'cursor' to the base of the allocated space. */
    p = Password;

GetPassword__Repeat:
    do {
        PassLength = (p - Password);

        DISPLAY->InputPopup(DISPLAY,
                            "Enter Password:",
                            Password,
                            TRUE,
                            InputErrorMessage);

        InputErrorMessage = NULL;

GetPassword__NoRedraw:
        SetMem(&Key, sizeof(EFI_KEY_DATA), 0x00);

        if (EFI_ERROR((Status = ReadKey(&Key, 0)))) {
            DISPLAY->Panic(DISPLAY,
                           "Irrecoverable keyboard input failure.",
                           Status,
                           TRUE,
                           EFI_SECONDS_TO_MICROSECONDS(10));
            HALT;
        }

        switch (Key.Key.UnicodeChar) {
            case CHAR_BACKSPACE:
            case 0x007F: /* CHAR_DELETE: */
GetPassword__Backspace:
                if (0 == PassLength) goto GetPassword__NoRedraw;

                --p;
                *p = '\0';

                continue;

            case CHAR_CARRIAGE_RETURN:
            case CHAR_LINEFEED:
                if (0 == PassLength) goto GetPassword__NoRedraw;

                goto GetPassword__EnterKey;

            default:
                CHAR8 EnteredKey = (CHAR8)(Key.Key.UnicodeChar & 0xFF);
                if (' ' > EnteredKey || '~' < EnteredKey) {
                    if (0x0008 == Key.Key.ScanCode) goto GetPassword__Backspace;

                    continue;   /* filter unprintable (bad) characters */
                }

                if (PassLength >= MFTAH_MAX_PW_LEN) continue;

                *p = EnteredKey;   /* TODO: What about muh Unicode? :/ */
                ++p;

                continue;
        }
    } while (TRUE);

GetPassword__EnterKey:
    Password[MFTAH_MAX_PW_LEN] = '\0';

    /* Check the password. */
    if (1 == PassLength && 'q' == Password[0]) {
        /* 'q' entered by itself; user wants to quit. */
        Reboot(EFI_SUCCESS);
    }

    else if (MFTAH_ERROR(
            (MftahStatus = MftahProtocol->check_password(MftahProtocol,
                                                         Context->MftahPayloadWrapper,
                                                         Password,
                                                         PassLength,
                                                         MFTAH_CRYPT_HOOK_DEFAULT,
                                                         NULL))
    )) {
        /* Tell the user what went wrong. */
        switch (MftahStatus) {
            case MFTAH_INVALID_PASSWORD:
            case MFTAH_BAD_PW_HASH:
                InputErrorMessage = "Invalid password. Try again.";
                break;
            default:
                InputErrorMessage = "Invalid protocol parameters.";
                break;
        }

        /* Re-enter the password prompt (keeping the currently-entered password). */
        goto GetPassword__Repeat;
    }

    /* Set the password pointer and exit success. */
    Context->Chain->MFTAHKey = Password;
    return EFI_SUCCESS;
}


STATIC
EFIAPI
EFI_STATUS
LoaderMftahDecrypt(IN LOADER_CONTEXT *Context)
{
    mftah_protocol_t *MftahProtocol = NULL;
    mftah_status_t MftahStatus = MFTAH_SUCCESS;

    if (
        0 == Context->LoadedImageBase
        || 0 == Context->LoadedImageSize
        || NULL == Context->MftahPayloadWrapper
    ) {
        EFI_DANGERLN("Invalid payload or base image parameters in context.");
        return EFI_BAD_BUFFER_SIZE;
    }

    if (EFI_ERROR(MftahInit()) || NULL == (MftahProtocol = MftahGetInstance())) {
        EFI_DANGERLN("Failed to create a MFTAH protocol instance.");
        return EFI_OUT_OF_RESOURCES;
    }

    MftahStatus = MftahProtocol->decrypt(MftahProtocol,
                                         Context->MftahPayloadWrapper,
                                         Context->Chain->MFTAHKey,
                                         AsciiStrLen(Context->Chain->MFTAHKey),
                                         SpawnMftahDecryptionWorkers,
                                         MftahDecryptionSpin);
    if (MFTAH_ERROR(MftahStatus)) {
        /* TODO: Better reasons/error messages. */
        EFI_DANGERLN("Failed to decrypt the MFTAH payload object. Code '%u'.", MftahStatus);
        return EFI_LOAD_ERROR;
    }

    /* Now that the payload is decrypted, lop off the initial 128-byte header and adjust. */
    Context->LoadedImageBase += sizeof(mftah_payload_header_t);
    Context->LoadedImageSize -= sizeof(mftah_payload_header_t);

    /* Clear the MFTAH Key location several times with garbage data. Freed later upon de-init. */
    SecureWipe(Context->Chain->MFTAHKey, AsciiStrLen(Context->Chain->MFTAHKey));

    return EFI_SUCCESS;
}


STATIC
EFIAPI
EFI_STATUS
LoaderDecompress(IN LOADER_CONTEXT *Context)
{
    // TODO
    return EFI_SUCCESS;
}


VOID
LoaderDestroyContext(IN LOADER_CONTEXT *Context)
{
    /* One of the final methods called by this program. Therefore, it should also
        destroy the current framebuffer/display handle. NOTE that we don't destroy
        the Context->Chain item directly, because it's included in ConfigDestroy. We
        also don't destroy the loaded device path or ramdisk (obviously). */
    FreePool(Context->MftahPayloadWrapper);
    FreePool(Context);

    /* Destroy the MFTAH loader protocol instance. */
    MftahDestroy();

    /* Deinit the config object. We don't need it anymore. */
    ConfigDestroy();

    /* Black out the screen one last time, then destroy the display driver. */
    DISPLAY->ClearScreen(DISPLAY, 0);
    DISPLAY->Destroy(DISPLAY);
    DISPLAY = NULL;
}


VOID
LoaderEnterChain(IN UINTN SelectedChainIndex)
{
    /* Basically just free up resources and switch to the right type.
        Through this method, we're effectively just switching back to a simple text mode. */
    EFI_STATUS Status = EFI_SUCCESS;

    /* Set up the context object and send it to the right chain. */
    LOADER_CONTEXT *Context =
        (LOADER_CONTEXT *)AllocateZeroPool(sizeof(LOADER_CONTEXT));
    if (NULL == Context) {
        DISPLAY->Panic(DISPLAY,
                       "Failed to allocate loader context.",
                       EFI_OUT_OF_RESOURCES,
                       TRUE,
                       EFI_SECONDS_TO_MICROSECONDS(10));
    }

    /* Preserve the pointer to the chain in the context. */
    Context->Chain = CONFIG->Chains[SelectedChainIndex];

    /* Making access of this variable more terse. */
    CONFIG_CHAIN_BLOCK *chain = CONFIG->Chains[SelectedChainIndex];

    /* Clear the screen. */
    DISPLAY->ClearScreen(DISPLAY, CONFIG->Colors.Background);

    /* Load any data ramdisks that were specified in the chain.
        NOTE: Failure to load these is not fatal unless otherwise specified. */
    for (UINTN i = 0; i < chain->DataRamdisksLength; ++i) {
        DATA_RAMDISK *r = chain->DataRamdisks[i];

        Status = LoaderReadDataRamdisk(r, Context);

        if (EFI_ERROR(Status) && TRUE == r->IsRequired) {
            DISPLAY->Panic(DISPLAY,
                           "Could not register the required data ramdisk.",
                           Status,
                           TRUE,
                           EFI_SECONDS_TO_MICROSECONDS(10));
        }
    }

    /* Clear the screen. */
    DISPLAY->ClearScreen(DISPLAY, CONFIG->Colors.Background);

    /* Read the payload file from block storage. */
    if (EFI_ERROR((Status = LoaderReadImage(Context)))) {
        DISPLAY->Panic(DISPLAY,
                       "Failed to read the target payload.",
                       Status,
                       TRUE,
                       EFI_SECONDS_TO_MICROSECONDS(10));
    }

    /* Check the chain's properties. This occurs in a certain order. For example,
        MFTAH is always the OUTERMOST layer when compared to compression, because
        compressing AES-256 data is rather useless. */
    if (TRUE == chain->IsMFTAH) {
        /* Clear the screen. */
        DISPLAY->ClearScreen(DISPLAY, CONFIG->Colors.Background);
        InputErrorMessage = NULL;

        /* Prompt for a password if necessary. This also creates the MFTAH
            payload wrapper object. */
        if (EFI_ERROR((Status = LoaderGetAndValidateMftahKey(Context)))) {
            DISPLAY->Panic(DISPLAY,
                           "Fatal exception while capturing MFTAH key.",
                           Status,
                           TRUE,
                           EFI_SECONDS_TO_MICROSECONDS(10));
        }

        /* Clear the screen. */
        DISPLAY->ClearScreen(DISPLAY, CONFIG->Colors.Background);
        ProgressStatusMessage = "Decrypting...";

        if (EFI_ERROR((Status = LoaderMftahDecrypt(Context)))) {
            DISPLAY->Panic(DISPLAY,
                           "MFTAH decryption encountered a fatal exception.",
                           Status,
                           TRUE,
                           EFI_SECONDS_TO_MICROSECONDS(10));
        }
    }

    if (TRUE == chain->IsCompressed) {
        // TODO: Compression compatibility.
        /* Clear the screen. */
        DISPLAY->ClearScreen(DISPLAY, CONFIG->Colors.Background);
        ProgressStatusMessage = "Decompressing...";

        if (EFI_ERROR((Status = LoaderDecompress(Context)))) {
            DISPLAY->Panic(DISPLAY,
                           "Decompression returned an irrecoverable failure code.",
                           Status,
                           TRUE,
                           EFI_SECONDS_TO_MICROSECONDS(10));
        }
    }

    DISPLAY->ClearScreen(DISPLAY, CONFIG->Colors.Background);

    /* Load the image based on the chain's type. */
    switch (chain->Type) {
        case DISK:  DiskLoader .Load(Context); break;
        case EXE:   ExeLoader  .Load(Context); break;
        case ELF:   ElfLoader  .Load(Context); break;
        case BIN:   BinLoader  .Load(Context); break;
        default: break;
    }

    /* End of the road. */
    Status = EFI_NOT_STARTED;

    /* NOTE: need to use the old PANIC here because the DISPLAY was likely freed already. */
    PANIC("Could not transfer control to the right loader.");
}
