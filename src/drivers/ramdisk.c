#include "../include/drivers/ramdisk.h"

#include "../include/drivers/acpi.h"
#include "../include/drivers/nfit.h"



/* Implement global exported GUID objects. */
EFI_GUID gEfiRamdiskGuid = EFI_RAM_DISK_PROTOCOL_GUID;
EFI_GUID gEfiRamdiskVirtualDiskGuid = EFI_VIRTUAL_DISK_GUID;
EFI_GUID gEfiRamdiskVirtualCdGuid = EFI_VIRTUAL_CD_GUID;
EFI_GUID gEfiRamdiskPersistentVirtualDiskGuid = EFI_PERSISTENT_VIRTUAL_DISK_GUID;
EFI_GUID gEfiRamdiskPersistentVirtualCdGuid = EFI_PERSISTENT_VIRTUAL_CD_GUID;


STATIC
UINTN
RamdiskCurrentInstance = 0xBFA0;



/* Handle for the EFI_RAM_DISK_PROTOCOL instance. */
EFI_RAM_DISK_PROTOCOL
RAMDISK = {
    RamDiskRegister,
    RamDiskUnregister,
};


STATIC
RAMDISK_PRIVATE_DATA
mRamDiskPrivateDataTemplate = {
    RAMDISK_PRIVATE_DATA_SIGNATURE,
    NULL
};

STATIC
MEDIA_RAMDISK_DEVICE_PATH
mRamDiskDeviceNodeTemplate = {
    {
        MEDIA_DEVICE_PATH,
        MEDIA_RAM_DISK_DP,
        {
            (UINT8)(sizeof(MEDIA_RAMDISK_DEVICE_PATH)),
            (UINT8)(sizeof(MEDIA_RAMDISK_DEVICE_PATH) >> 8)
        }
    }
};

STATIC
EFI_BLOCK_IO_PROTOCOL
mRamDiskBlockIoTemplate = {
    EFI_BLOCK_IO_PROTOCOL_REVISION,
    (EFI_BLOCK_IO_MEDIA *) 0,
    RamDiskBlkIoReset,
    RamDiskBlkIoReadBlocks,
    RamDiskBlkIoWriteBlocks,
    RamDiskBlkIoFlushBlocks
};

/* The EFI_BLOCK_IO_PROTOCOL2 instance that is installed onto
    the handle for newly registered RAM disks. */
STATIC
EFI_BLOCK_IO2_PROTOCOL
mRamDiskBlockIo2Template = {
    (EFI_BLOCK_IO_MEDIA *) 0,
    RamDiskBlkIo2Reset,
    RamDiskBlkIo2ReadBlocksEx,
    RamDiskBlkIo2WriteBlocksEx,
    RamDiskBlkIo2FlushBlocksEx
};



/**
 * Publish the SSDT table containing a root NVDIMM device. This assumes
 *  the presence of an NVDIMM Root Device has already been checked.
 * 
 * @returns Whether the operation succeeded.
 */
STATIC
EFI_STATUS
RamDiskPublishSsdt(VOID)
{
    EFI_STATUS Status;
    UINTN DummySsdtTableKey = 0;   /* don't care about preserving this returned value */

    EXTERN unsigned char NvdimmRootAml;
    EXTERN unsigned int NvdimmRootAmlLength;

    EFI_ACPI_TABLE_PROTOCOL *ACPI = AcpiGetInstance();
    if (NULL == ACPI) return EFI_DEVICE_ERROR;

    Status = uefi_call_wrapper(ACPI->InstallAcpiTable, 4,
                               ACPI,
                               (VOID *)&NvdimmRootAml,
                               NvdimmRootAmlLength,
                               &DummySsdtTableKey);
    return Status;
}


/**
 * Publish the given ramdisk to the ACPI NVDIMM Firmware Interface Table (NFIT).
 * 
 * @param[in]  PrivateData  A pointer to some existing ramdisk data meta-structure.
 * 
 * @returns Whether the publishing operation succeeded.
 */
