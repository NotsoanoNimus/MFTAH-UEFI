#include "../include/core/util.h"



UINT64
EFIAPI
FileSize(IN EFI_FILE_PROTOCOL *FileHandle)
{
    UINT64 value;
    EFI_FILE_INFO *FileInfo;

    FileInfo = LibFileInfo(FileHandle);
    if (NULL == FileInfo) {
        return 0;
    }

    value = FileInfo->FileSize;

    FreePool(FileInfo);

    return value;
}


EFI_STATUS
EFIAPI
ReadFile(IN EFI_HANDLE BaseImageHandle,
         IN CONST CHAR16 *Filename,
         IN UINTN Offset,
         OUT UINT8 **OutputBuffer,
         OUT UINTN *LoadedFileSize,
         IN BOOLEAN HandleIsLoadedImage,
         IN EFI_MEMORY_TYPE AllocatedMemoryType,
         IN UINTN RoundToBlockSize,
         IN UINTN ExtraEndAllocation,
         IN PROGRESS_UPDATE_HOOK ProgressHook OPTIONAL)
{
    EFI_STATUS Status = EFI_SUCCESS;

    EFI_LOADED_IMAGE_PROTOCOL *LoadedImage = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *ImageIoHandle = NULL;

    EFI_FILE_PROTOCOL *VolumeHandle = NULL;
    EFI_FILE_PROTOCOL *LoadedFileHandle = NULL;

    UINTN ChunkReadSize = MFTAH_RAMDISK_LOAD_BLOCK_SIZE;   /* 64 KiB */
    UINT8 *Buffer = NULL;
    UINTN ActualFileSize = 0;

    if (
        NULL == BaseImageHandle
        || NULL == Filename
        || NULL == OutputBuffer
        || NULL == LoadedFileSize
    ) return EFI_INVALID_PARAMETER;

    if (TRUE == HandleIsLoadedImage) {
        /* Open Loaded Image protocol handle. */
        Status = uefi_call_wrapper(BS->HandleProtocol, 3,
                                   BaseImageHandle,
                                   &gEfiLoadedImageProtocolGuid,
                                   (VOID **)&LoadedImage);
        if (EFI_ERROR(Status)) {
            EFI_DANGERLN("Error locating Loaded Image Protocol handle (%u).", Status);
            return Status;
        }

        BaseImageHandle = LoadedImage->DeviceHandle;
    }

    /* Use the device handle from the image to open the relative volume. */
    Status = uefi_call_wrapper(BS->HandleProtocol, 3,
                               BaseImageHandle,
                               &gEfiSimpleFileSystemProtocolGuid,
                               (VOID **)&ImageIoHandle);
    if (EFI_ERROR(Status)) {
        EFI_DANGERLN("Error initializing SFS protocol handle (%u).", Status);
        return Status;
    }

    /* Open the volume directly. */
    Status = uefi_call_wrapper(ImageIoHandle->OpenVolume, 2,
                               ImageIoHandle,
                               &VolumeHandle);
    if (EFI_ERROR(Status)) {
        EFI_DANGERLN("Error opening volume handle (%u).", Status);
        return Status;
    }

    /* Now try to load the file. */
    Status = uefi_call_wrapper(VolumeHandle->Open, 5,
                               VolumeHandle,
                               &LoadedFileHandle,
                               Filename,
                               EFI_FILE_MODE_READ,
                               (EFI_FILE_READ_ONLY
                                | EFI_FILE_ARCHIVE
                                | EFI_FILE_HIDDEN
                                | EFI_FILE_SYSTEM));
    if (EFI_ERROR(Status)) {
        EFI_DANGERLN("Error opening file handle (%u).", Status);
        return Status;
    }

    /* Update the known size of the file. */
    ActualFileSize = FileSize(LoadedFileHandle);
    if (0 == ActualFileSize) {
        Status = EFI_END_OF_FILE;
        goto ReadFile__clean_up_and_exit;
    }

    if (0 != RoundToBlockSize) {
        /* Round the buffer up to the requested block size. */
        *LoadedFileSize = (ActualFileSize) + (RoundToBlockSize - (ActualFileSize % RoundToBlockSize));
    } else {
        *LoadedFileSize = ActualFileSize;
    }

    (*LoadedFileSize) += ExtraEndAllocation;

    /* Stage the buffer in memory. */
    Status = uefi_call_wrapper(BS->AllocatePool, 3,
                               AllocatedMemoryType,
                               *LoadedFileSize,
                               (VOID **)&Buffer);
    if (EFI_ERROR(Status) || NULL == Buffer) {
        Status = EFI_OUT_OF_RESOURCES;
        goto ReadFile__clean_up_and_exit;
    }

    if (0 != RoundToBlockSize || 0 != ExtraEndAllocation) {
        /* Ensure the trailing padding (which won't have the file read into it) is explicitly set to zero. */
        SetMem((VOID *)((EFI_PHYSICAL_ADDRESS)Buffer + (*LoadedFileSize) - RoundToBlockSize - ExtraEndAllocation),
                (RoundToBlockSize + ExtraEndAllocation),
                0x00);
    }
    
    /* Set the starting position to the `Offset` value. */
    Status = uefi_call_wrapper(
        LoadedFileHandle->SetPosition, 2,
        LoadedFileHandle,
        Offset
    );
    if (EFI_ERROR(Status)) {
        FreePool(Buffer);
        goto ReadFile__clean_up_and_exit;
    }

    /* Now scroll along the length of the file, reading chunks into memory. */
    for (UINTN i = 0; i < ActualFileSize; i += ChunkReadSize) {
        /* The loop should never repeat with a 0-valued ChunkReadSize. */
        if (0 == ChunkReadSize) return EFI_END_OF_FILE;

        Status = uefi_call_wrapper(
            LoadedFileHandle->Read, 3,
            LoadedFileHandle,
            &ChunkReadSize,
            (VOID *)((EFI_PHYSICAL_ADDRESS)Buffer + i)
        );

        if (EFI_ERROR(Status)) {
            FreePool(Buffer);
            goto ReadFile__clean_up_and_exit;
        }

        /* Only print the progress every so often (if the hook is given). */
        if (NULL != ProgressHook && 0 == (i % (1 << 20))) {
            ProgressHook(&i, &ActualFileSize, NULL);
        }
    }

    /* Always print 100% if the hook is enabled. */
    if (NULL != ProgressHook) {
        ProgressHook(&ActualFileSize, &ActualFileSize, NULL);
    }

    /* All done! Return the location of the buffer. */
    *OutputBuffer = Buffer;
    Status = EFI_SUCCESS;

    /* Clean up after ourselves. We're not really concerned if these fail. */
ReadFile__clean_up_and_exit:
    uefi_call_wrapper(LoadedFileHandle->Close, 1, LoadedFileHandle);
    uefi_call_wrapper(VolumeHandle->Close, 1, VolumeHandle);

    return Status;
}


