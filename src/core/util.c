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
FileSizeFromPath(IN CHAR16 *Path,
                 IN EFI_HANDLE BaseImageHandle,
                 IN BOOLEAN HandleIsLoadedImage,
                 OUT UINTN *SizeOutput)
{
    if (
        NULL == BaseImageHandle
        || NULL == Path
        || NULL == SizeOutput
    ) return EFI_INVALID_PARAMETER;

    EFI_STATUS Status = EFI_SUCCESS;

    EFI_LOADED_IMAGE_PROTOCOL *LoadedImage = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *ImageIoHandle = NULL;

    EFI_FILE_PROTOCOL *VolumeHandle = NULL;
    EFI_FILE_PROTOCOL *LoadedFileHandle = NULL;
    UINTN ActualFileSize = 0;

    if (TRUE == HandleIsLoadedImage) {
        /* Open Loaded Image protocol handle. */
        Status = BS->HandleProtocol(BaseImageHandle,
                                    &gEfiLoadedImageProtocolGuid,
                                    (VOID **)&LoadedImage);
        if (EFI_ERROR(Status)) return Status;

        BaseImageHandle = LoadedImage->DeviceHandle;
    }

    /* Use the device handle from the image to open the relative volume. */
    Status = BS->HandleProtocol(BaseImageHandle,
                                &gEfiSimpleFileSystemProtocolGuid,
                                (VOID **)&ImageIoHandle);
    if (EFI_ERROR(Status)) return Status;

    /* Open the volume directly. */
    Status = ImageIoHandle->OpenVolume(ImageIoHandle, &VolumeHandle);
    if (EFI_ERROR(Status)) return Status;

    /* Now try to load the file. */
    Status = VolumeHandle->Open(VolumeHandle,
                                &LoadedFileHandle,
                                Path,
                                EFI_FILE_MODE_READ,
                                (EFI_FILE_READ_ONLY
                                 | EFI_FILE_ARCHIVE
                                 | EFI_FILE_HIDDEN
                                 | EFI_FILE_SYSTEM));
    if (EFI_ERROR(Status)) return Status;

    /* Update the known size of the file. */
    ActualFileSize = FileSize(LoadedFileHandle);
    if (0 == ActualFileSize) {
        Status = EFI_END_OF_FILE;
    }

    LoadedFileHandle->Close(LoadedFileHandle);
    VolumeHandle->Close(VolumeHandle);

    *SizeOutput = ActualFileSize;
    return Status;
}