STATIC
EFI_STATUS
RamDiskPublishNfit(IN RAMDISK_PRIVATE_DATA *PrivateData)
{
    EFI_STATUS Status = EFI_SUCCESS;
    EFI_ACPI_TABLE_PROTOCOL *ACPI;
    UINTN TableKey;
    EFI_ACPI_DESCRIPTION_HEADER *NfitHeader;
    EFI_ACPI_NFIT_SPA_STRUCTURE *SpaRange;
    VOID *Nfit;
    UINT32 NfitLen;

    ACPI = AcpiGetInstance();
    if (NULL == ACPI) {
        return EFI_NOT_FOUND;
    }

    /* Assume that if no NFIT is in the ACPI table, then there is no NVDIMM 
          device in the \SB scope. So report one via the SSDT. */
    /* TODO! Determine if one exists already and append the SPA. */
    ERRCHECK(RamDiskPublishSsdt());

    NfitLen = sizeof(EFI_ACPI_SDT_NFIT) + sizeof(EFI_ACPI_NFIT_SPA_STRUCTURE);
    Nfit = AllocateZeroPool(NfitLen);
    if (NULL == Nfit) {
        return EFI_OUT_OF_RESOURCES;
    }

    SpaRange = (EFI_ACPI_NFIT_SPA_STRUCTURE *)
        ((EFI_PHYSICAL_ADDRESS)Nfit + sizeof(EFI_ACPI_NFIT_SPA_STRUCTURE));

    UINT8 PcdAcpiDefaultOemId[6] = { 'X', 'M', 'I', 'T', ' ', ' ' };

    NfitHeader                  = (EFI_ACPI_DESCRIPTION_HEADER *)Nfit;
    NfitHeader->Signature       = EFI_ACPI_NFIT_SIGNATURE;
    NfitHeader->Length          = NfitLen;
    NfitHeader->Revision        = EFI_ACPI_NFIT_REVISION;
    NfitHeader->OemRevision     = MFTAH_RELEASE_DATE;   /* OEM Revision by some MFTAH release date. */
    NfitHeader->CreatorId       = MFTAH_CREATOR_ID;
    NfitHeader->CreatorRevision = 0x1;   /* Should always be 1. */
    NfitHeader->OemTableId      = MFTAH_OEM_TABLE_ID;
    CopyMem(NfitHeader->OemId, PcdAcpiDefaultOemId, sizeof(NfitHeader->OemId));

    /* Fill in the content of the SPA Range Structure. */
    SpaRange->Type                             = NFIT_TABLE_TYPE_SPA;
    SpaRange->Length                           = sizeof(EFI_ACPI_NFIT_SPA_STRUCTURE);
    SpaRange->SystemPhysicalAddressRangeBase   = PrivateData->StartingAddr;
    SpaRange->SystemPhysicalAddressRangeLength = PrivateData->Size;
    CopyMem(&SpaRange->AddressRangeTypeGUID, &PrivateData->TypeGuid, sizeof(EFI_GUID));

    /* Finally, calculate the checksum of the NFIT table. */
    AcpiChecksumTable((EFI_ACPI_DESCRIPTION_HEADER *)Nfit);

    /* Publish the NFIT to the ACPI table. */
    Status = uefi_call_wrapper(ACPI->InstallAcpiTable, 4,
                               ACPI,
                               Nfit,
                               NfitHeader->Length,
                               &TableKey);

    FreePool(Nfit);
    return Status;
}


/**
 * Initialize the ramdisk device node.
 *
 * @param[in]      PrivateData     Points to RAM disk private data.
 * @param[in, out] RamDiskDevNode  Points to the RAM disk device node.
 *
 */
STATIC
VOID
RamDiskInitDeviceNode(IN RAMDISK_PRIVATE_DATA *PrivateData,
                      IN OUT MEDIA_RAMDISK_DEVICE_PATH *RamDiskDevNode)
{
    UINT64 EndingAddr = PrivateData->StartingAddr + PrivateData->Size - 1;

    CopyMem((UINT64 *)&(RamDiskDevNode->StartingAddr[0]),
            &PrivateData->StartingAddr,
            sizeof(UINT64));
    CopyMem((UINT64 *)&(RamDiskDevNode->EndingAddr[0]),
            &EndingAddr,
            sizeof(UINT64));
    CopyMem(&RamDiskDevNode->TypeGuid,
            &PrivateData->TypeGuid,
            sizeof(EFI_GUID));

    RamDiskDevNode->Instance = PrivateData->InstanceNumber;
}


