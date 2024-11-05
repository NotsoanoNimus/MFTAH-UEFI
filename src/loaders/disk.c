#include "../include/loaders/disk.h"
#include "../include/loaders/elf.h"
#include "../include/loaders/exe.h"
#include "../include/loaders/bin.h"

#include "../include/drivers/ramdisk.h"

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

    /* Next, find the target image to chainload and load it to another segment of reserved memory. */
    // TODO: This relies on the loaded ramdisk having a FAT partition or SFS in the first place. Is this ok?
    Status = uefi_call_wrapper(BS->LocateDevicePath, 3,
                               &gEfiSimpleFileSystemProtocolGuid,
                               (VOID **)&RamdiskDevicePath,
                               &RamdiskDeviceHandle);
    if (EFI_ERROR(Status)) {
        PANIC("Could not find an SFS handle for the loaded ramdisk.");
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
                      NULL);
    if (EFI_ERROR(Status)) {
        PANIC("There was a problem loading the chain's target.");
    }

    Context->LoadedImageBase = (EFI_PHYSICAL_ADDRESS)NestedChainloadFileBuffer;
    Context->LoadedImageSize = NestedChainloadFileSize;

    if (NULL != Context->LoadedImageDevicePath) {
        FreePool(Context->LoadedImageDevicePath);
    }
    Context->LoadedImageDevicePath = FileDevicePath(RamdiskDeviceHandle, TargetPath);
    FreePool(TargetPath);

    /* Let the EXE loader know that the ramdisk is the invocation point. */

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