EFI_STATUS
GetFileSystemHandleByVolumeName(IN CHAR16 *VolumeName,
                                OUT EFI_HANDLE *TargetHandle)
{
    if (
        NULL == VolumeName
        || 0 == StrLen(VolumeName)
        || NULL == TargetHandle
    ) return EFI_INVALID_PARAMETER;

    EFI_STATUS Status = EFI_SUCCESS;
    EFI_HANDLE *Handles = NULL;
    UINTN HandleCount = 0;
    BOOLEAN GotHandle = FALSE;

    ERRCHECK_UEFI(
        BS->LocateHandleBuffer,
        5,
        ByProtocol,
        &gEfiSimpleFileSystemProtocolGuid,
        NULL,
        &HandleCount,
        &Handles
    );

    /* Iterate the returned set of handles. */
    for (UINTN i = 0; i < HandleCount; ++i) {
        EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = NULL;
        EFI_FILE_PROTOCOL *root = NULL;

        ERRCHECK_UEFI(
            BS->HandleProtocol,
            3,
            Handles[i],
            &gEfiSimpleFileSystemProtocolGuid,
            (VOID **)&fs
        );

        ERRCHECK_UEFI(
            fs->OpenVolume,
            2,
            fs,
            &root
        );

        EFI_FILE_SYSTEM_VOLUME_LABEL_INFO *VolumeInfo = LibFileSystemVolumeLabelInfo(root);
        if (NULL == VolumeInfo) continue;

        if (0 == StriCmp(VolumeInfo->VolumeLabel, VolumeName)) {
            *TargetHandle = Handles[i];
            GotHandle = TRUE;
        }

        FreePool(VolumeInfo);
        if (TRUE == GotHandle) break;
    }

    if (FALSE == GotHandle) Status = EFI_NOT_FOUND;

    FreePool(Handles);
    return Status;
}