EFI_STATUS
EFIAPI
RamDiskRegister(IN UINT64 RamDiskBase,
                IN UINT64 RamDiskSize,
                IN EFI_GUID *RamDiskType,
                IN EFI_DEVICE_PATH *ParentDevicePath OPTIONAL,
                OUT EFI_DEVICE_PATH_PROTOCOL **DevicePath)
{
    EFI_STATUS                      Status;
    RAMDISK_PRIVATE_DATA            *PrivateData;
    RAMDISK_PRIVATE_DATA            *RegisteredPrivateData;
    MEDIA_RAMDISK_DEVICE_PATH       *RamDiskDevNode;
    UINTN                           DevicePathSizeInt;
    LIST_ENTRY                      *Entry;

    if ((0 == RamDiskSize) || (NULL == RamDiskType) || (NULL == DevicePath)) {
        EFI_DANGERLN("\r\nNo ramdisk or ramdisk device path was found.");
        return EFI_NOT_FOUND;
    }

    /* Add a check to prevent data read across the memory boundary. */
    if (0 != (RamDiskSize % RAM_DISK_BLOCK_SIZE)) {
        EFI_DANGERLN(
            "\r\nThe ramdisk size of '%d' is not a multiple of block size '%d' (R: %d).",
            RamDiskSize,
            RAM_DISK_BLOCK_SIZE,
            (RamDiskSize % RAM_DISK_BLOCK_SIZE)
        );
        return EFI_BAD_BUFFER_SIZE;
    }

    if (
        RamDiskSize >= UINT64_MAX
        || RamDiskBase > ((UINT64_MAX - RamDiskSize) + 1)
    ) {
        EFI_DANGERLN("\r\nThe ramdisk is misaligned or exceeds 64-bit memory boundaries.");
        return EFI_INVALID_PARAMETER;
    }

    RamDiskDevNode = NULL;

    /* Initialize the loaded ramdisk's structure. */
    PrivateData = (RAMDISK_PRIVATE_DATA *)AllocateZeroPool(sizeof(RAMDISK_PRIVATE_DATA));
    if (NULL == PrivateData) {
        return EFI_OUT_OF_RESOURCES;
    }

    CopyMem(PrivateData, &mRamDiskPrivateDataTemplate, sizeof(RAMDISK_PRIVATE_DATA));
    CopyMem(&PrivateData->TypeGuid, RamDiskType, sizeof(EFI_GUID));
    PrivateData->StartingAddr = RamDiskBase;
    PrivateData->Size         = RamDiskSize;

    /* Set an incremental ramdisk instance number to identify it in device paths. */
    ++RamdiskCurrentInstance;
    PrivateData->InstanceNumber = RamdiskCurrentInstance;

    /* Generate device path information for the ramdisk. */
    RamDiskDevNode = (MEDIA_RAMDISK_DEVICE_PATH *)AllocateZeroPool(sizeof(MEDIA_RAMDISK_DEVICE_PATH));
    if (NULL == RamDiskDevNode) {
        Status = EFI_OUT_OF_RESOURCES;
        goto ErrorExit;
    }

    CopyMem(RamDiskDevNode, &mRamDiskDeviceNodeTemplate, sizeof(MEDIA_RAMDISK_DEVICE_PATH));
    RamDiskInitDeviceNode(PrivateData, RamDiskDevNode);

    *DevicePath = AppendDevicePathNode(ParentDevicePath,
                                       (EFI_DEVICE_PATH_PROTOCOL *)RamDiskDevNode);
    if (NULL == *DevicePath) {
        Status = EFI_OUT_OF_RESOURCES;
        goto ErrorExit;
    }

    PrivateData->DevicePath = *DevicePath;

    /* Fill Block IO protocol informations for the ramdisk. */
    RamDiskInitBlockIo(PrivateData);

    /* Install EFI_DEVICE_PATH_PROTOCOL & EFI_BLOCK_IO(2)_PROTOCOL on a new handle. */
    Status = uefi_call_wrapper(BS->InstallMultipleProtocolInterfaces, 8,
                               &PrivateData->Handle,
                               &gEfiBlockIoProtocolGuid,
                               &PrivateData->BlockIo,
                               &gEfiBlockIo2ProtocolGuid,
                               &PrivateData->BlockIo2,
                               &gEfiDevicePathProtocolGuid,
                               PrivateData->DevicePath,
                               NULL);
    if (EFI_ERROR(Status)) {
        goto ErrorExit;
    }

    Status = uefi_call_wrapper(BS->ConnectController, 4,
                               PrivateData->Handle,
                               NULL,
                               NULL,
                               TRUE);
    if (EFI_ERROR(Status)) {
        goto ErrorExit;
    }

    FreePool(RamDiskDevNode);

    ERRCHECK(RamDiskPublishNfit(PrivateData));

    return EFI_SUCCESS;

ErrorExit:
    if (NULL != RamDiskDevNode) {
        FreePool(RamDiskDevNode);
    }

    if (NULL != PrivateData) {
        if (PrivateData->DevicePath) {
            FreePool(PrivateData->DevicePath);
        }
        FreePool(PrivateData);
    }

    return Status;
}


/**
 * This functionality is left incomplete because it's not currently used.
 */
