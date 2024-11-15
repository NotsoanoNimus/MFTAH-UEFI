#include "../include/drivers/ramdisk.h"

#include "../include/drivers/acpi.h"
#include "../include/drivers/nfit.h"



/* Implement global exported GUID objects. */
EFI_GUID gEfiRamdiskGuid = EFI_RAM_DISK_PROTOCOL_GUID;
EFI_GUID gEfiRamdiskVirtualDiskGuid = EFI_VIRTUAL_DISK_GUID;
EFI_GUID gEfiRamdiskVirtualCdGuid = EFI_VIRTUAL_CD_GUID;
EFI_GUID gEfiRamdiskPersistentVirtualDiskGuid = EFI_PERSISTENT_VIRTUAL_DISK_GUID;
EFI_GUID gEfiRamdiskPersistentVirtualCdGuid = EFI_PERSISTENT_VIRTUAL_CD_GUID;

/* Instance counter for registered ramdisks. Just makes the ID non-zero. */
STATIC UINTN RamdiskCurrentInstance = 0xBFA0;

/* Track the running contents of the NFIT table.
    Used to expand as more ramdisks are loaded. */
STATIC VOID     *RamdiskNfit        = NULL;
STATIC UINT32   RamdiskNfitLength   = 0;
STATIC UINTN    RamdiskAcpiTableKey = 0;