VOID
EFIAPI
Shutdown(IN CONST EFI_STATUS Reason)
{
    EFI_STATUS Status = uefi_call_wrapper(
        RT->ResetSystem, 4,
        EfiResetShutdown,
        Reason,
        0,
        NULL
    );

    PANIC("System execution suspended. Please shutdown or reboot manually.");
}


VOID
EFIAPI
Reboot(IN CONST EFI_STATUS Reason)
{
    EFI_STATUS Status = uefi_call_wrapper(
        RT->ResetSystem, 4,
        EfiResetCold,
        Reason,
        0,
        NULL
    );

    PANIC("System execution suspended, but failed to restart automatically. Please reboot manually.");
}


BOOLEAN
EFIAPI
AsciiIsNumeric(IN CONST CHAR8 *String)
{
    if (NULL == String) return FALSE;

    /* No stdlib to help us here... */
    CHAR *x;
    for (x = String; *x; x = (CHAR *)((UINTN)x + 1)) {
        if (x == String && '-' == String[0]) continue;

        if (*x < '0' || *x > '9') return FALSE;
    }

    return TRUE;
}


/**
 * ATOI implementation for ASCII (CHAR8) strings. Does not
 *  currently work with negative numbers.
 * 
 * @param[in]   String  The numeric string to convert.
 * 
 * @returns A number from the given string. -1 on error.
 */
UINTN
EFIAPI
AsciiAtoi(IN CONST CHAR8 *String)
{
    if (NULL == String || !AsciiIsNumeric(String)) return -1U;

    UINTN sum = 0, pow = 1, len = AsciiStrLen(String), i, j;
    for (i = 0; i < len; ++i) {
        /* Since the string is interpreted LTR, the 10^j multiplier scales oppositely.
            This is because the leftmost digits are the higher-magnitude digits. */
        for (j = (len - i - 1); j > 0; --j) pow *= 10;

        sum += ((String[i] - '0') * pow);

        /* Always set this accumulator back to 1 with each iteration. */
        pow = 1;
    }

    return sum;
}


VOID
AsciiSPrint(OUT CHAR8 *Into,
            IN UINTN Length,
            IN CONST CHAR8 *Format,
            ...)
{
    va_list args;
    va_start(args, Format);
    AsciiVSPrint(Into, Length, Format, args);
    va_end(args);
}


CHAR16 *
AsciiStrToUnicode(IN CHAR8 *Src)
{
    if (NULL == Src || 0 == AsciiStrLen(Src)) return NULL;

    CHAR16 *Ret = (CHAR16 *)
        AllocateZeroPool(sizeof(CHAR16) * (AsciiStrLen(Src) + 1));
    if (NULL == Ret) return NULL;

    /* Yes, I'm aware this whole thing could be much simpler. Later ok?
        I'm busy making big assumptions that will turn out catastrophic. */
    for (UINTN i = 0; i < AsciiStrLen(Src); ++i)
        *((CHAR8 *)((EFI_PHYSICAL_ADDRESS)(Ret) + (2*i))) = Src[i];

    return Ret;
}