EFI_STATUS
EFIAPI
RamDiskUnregister(IN EFI_DEVICE_PATH_PROTOCOL *DevicePath)
{
    if (NULL == DevicePath) {
        return EFI_INVALID_PARAMETER;
    }

    uefi_call_wrapper(BS->UninstallMultipleProtocolInterfaces, 3,
                      DevicePath,
                      &gEfiRamdiskGuid,
                      NULL);

    return EFI_SUCCESS;
}


EFI_STATUS
EFIAPI
RamdiskDriverInit(IN EFI_HANDLE ImageHandle)
{
    return uefi_call_wrapper(BS->InstallMultipleProtocolInterfaces, 4,
                             &ImageHandle,
                             &gEfiRamdiskGuid,
                             &RAMDISK,
                             NULL);
}


VOID
EFIAPI
RamDiskInitBlockIo(IN RAMDISK_PRIVATE_DATA *PrivateData)
{
    EFI_BLOCK_IO_PROTOCOL           *BlockIo;
    EFI_BLOCK_IO2_PROTOCOL          *BlockIo2;
    EFI_BLOCK_IO_MEDIA              *Media;

    BlockIo  = &PrivateData->BlockIo;
    BlockIo2 = &PrivateData->BlockIo2;
    Media    = &PrivateData->Media;

    CopyMem(BlockIo, &mRamDiskBlockIoTemplate, sizeof(EFI_BLOCK_IO_PROTOCOL));
    CopyMem(BlockIo2, &mRamDiskBlockIo2Template, sizeof(EFI_BLOCK_IO2_PROTOCOL));

    BlockIo->Media          = Media;
    BlockIo2->Media         = Media;
    Media->RemovableMedia   = FALSE;
    Media->MediaPresent     = TRUE;
    Media->LogicalPartition = FALSE;
    Media->ReadOnly         = FALSE;
    Media->WriteCaching     = FALSE;
    Media->BlockSize        = RAM_DISK_BLOCK_SIZE;
    Media->LastBlock        = DivU64x32((PrivateData->Size + RAM_DISK_BLOCK_SIZE - 1),
                                        RAM_DISK_BLOCK_SIZE,
                                        NULL) - 1;
}


EFI_STATUS
EFIAPI
RamDiskBlkIoReset(IN EFI_BLOCK_IO_PROTOCOL *This,
                  IN BOOLEAN ExtendedVerification)
{
    return EFI_SUCCESS;
}


EFI_STATUS
EFIAPI
RamDiskBlkIoReadBlocks(IN EFI_BLOCK_IO_PROTOCOL *This,
                       IN UINT32 MediaId,
                       IN EFI_LBA Lba,
                       IN UINTN BufferSize,
                       OUT VOID *Buffer)
{
    RAMDISK_PRIVATE_DATA *PrivateData;
    UINTN NumberOfBlocks;

    PrivateData = RAM_DISK_PRIVATE_FROM_BLKIO(This);

    if (MediaId != PrivateData->Media.MediaId) {
        return EFI_MEDIA_CHANGED;
    }

    if (NULL == Buffer) {
        return EFI_INVALID_PARAMETER;
    }

    if (0 == BufferSize) {
        return EFI_SUCCESS;
    }

    if ((BufferSize % PrivateData->Media.BlockSize) != 0) {
        return EFI_BAD_BUFFER_SIZE;
    }

    if (Lba > PrivateData->Media.LastBlock) {
        return EFI_INVALID_PARAMETER;
    }

    NumberOfBlocks = BufferSize / PrivateData->Media.BlockSize;
    if ((Lba + NumberOfBlocks - 1) > PrivateData->Media.LastBlock) {
        return EFI_INVALID_PARAMETER;
    }

    CopyMem(Buffer,
            (VOID *)(UINTN)(PrivateData->StartingAddr + MultU64x32(Lba, PrivateData->Media.BlockSize)),
            BufferSize);

    return EFI_SUCCESS;
}


