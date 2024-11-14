#include "../include/loaders/disk.h"
#include "../include/loaders/elf.h"
#include "../include/loaders/exe.h"
#include "../include/loaders/bin.h"

#include "../include/drivers/ramdisk.h"
#include "../include/drivers/displays.h"

#include "../include/core/util.h"



STATIC
EFIAPI
VOID
LoadImage(IN LOADER_CONTEXT *Context)
{
    /* The context contains the base address and length of a ramdisk to load
     * a target executable from (specified in the chain). Search for the file
     * then load it and execute it based on its specified sub-type. */
    /* ===== */
    EFI_STATUS Status = EFI_SUCCESS;
    EFI_DEVICE_PATH_PROTOCOL *RamdiskDevicePath = NULL;
    EFI_HANDLE RamdiskDeviceHandle = NULL;
    UINTN HandlesCount = 0;
    EFI_HANDLE *Handles = NULL;
    VOID *NestedChainloadFileBuffer = NULL;
    UINTN NestedChainloadFileSize = 0;

    /* First, set up the ramdisk through the driver. This registers it as an available SFS handle. */
    Status = RAMDISK.Register(Context->LoadedImageBase,
                              Context->LoadedImageSize,
                              &gEfiRamdiskVirtualDiskGuid,
                              NULL,
                              &RamdiskDevicePath);
    if (EFI_ERROR(Status)) {
        PANIC("Could not register the loaded ramdisk through the active protocol.");
    }

    Status  = SetEfiVarsHint(L"MFTAH__RAMDISK_BASE", (EFI_PHYSICAL_ADDRESS)&(Context->LoadedImageBase), 0);
    Status |= SetEfiVarsHint(L"MFTAH__RAMDISK_SIZE", (EFI_PHYSICAL_ADDRESS)&(Context->LoadedImageSize), 0);

    /* Loaded ramdisks should always let the operating system know where to find them (by default). */
    if (TRUE == CONFIG->RequireHints && EFI_ERROR(Status)) {
        // TODO: Use displays panic
        PANIC("Failed to set required ramdisk EFI hints.");
    }

    /* Next, find the target image to chainload and load it to another segment of reserved memory. */
    Status = uefi_call_wrapper(BS->LocateDevicePath, 3,
                               &gEfiSimpleFileSystemProtocolGuid,
                               (VOID **)&RamdiskDevicePath,
                               &RamdiskDeviceHandle);

    if (EFI_ERROR(Status)) {
        /* Try an alternative, iterative method. */
        Status = uefi_call_wrapper(BS->LocateHandleBuffer, 5,
                                   ByProtocol,
                                   &gEfiSimpleFileSystemProtocolGuid,
                                   NULL,
                                   &HandlesCount,
                                   &Handles);
        if (EFI_ERROR(Status) || NULL == Handles || 0 == HandlesCount) {
            PANIC("Failed to retrieve a list of SFS handles to iterate.");
        }

        for (UINTN i = 0; i < HandlesCount; ++i) {
            EFI_DEVICE_PATH *p = DevicePathFromHandle(Handles[i]);

            /* Compare the type, sub-type, and ramdisk starting address for the handle.
               See: https://uefi.org/specs/UEFI/2.10/10_Protocols_Device_Path_Protocol.html#ram-disk */
            if (
                    p->Type == RamdiskDevicePath->Type
                    && p->SubType == RamdiskDevicePath->SubType
                    && (
                            ((MEDIA_RAMDISK_DEVICE_PATH *) RamdiskDevicePath)->Instance
                            == ((MEDIA_RAMDISK_DEVICE_PATH *) p)->Instance
                    )
                ) RamdiskDeviceHandle = Handles[i];

            if (NULL != RamdiskDeviceHandle) break;
        }

        FreePool(Handles);

        if (NULL == RamdiskDeviceHandle) {
            PANIC("Could not seek a matching handle for the loaded ramdisk.");
        }

        /* Try to convert the handle to an SFS instance. */
        RamdiskDevicePath = DevicePathFromHandle(RamdiskDeviceHandle);
        Status = uefi_call_wrapper(BS->LocateDevicePath, 3,
                                   &gEfiSimpleFileSystemProtocolGuid,
                                   (VOID **)&RamdiskDevicePath,
                                   &RamdiskDeviceHandle);

        if (EFI_ERROR(Status)) {
            PANIC("Could not locate an SFS handle for the loaded ramdisk.");
        }
    }

    CHAR16 *TargetPath = AsciiStrToUnicode(Context->Chain->TargetPath);
    if (NULL == TargetPath) {
        PANIC("Failed to copy target path information: out of resources.");
    }

    // TODO: Put this filename sanitization in the chain's validation method.
    for (CHAR16 *p = TargetPath; *p; ++p) if (L'/' == *p) *p = L'\\';

    Status = ReadFile(RamdiskDeviceHandle,
                      TargetPath,
                      0,
                      (UINT8 **)&NestedChainloadFileBuffer,
                      &NestedChainloadFileSize,
                      FALSE,
                      EfiReservedMemoryType,
                      0,
                      0,
                      NULL);
    if (EFI_ERROR(Status)) {
        PANIC("There was a problem loading the chain's target.");
    }

    Context->LoadedImageBase = (EFI_PHYSICAL_ADDRESS)NestedChainloadFileBuffer;
    Context->LoadedImageSize = NestedChainloadFileSize;
    Context->LoadedImageDevicePath = FileDevicePath(RamdiskDeviceHandle, TargetPath);

    FreePool(TargetPath);

    /* Ramdisks are a bit different: the sub-type loader is now called on the loaded image.
     * Even though the LoadedImage properties are changed on the chain from the loader context,
     * the original ramdisk remains a loaded piece of reserved memory for the chainloaded OS to
     * use and discover through the newly-entered ACPI NFIT entry. */
    switch (Context->Chain->SubType) {
        case EXE:   ExeLoader.Load(Context);  break;
        case ELF:   ElfLoader.Load(Context);  break;
        case BIN:   BinLoader.Load(Context);  break;
        default: break;
    }
}


EFI_EXECUTABLE_LOADER DiskLoader = { .Load = LoadImage };