EFI_STATUS
EFIAPI
ReadFile(IN EFI_HANDLE BaseImageHandle,
         IN CONST CHAR16 *Filename,
         IN UINTN Offset,
         IN OUT UINT8 **OutputBuffer,
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
        Status = BS->HandleProtocol(BaseImageHandle,
                                    &gEfiLoadedImageProtocolGuid,
                                    (VOID **)&LoadedImage);
        if (EFI_ERROR(Status)) {
            EFI_DANGERLN("Error locating Loaded Image Protocol handle (%u).", Status);
            return Status;
        }

        BaseImageHandle = LoadedImage->DeviceHandle;
    }

    /* Use the device handle from the image to open the relative volume. */
    Status = BS->HandleProtocol(BaseImageHandle,
                                &gEfiSimpleFileSystemProtocolGuid,
                                (VOID **)&ImageIoHandle);
    if (EFI_ERROR(Status)) {
        EFI_DANGERLN("Error initializing SFS protocol handle (%u).", Status);
        return Status;
    }

    /* Open the volume directly. */
    Status = ImageIoHandle->OpenVolume(ImageIoHandle, &VolumeHandle);
    if (EFI_ERROR(Status)) {
        EFI_DANGERLN("Error opening volume handle (%u).", Status);
        return Status;
    }

    /* Now try to load the file. */
    Status = VolumeHandle->Open(VolumeHandle,
                                &LoadedFileHandle,
                                Filename,
                                EFI_FILE_MODE_READ,
                                (EFI_FILE_READ_ONLY
                                 | EFI_FILE_ARCHIVE
                                 | EFI_FILE_HIDDEN
                                 | EFI_FILE_SYSTEM));
    if (EFI_ERROR(Status)) {
        EFI_DANGERLN("Error opening file handle '%s' (%u).", Filename, Status);
        return Status;
    }

    /* Update the known size of the file. */
    ActualFileSize = FileSize(LoadedFileHandle);
    if (0 == ActualFileSize) {
        Status = EFI_END_OF_FILE;
        goto ReadFile__clean_up_and_exit;
    }

    if (NULL == *OutputBuffer) {
        if (0 != RoundToBlockSize) {
            /* Round the buffer up to the requested block size. */
            *LoadedFileSize = (ActualFileSize) + (RoundToBlockSize - (ActualFileSize % RoundToBlockSize));
        } else {
            *LoadedFileSize = ActualFileSize;
        }

        (*LoadedFileSize) += ExtraEndAllocation;

        /* Stage the buffer in memory. */
        Status = BS->AllocatePool(AllocatedMemoryType, *LoadedFileSize, (VOID **)&Buffer);
        if (EFI_ERROR(Status) || NULL == Buffer) {
            if (EFI_SUCCESS == Status) Status = EFI_ABORTED;
            goto ReadFile__clean_up_and_exit;
        }

        if (0 != RoundToBlockSize || 0 != ExtraEndAllocation) {
            /* Ensure the trailing padding (which won't have the file read into it) is explicitly set to zero. */
            SetMem((VOID *)((EFI_PHYSICAL_ADDRESS)Buffer + (*LoadedFileSize) - RoundToBlockSize - ExtraEndAllocation),
                   (RoundToBlockSize + ExtraEndAllocation),
                   0x00);
        }
    } else {
        Buffer = *OutputBuffer;
    }
    
    /* Set the starting position to the `Offset` value. */
    Status = LoadedFileHandle->SetPosition(LoadedFileHandle, Offset);
    if (EFI_ERROR(Status)) {
        FreePool(Buffer);
        goto ReadFile__clean_up_and_exit;
    }

    /* Now scroll along the length of the file, reading chunks into memory. */
    for (UINTN i = 0; i < ActualFileSize; i += ChunkReadSize) {
        /* The loop should never repeat with a 0-valued ChunkReadSize. */
        if (0 == ChunkReadSize) return EFI_END_OF_FILE;

        Status = LoadedFileHandle->Read(LoadedFileHandle,
                                        &ChunkReadSize,
                                        (VOID *)((EFI_PHYSICAL_ADDRESS)Buffer + i));
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
    LoadedFileHandle->Close(LoadedFileHandle);
    VolumeHandle->Close(VolumeHandle);

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

    ERRCHECK(
        BS->LocateHandleBuffer(ByProtocol,
                               &gEfiSimpleFileSystemProtocolGuid,
                               NULL,
                               &HandleCount,
                               &Handles)
    );

    /* Iterate the returned set of handles. */
    for (UINTN i = 0; i < HandleCount; ++i) {
        EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = NULL;
        EFI_FILE_PROTOCOL *root = NULL;

        ERRCHECK(
            BS->HandleProtocol(Handles[i],
                               &gEfiSimpleFileSystemProtocolGuid,
                               (VOID **)&fs)
        );

        ERRCHECK(fs->OpenVolume(fs, &root));

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
__attribute__((optnone))
SecureWipe(IN VOID *Buffer,
           IN UINTN Length)
{
    for (UINTN i = 0; i < 3; ++i) SetMem(Buffer, Length, 0xA5);   /* 10100101 */
    for (UINTN i = 0; i < 3; ++i) SetMem(Buffer, Length, 0x5A);   /* 01011010 */
}


EFI_STATUS
EFIAPI
SetEfiVarsHint(IN CHAR16 *VariableName,
               IN EFI_PHYSICAL_ADDRESS Value,
               IN UINTN ExplicitAccess OPTIONAL)
{
    return ST->RuntimeServices
        ->SetVariable(VariableName,
                      &gXmitVendorGuid,
                      (0 != ExplicitAccess
                          ? ExplicitAccess
                          : (EFI_VARIABLE_RUNTIME_ACCESS | EFI_VARIABLE_BOOTSERVICE_ACCESS)),
                      sizeof(EFI_PHYSICAL_ADDRESS),
                      (VOID *)Value);
}


VOID
EFIAPI
Shutdown(IN CONST EFI_STATUS Reason)
{
    EFI_STATUS Status = RT->ResetSystem(EfiResetShutdown, Reason, 0, NULL);
    PANIC("System execution suspended. Please shutdown or reboot manually.");
}


VOID
EFIAPI
Reboot(IN CONST EFI_STATUS Reason)
{
    EFI_STATUS Status = RT->ResetSystem(EfiResetCold, Reason, 0, NULL);
    PANIC("System execution suspended, but failed to restart automatically. Please reboot manually.");
}


EFI_STATUS
EFIAPI
GetMemoryMap(OUT EFI_MEMORY_MAP_META *Map)
{
    if (NULL == Map) return EFI_INVALID_PARAMETER;

    EFI_STATUS Status = EFI_SUCCESS;

    Status = BS->GetMemoryMap(&(Map->MemoryMapSize),
                              Map->BaseDescriptor,
                              &(Map->MapKey),
                              &(Map->DescriptorSize),
                              &(Map->DescriptorVersion));
    if (EFI_SUCCESS != Status && EFI_BUFFER_TOO_SMALL != Status) {
        return Status;
    }

    if (0 == Map->MemoryMapSize || 0 == Map->DescriptorSize) {
        return EFI_NOT_FOUND;
    }

    /* Account for the possible fragmentation/expansion of the memory map based on the below allocation. */
    Map->MemoryMapSize += (2 * Map->DescriptorSize);
    /* NOTE: The size is 2x because a new range inserted into the middle of an existing entry will result
        in 2 new entries: the allocation as well as the trailing region/fragment. */

    Map->BaseDescriptor = (EFI_MEMORY_DESCRIPTOR *)AllocateZeroPool(Map->MemoryMapSize);
    if (NULL == Map->BaseDescriptor) return EFI_OUT_OF_RESOURCES;

    Status = BS->GetMemoryMap(&(Map->MemoryMapSize),
                              Map->BaseDescriptor,
                              &(Map->MapKey),
                              &(Map->DescriptorSize),
                              &(Map->DescriptorVersion));
    if (EFI_ERROR(Status)) return Status;

    return EFI_SUCCESS;
}


VOID
EFIAPI
FinalizeExitBootServices(IN EFI_MEMORY_MAP_META *Meta)
{
    if (NULL == ST || NULL == Meta) return;

    /* Set the virtual address mapping for an OS to acquire and rearrange via Runtime Services.
        We shouldn't concern ourselves with the return code too much: the runtime OS can figure
        out any further accesses or virtual memory mapping. */
    // TODO: Don't think this is actually necessary.
    // RT->SetVirtualAddressMap(Meta->MemoryMapSize,
    //                          Meta->DescriptorSize,
    //                          Meta->DescriptorVersion,
    //                          Meta->BaseDescriptor);

    /* Wipe out key entries in the System Table, according to the UEFI spec. */
    ST->ConsoleInHandle = NULL;
    ST->ConsoleOutHandle = NULL;
    ST->StandardErrorHandle = NULL;
    ST->ConIn = NULL;
    ST->ConOut = NULL;
    ST->StdErr = NULL;
    ST->BootServices = NULL;

    /* Recompute the CRC32 of the EFI System Table (since the table has been modified). */
    SetCrc(&(ST->Hdr));

    /* Nullify the global reference to the Boot Services interface. */
    BS = NULL;
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


CHAR8 *
UnicodeStrToAscii(IN CHAR16 *Src)
{
    if (NULL == Src || 0 == StrLen(Src)) return NULL;

    CHAR8 *Ret = (CHAR8 *)
        AllocateZeroPool(sizeof(CHAR8) * (StrLen(Src) + 1));
    if (NULL == Ret) return NULL;

    for (UINTN i = 0; i < StrLen(Src); ++i)
        Ret[i] = *((CHAR8 *)&(Src[i]));

    return Ret;
}