/* External references for the NVDIMM Root Device AML bytecode. */
EXTERN unsigned char NvdimmRootAml[];
EXTERN unsigned int NvdimmRootAmlLength;



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
    UINTN DummySsdtTableKey = 0;   /* don't care about preserving this returned value */
    EFI_ACPI_TABLE_PROTOCOL *ACPI = AcpiGetInstance();

    if (NULL == ACPI) return EFI_DEVICE_ERROR;

    return ACPI->InstallAcpiTable(ACPI,
                                  (VOID *)(&NvdimmRootAml[0]),
                                  NvdimmRootAmlLength,
                                  &DummySsdtTableKey);
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
    EFI_ACPI_DESCRIPTION_HEADER *NfitHeader;
    EFI_ACPI_NFIT_SPA_STRUCTURE *SpaRange;

    ACPI = AcpiGetInstance();
    if (NULL == ACPI) {
        return EFI_NOT_FOUND;
    }

    /* On first init, create the initial NFIT structure. */
    if (NULL == RamdiskNfit) {
        /* Assume that if no NFIT is in the ACPI table, then there is no NVDIMM Root Device. */
        /* TODO! Determine if one exists already and append the SPA. */
        ERRCHECK(RamDiskPublishSsdt());

        RamdiskNfitLength = sizeof(EFI_ACPI_SDT_NFIT) + sizeof(EFI_ACPI_NFIT_SPA_STRUCTURE);
        RamdiskNfit = AllocateZeroPool(RamdiskNfitLength);
        if (NULL == RamdiskNfit) return EFI_OUT_OF_RESOURCES;

        SpaRange = (EFI_ACPI_NFIT_SPA_STRUCTURE *)
            ((EFI_PHYSICAL_ADDRESS)RamdiskNfit + sizeof(EFI_ACPI_SDT_NFIT));

        UINT32 MftahReleaseDate = EFI_SWAP_ENDIAN_32(MFTAH_RELEASE_DATE);
        UINT8 MftahCreatorId[4] = MFTAH_CREATOR_ID;
        UINT8 MftahOemTableId[8] = MFTAH_OEM_TABLE_ID;
        UINT8 MftahOemId[6] = MFTAH_OEM_ID;

        /* Fill out the base NFIT descriptor table. */
        NfitHeader                          = (EFI_ACPI_DESCRIPTION_HEADER *)RamdiskNfit;
        NfitHeader->Signature               = EFI_ACPI_NFIT_SIGNATURE;
        NfitHeader->Length                  = RamdiskNfitLength;
        NfitHeader->Revision                = EFI_ACPI_NFIT_REVISION;
        NfitHeader->CreatorRevision[0]      = 0x1;   /* Should always just be 1. */
        CopyMem(NfitHeader->OemRevision,    &MftahReleaseDate,  4);
        CopyMem(NfitHeader->CreatorId,      &MftahCreatorId,    4);
        CopyMem(NfitHeader->OemTableId,     &MftahOemTableId,   8);
        CopyMem(NfitHeader->OemId,          &MftahOemId,        6);

        /* Fill in the content of the first SPA Range Structure. */
        SpaRange->Type                              = NFIT_TABLE_TYPE_SPA;
        SpaRange->Length                            = sizeof(EFI_ACPI_NFIT_SPA_STRUCTURE);
        SpaRange->SystemPhysicalAddressRangeBase    = PrivateData->StartingAddr;
        SpaRange->SystemPhysicalAddressRangeLength  = PrivateData->Size;
        CopyMem(&SpaRange->AddressRangeTypeGUID,    &PrivateData->TypeGuid, sizeof(EFI_GUID));
    } else {
        /* Adding an additional SPA entry to the ramdisks list. */
        VOID *RamdiskNfitRealloc =
            AllocateZeroPool(RamdiskNfitLength + sizeof(EFI_ACPI_NFIT_SPA_STRUCTURE));
        if (NULL == RamdiskNfitRealloc) return EFI_OUT_OF_RESOURCES;

        CopyMem(RamdiskNfitRealloc, RamdiskNfit, RamdiskNfitLength);

        /* Remove the old NFIT entry. */
        Status = ACPI->UninstallAcpiTable(ACPI, RamdiskAcpiTableKey);
        if (EFI_ERROR(Status)) {
            EFI_DANGERLN("WARNING: RAMDISK:  Failed to remove previous ACPI NFIT table (%u).", Status);
            return Status;
        }

        /* Build a new SPA entry onto the end of the list. */
        SpaRange = (EFI_ACPI_NFIT_SPA_STRUCTURE *)
            ((EFI_PHYSICAL_ADDRESS)RamdiskNfitRealloc + RamdiskNfitLength);

        SpaRange->Type                              = NFIT_TABLE_TYPE_SPA;
        SpaRange->Length                            = sizeof(EFI_ACPI_NFIT_SPA_STRUCTURE);
        SpaRange->SystemPhysicalAddressRangeBase    = PrivateData->StartingAddr;
        SpaRange->SystemPhysicalAddressRangeLength  = PrivateData->Size;
        CopyMem(&SpaRange->AddressRangeTypeGUID,    &PrivateData->TypeGuid, sizeof(EFI_GUID));

        /* Adjust to the pointer for the new ramdisk. */
        FreePool(RamdiskNfit);
        RamdiskNfit = RamdiskNfitRealloc;
        RamdiskNfitLength += sizeof(EFI_ACPI_NFIT_SPA_STRUCTURE);

        /* Update NFIT's `Length` field. */
        NfitHeader = (EFI_ACPI_DESCRIPTION_HEADER *)RamdiskNfit;
        NfitHeader->Length = RamdiskNfitLength;
    }

    /* Finally, calculate the checksum of the NFIT table. */
    AcpiChecksumTable((EFI_ACPI_DESCRIPTION_HEADER *)RamdiskNfit);

    /* Publish the NFIT to the ACPI table and capture the table key. */
    return ACPI->InstallAcpiTable(ACPI,
                                  RamdiskNfit,
                                  RamdiskNfitLength,
                                  &RamdiskAcpiTableKey);
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
    EFI_STATUS Status;
    RAMDISK_PRIVATE_DATA *PrivateData;
    MEDIA_RAMDISK_DEVICE_PATH *RamDiskDevNode;

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
        Status = EFI_OUT_OF_RESOURCES;
        goto ErrorExit;
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
    Status = BS->InstallMultipleProtocolInterfaces(
        &PrivateData->Handle,
        &gEfiBlockIoProtocolGuid,
        &PrivateData->BlockIo,
        &gEfiBlockIo2ProtocolGuid,
        &PrivateData->BlockIo2,
        &gEfiDevicePathProtocolGuid,
        PrivateData->DevicePath,
        NULL
    );
    if (EFI_ERROR(Status)) goto ErrorExit;

    Status = BS->ConnectController(PrivateData->Handle,
                                   NULL,
                                   NULL,
                                   TRUE);
    if (EFI_ERROR(Status)) goto ErrorExit;

    FreePool(RamDiskDevNode);

    Status = RamDiskPublishNfit(PrivateData);
    if (EFI_ERROR(Status)) goto ErrorExit;

    return Status;

ErrorExit:
    if (NULL != RamDiskDevNode) {
        FreePool(RamDiskDevNode);
    }

    if (NULL != PrivateData) {
        if (NULL != PrivateData->DevicePath) {
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

    BS->UninstallMultipleProtocolInterfaces(DevicePath,
                                            &gEfiRamdiskGuid,
                                            NULL);
    return EFI_SUCCESS;
}


EFI_STATUS
EFIAPI
RamdiskDriverInit(IN EFI_HANDLE ImageHandle)
{
    return BS->InstallMultipleProtocolInterfaces(
        &ImageHandle,
        &gEfiRamdiskGuid,
        &RAMDISK,
        NULL
    );
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
        BS->SignalEvent(Token->Event);
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
        BS->SignalEvent(Token->Event);
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
        BS->SignalEvent(Token->Event);
    }

    return EFI_SUCCESS;
}