EFI_STATUS
EFIAPI
RamDiskBlkIoWriteBlocks(IN EFI_BLOCK_IO_PROTOCOL *This,
                        IN UINT32 MediaId,
                        IN EFI_LBA Lba,
                        IN UINTN BufferSize,
                        IN VOID *Buffer)
{
    RAMDISK_PRIVATE_DATA *PrivateData;
    UINTN NumberOfBlocks;

    PrivateData = RAM_DISK_PRIVATE_FROM_BLKIO (This);

    if (MediaId != PrivateData->Media.MediaId) {
        return EFI_MEDIA_CHANGED;
    }

    if (TRUE == PrivateData->Media.ReadOnly) {
        return EFI_WRITE_PROTECTED;
    }

    if (NULL == Buffer) {
        return EFI_INVALID_PARAMETER;
    }

    if (0 == BufferSize) {
        return EFI_SUCCESS;
    }

    if ((BufferSize % PrivateData->Media.BlockSize) != 0) {
        return EFI_BAD_BUFFER_SIZE;
    }

    if (Lba > PrivateData->Media.LastBlock) {
        return EFI_INVALID_PARAMETER;
    }

    NumberOfBlocks = BufferSize / PrivateData->Media.BlockSize;
    if ((Lba + NumberOfBlocks - 1) > PrivateData->Media.LastBlock) {
        return EFI_INVALID_PARAMETER;
    }

    CopyMem((VOID *)(UINTN)(PrivateData->StartingAddr + MultU64x32(Lba, PrivateData->Media.BlockSize)),
            Buffer,
            BufferSize);

    return EFI_SUCCESS;
}


EFI_STATUS
EFIAPI
RamDiskBlkIoFlushBlocks(IN EFI_BLOCK_IO_PROTOCOL *This)
{
    return EFI_SUCCESS;
}


EFI_STATUS
EFIAPI
RamDiskBlkIo2Reset(IN EFI_BLOCK_IO2_PROTOCOL *This,
                   IN BOOLEAN ExtendedVerification)
{
    return EFI_SUCCESS;
}


EFI_STATUS
EFIAPI
RamDiskBlkIo2ReadBlocksEx(IN EFI_BLOCK_IO2_PROTOCOL *This,
                          IN UINT32 MediaId,
                          IN EFI_LBA Lba,
                          IN OUT EFI_BLOCK_IO2_TOKEN *Token,
                          IN UINTN BufferSize,
                          OUT VOID *Buffer)
{
    RAMDISK_PRIVATE_DATA *PrivateData;
    EFI_STATUS Status;

    PrivateData = RAM_DISK_PRIVATE_FROM_BLKIO2(This);

    Status = RamDiskBlkIoReadBlocks(&PrivateData->BlockIo,
                                    MediaId,
                                    Lba,
                                    BufferSize,
                                    Buffer);
    if (EFI_ERROR(Status)) {
        return Status;
    }

    /* If the caller's event is given, signal it after the memory read completes. */
    if ((NULL != Token) && (NULL != Token->Event)) {
        Token->TransactionStatus = EFI_SUCCESS;
        uefi_call_wrapper(BS->SignalEvent, 1, Token->Event);
    }

    return EFI_SUCCESS;
}


EFI_STATUS
EFIAPI
RamDiskBlkIo2WriteBlocksEx(IN EFI_BLOCK_IO2_PROTOCOL *This,
                           IN UINT32 MediaId,
                           IN EFI_LBA Lba,
                           IN OUT EFI_BLOCK_IO2_TOKEN *Token,
                           IN UINTN BufferSize,
                           IN VOID *Buffer)
{
    RAMDISK_PRIVATE_DATA *PrivateData;
    EFI_STATUS Status;

    PrivateData = RAM_DISK_PRIVATE_FROM_BLKIO2(This);

    Status = RamDiskBlkIoWriteBlocks(&PrivateData->BlockIo,
                                     MediaId,
                                     Lba,
                                     BufferSize,
                                     Buffer);
    if (EFI_ERROR(Status)) {
        return Status;
    }

    /* If the caller's event is given, signal it after the memory write completes. */
    if ((NULL != Token) && (NULL != Token->Event)) {
        Token->TransactionStatus = EFI_SUCCESS;
        uefi_call_wrapper(BS->SignalEvent, 1, Token->Event);
    }

    return EFI_SUCCESS;
}


EFI_STATUS
EFIAPI
RamDiskBlkIo2FlushBlocksEx(IN EFI_BLOCK_IO2_PROTOCOL *This,
                           IN OUT EFI_BLOCK_IO2_TOKEN *Token)
{
    RAMDISK_PRIVATE_DATA *PrivateData;

    PrivateData = RAM_DISK_PRIVATE_FROM_BLKIO2 (This);

    if (TRUE == PrivateData->Media.ReadOnly) {
        return EFI_WRITE_PROTECTED;
    }

    /* If the caller's event is given, signal it directly. */
    if ((NULL != Token) && (NULL != Token->Event)) {
        Token->TransactionStatus = EFI_SUCCESS;
        uefi_call_wrapper(BS->SignalEvent, 1, Token->Event);
    }

    return EFI_SUCCESS;
}
