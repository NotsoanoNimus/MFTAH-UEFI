#include "../include/drivers/ramdisk.h"

#include "../include/drivers/nfit.h"



/* Implement global exported GUID objects. */
EFI_GUID gEfiRamdiskGuid = EFI_RAM_DISK_PROTOCOL_GUID;
EFI_GUID gEfiRamdiskVirtualDiskGuid = EFI_VIRTUAL_DISK_GUID;
EFI_GUID gEfiRamdiskVirtualCdGuid = EFI_VIRTUAL_CD_GUID;
EFI_GUID gEfiRamdiskPersistentVirtualDiskGuid = EFI_PERSISTENT_VIRTUAL_DISK_GUID;
EFI_GUID gEfiRamdiskPersistentVirtualCdGuid = EFI_PERSISTENT_VIRTUAL_CD_GUID;



/* Handle for the EFI_RAM_DISK_PROTOCOL instance. */
static
EFI_RAM_DISK_PROTOCOL
mRamDiskProtocol = {
    RamDiskRegister,
    RamDiskUnregister,
};


static
RAMDISK_PRIVATE_DATA
mRamDiskPrivateDataTemplate = {
    RAMDISK_PRIVATE_DATA_SIGNATURE,
    NULL
};

static
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

static
EFI_BLOCK_IO_PROTOCOL
mRamDiskBlockIoTemplate = {
    EFI_BLOCK_IO_PROTOCOL_REVISION,
    (EFI_BLOCK_IO_MEDIA *) 0,
    RamDiskBlkIoReset,
    RamDiskBlkIoReadBlocks,
    RamDiskBlkIoWriteBlocks,
    RamDiskBlkIoFlushBlocks
};

//
// The EFI_BLOCK_IO_PROTOCOL2 instances that is installed onto the handle
// for newly registered RAM disks
//
static
EFI_BLOCK_IO2_PROTOCOL
mRamDiskBlockIo2Template = {
    (EFI_BLOCK_IO_MEDIA *) 0,
    RamDiskBlkIo2Reset,
    RamDiskBlkIo2ReadBlocksEx,
    RamDiskBlkIo2WriteBlocksEx,
    RamDiskBlkIo2FlushBlocksEx
};



/**
 * Publish the SSDT table containing a root NVDIMM device.
 * 
 * @returns Whether the operation succeeded.
 */
static
EFI_STATUS
RamDiskPublishSsdt()
{
    EFI_STATUS Status = EFI_SUCCESS;
    UINTN DummySsdtTableKey = 0;

    /* An empty NVDIMM Root Device SSDT entry. Inserted only if 
          an existing NVDIMM Root Device is not already found. */
    UINT8 nvdimmRootAml[0x7C] = {0};

    DPRINTLN(L"-- Publishing RamDisk SSDT entry for ACPI.");
    MEMDUMP(nvdimmRootAml, sizeof(nvdimmRootAml));
    DPRINTLN(L"");

    /*
     * Install an SSDT entry for the ACPI table using some compiled AML.
     *   The AML comes from the RamDisk.asl file in the local folder.
     *   This is compiled on Linux using the 'iasl' tool and the resultant
     *   bytecode is placed in a static C variable above (nvdimmRootAml).
     * 
     * Compilation Command/Macro:
     *    iasl RamDisk.asl && echo && \
     *       T="$(xxd -p RamDisk.aml | tr -d '\n' | sed -r 's/(..)/0x\1, /g')" && echo && \
     *       echo "$T" | sed -r "s/((0x..,\s){8})/\1\r\n/g"
     * 
     * This code assumes that the "RamDisk " SSDT is NOT already installed
     *   at boot.
     */
    Status = uefi_call_wrapper(
        gAcpiTableProtocol->InstallAcpiTable,
        4,
        gAcpiTableProtocol,
        nvdimmRootAml,
        sizeof(nvdimmRootAml),
        &DummySsdtTableKey
    );

    return Status;
}


/**
 * Publish the given ramdisk to the ACPI NVDIMM Firmware Interface Table (NFIT).
 * 
 * @param[in]  PrivateData  A pointer to some existing ramdisk data meta-structure.
 * 
 * @returns Whether the publishing operation succeeded.
 */
static
EFI_STATUS
RamDiskPublishNfit(IN RAMDISK_PRIVATE_DATA *PrivateData)
{
    EFI_STATUS Status = EFI_SUCCESS;
    EFI_MEMORY_DESCRIPTOR *MemoryMap;
    EFI_MEMORY_DESCRIPTOR *MemoryMapEntry;
    EFI_MEMORY_DESCRIPTOR *MemoryMapEnd;
    UINTN MemoryMapSize; 
    UINTN TableIndex;
    VOID *TableHeader;
    EFI_ACPI_TABLE_VERSION TableVersion;
    UINTN TableKey;
    EFI_ACPI_DESCRIPTION_HEADER *NfitHeader;
    EFI_ACPI_6_4_NFIT_SYSTEM_PHYSICAL_ADDRESS_RANGE_STRUCTURE *SpaRange;
    VOID *Nfit;
    UINT32 NfitLen;
    UINTN MapKey;
    UINTN DescriptorSize;
    UINT32 DescriptorVersion;
    UINT64 CurrentData;
    UINT8 Checksum;
    BOOLEAN MemoryFound;

    MemoryMapSize = 0;
    MemoryMap = NULL;
    MemoryFound = FALSE;

    Status = uefi_call_wrapper(
        BS->GetMemoryMap,
        5,
        &MemoryMapSize,
        MemoryMap,
        &MapKey,
        &DescriptorSize,
        &DescriptorVersion
    );

    ASSERT(EFI_BUFFER_TOO_SMALL == Status);
    
    if (NULL == gAcpiTableProtocol) {
        return EFI_NOT_FOUND;
    }

    do {
        MemoryMap = (EFI_MEMORY_DESCRIPTOR *)AllocatePool(MemoryMapSize);
        if (NULL == MemoryMap) {
            return EFI_OUT_OF_RESOURCES;
        }

        Status = uefi_call_wrapper(
            BS->GetMemoryMap,
            5,
            &MemoryMapSize,
            MemoryMap,
            &MapKey,
            &DescriptorSize,
            &DescriptorVersion
        );
        
        if (EFI_ERROR(Status)) {
            FreePool(MemoryMap);
        }
    } while (EFI_BUFFER_TOO_SMALL == Status);

    if (EFI_ERROR(Status)) {
        return Status;
    }

    MemoryMapEntry = MemoryMap;
    MemoryMapEnd = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)MemoryMap + MemoryMapSize);

    while ((UINTN)MemoryMapEntry < (UINTN)MemoryMapEnd) {
        if (//(MemoryMapEntry->Type == EfiReservedMemoryType)
            (MemoryMapEntry->PhysicalStart <= PrivateData->StartingAddr)
            && (
                MemoryMapEntry->PhysicalStart + MultU64x32(MemoryMapEntry->NumberOfPages, EFI_PAGE_SIZE)
                    >= PrivateData->StartingAddr + PrivateData->Size
            )
        ) {
            MemoryFound = TRUE;
            break;
        }

        MemoryMapEntry = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)MemoryMapEntry + DescriptorSize);
    }

    FreePool(MemoryMap);

    if (!MemoryFound) {
        PRINTLN(L"Could not find the BootServices memory map.");
        return EFI_NOT_FOUND;
    }

    /* Assume that if no NFIT is in the ACPI table, then there is no NVDIMM 
          device in the \SB scope. So report one via the SSDT. */
    ERRCHECK(RamDiskPublishSsdt());

    /* TODO! Determine if one exists already and append the SPA. */
    DPRINTLN(L"\r\nRamDiskPublishNfit: No NFIT is in the ACPI Table, will create one.");

    NfitLen = 40 + 56;// + 48;
            //sizeof(EFI_ACPI_6_1_NVDIMM_FIRMWARE_INTERFACE_TABLE) +
            //sizeof(EFI_ACPI_6_1_NFIT_SYSTEM_PHYSICAL_ADDRESS_RANGE_STRUCTURE) +
            //sizeof(EFI_ACPI_6_1_NFIT_REGION_MAPPING_STRUCTURE);
    Nfit = AllocateZeroPool(NfitLen);
    if (NULL == Nfit) {
        return EFI_OUT_OF_RESOURCES;
    }

    SpaRange = (EFI_ACPI_6_4_NFIT_SYSTEM_PHYSICAL_ADDRESS_RANGE_STRUCTURE *)
               ((UINT8 *)Nfit + 40);

    UINT8 PcdAcpiDefaultOemId[6] = { 'X', 'M', 'I', 'T', ' ', ' ' };

    NfitHeader                  = (EFI_ACPI_DESCRIPTION_HEADER *)Nfit;
    NfitHeader->Signature       = EFI_ACPI_NFIT_SIGNATURE;
    NfitHeader->Length          = NfitLen;
    NfitHeader->Revision        = EFI_ACPI_6_4_NVDIMM_FIRMWARE_INTERFACE_TABLE_REVISION;
    NfitHeader->OemRevision     = MFTAH_RELEASE_DATE;   /* OEM Revision by some MFTAH release date. */
    NfitHeader->CreatorId       = MFTAH_CREATOR_ID;
    NfitHeader->CreatorRevision = 0x1;   /* Should always be 1. */
    NfitHeader->OemTableId      = MFTAH_OEM_TABLE_ID;
    CopyMem(NfitHeader->OemId, &PcdAcpiDefaultOemId[0], sizeof(NfitHeader->OemId));

    /* Fill in the content of the SPA Range Structure. */
    SpaRange->Type                             = EFI_ACPI_6_4_NFIT_SYSTEM_PHYSICAL_ADDRESS_RANGE_STRUCTURE_TYPE;
    SpaRange->Length                           = sizeof(EFI_ACPI_6_4_NFIT_SYSTEM_PHYSICAL_ADDRESS_RANGE_STRUCTURE);
    SpaRange->SystemPhysicalAddressRangeBase   = PrivateData->StartingAddr;
    SpaRange->SystemPhysicalAddressRangeLength = PrivateData->Size;
    CopyMem(&SpaRange->AddressRangeTypeGUID, &PrivateData->TypeGuid, sizeof(EFI_GUID));

    /* Finally, calculate the checksum of the NFIT table. */
    NfitHeader->Checksum = CalculateCheckSum8((UINT8 *)Nfit, NfitHeader->Length);

    /* Publish the NFIT to the ACPI table. */
    Status = uefi_call_wrapper(
        gAcpiTableProtocol->InstallAcpiTable,
        4,
        gAcpiTableProtocol,
        Nfit,
        NfitHeader->Length,
        &TableKey
    );

    FreePool(Nfit);

    return Status;
}


/**
 * Initialize the RAM disk device node.
 *
 * @param[in]      PrivateData     Points to RAM disk private data.
 * @param[in, out] RamDiskDevNode  Points to the RAM disk device node.
 *
 */
static
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

    ASSERT(*((UINT64 *)&RamDiskDevNode->TypeGuid) == *((UINT64 *)&PrivateData->TypeGuid));
    ASSERT(*((UINT64 *)&(RamDiskDevNode->TypeGuid.Data4[0])) == *((UINT64 *)&(PrivateData->TypeGuid.Data4[0])));

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

    DPRINT(L"REGISTER ");

    if ((0 == RamDiskSize) || (NULL == RamDiskType) || (NULL == DevicePath)) {
        PRINTLN(L"\r\nNo ramdisk or ramdisk device path was found.");
        return EFI_NOT_FOUND;
    }

    /* Add a check to prevent data read across the memory boundary. */
    if (0 != (RamDiskSize % RAM_DISK_BLOCK_SIZE)) {
        PRINTLN(
            L"\r\nThe ramdisk size of '%d' is not a multiple of block size '%d' (R: %d).",
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
        PRINTLN(L"\r\nThe ramdisk is misaligned or exceeds 64-bit memory boundaries.");
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

    /* Generate device path information for the ramdisk. */
    DPRINT(L"ALLOC2 ");
    RamDiskDevNode = (MEDIA_RAMDISK_DEVICE_PATH *)AllocateZeroPool(sizeof(MEDIA_RAMDISK_DEVICE_PATH));
    if (NULL == RamDiskDevNode) {
        Status = EFI_OUT_OF_RESOURCES;
        goto ErrorExit;
    }
    DPRINT(L"COPY2 ");
    CopyMem(RamDiskDevNode, &mRamDiskDeviceNodeTemplate, sizeof(MEDIA_RAMDISK_DEVICE_PATH));

    DPRINT(L"D_INIT ");
    RamDiskInitDeviceNode(PrivateData, RamDiskDevNode);

    DPRINT(L"\r\n");
    MEMDUMP(&mRamDiskDeviceNodeTemplate, sizeof(MEDIA_RAMDISK_DEVICE_PATH));
    DPRINT(L"\r\n");

    DPRINT(L"D_APPEND ");
    *DevicePath = AppendDevicePathNode(ParentDevicePath,
                                       (EFI_DEVICE_PATH_PROTOCOL *) RamDiskDevNode);
    if (NULL == *DevicePath) {
        Status = EFI_OUT_OF_RESOURCES;
        goto ErrorExit;
    }

    CHAR16 *v = DevicePathToStr(*DevicePath);
    DPRINT(L"\r\n %s\r\n", v);
    MEMDUMP(*DevicePath, 64);
    DPRINT(L"\r\n");
    FreePool(v);

    PrivateData->DevicePath = *DevicePath;

    /* Fill Block IO protocol informations for the ramdisk. */
    DPRINT(L"BLOCKIO ");
    RamDiskInitBlockIo(PrivateData);

    /* Install EFI_DEVICE_PATH_PROTOCOL & EFI_BLOCK_IO(2)_PROTOCOL on a new handle. */
    DPRINT(L"PROTOINST ");
    Status = uefi_call_wrapper(
        BS->InstallMultipleProtocolInterfaces,
        8,
        &PrivateData->Handle,
        &gEfiBlockIoProtocolGuid,
        &PrivateData->BlockIo,
        &gEfiBlockIo2ProtocolGuid,
        &PrivateData->BlockIo2,
        &gEfiDevicePathProtocolGuid,
        PrivateData->DevicePath,
        NULL
    );
    if (EFI_ERROR(Status)) {
        goto ErrorExit;
    }

    DPRINT(L"CONNECTCONTROLLER(%016x) ", PrivateData->Handle);
    Status = uefi_call_wrapper(
        BS->ConnectController,
        4,
        PrivateData->Handle,
        NULL,
        NULL,
        TRUE
    );
    if (EFI_ERROR(Status)) {
        goto ErrorExit;
    }

    DPRINT(
        L"\r\n\tPDH(%p) PDBH(%p) PDB2H(%p) MEDIA(%08x) ",
        PrivateData->Handle,
        PrivateData->BlockIo,
        PrivateData->BlockIo2,
        PrivateData->Media.MediaId
    );

    FreePool(RamDiskDevNode);

    if (NULL != gAcpiTableProtocol) {
        ERRCHECK(RamDiskPublishNfit(PrivateData));
    } else {
        PANIC(L"Cannot register ramdisk in ACPI NVDIMM Firmware Interface Table (NFIT).");
    }

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

    uefi_call_wrapper(
        BS->UninstallMultipleProtocolInterfaces,
        3,
        DevicePath,
        &gEfiRamdiskGuid,
        NULL
    );   

    return EFI_SUCCESS;
}


EFI_STATUS
EFIAPI
RDEntryPoint(IN EFI_HANDLE ImageHandle,
             IN EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_STATUS                      Status;
    UINT64                          *StartingAddr;
    EFI_DEVICE_PATH_PROTOCOL        *DevicePath;
    VOID                            *DummyInterface = NULL;

    /* This check is no longer done because the application should use its own RD driver. */
    // Status = uefi_call_wrapper(
    //     BS->LocateProtocol,
    //     3,
    //     &gEfiRamdiskGuid,
    //     NULL,
    //     &DummyInterface
    // );
    // if (!EFI_ERROR(Status)) {
    //     Print(L"-- A ramdisk driver already exists in the protocol database.\r\n");
    //     return EFI_ALREADY_STARTED;
    // }

    Status = uefi_call_wrapper(
        BS->InstallMultipleProtocolInterfaces,
        4,
        &ImageHandle,
        &gEfiRamdiskGuid,
        &mRamDiskProtocol,
        NULL
    );
    if (EFI_ERROR(Status)) {
        PRINTLN(L"-- Could not register protocol ID for ImageHandle.");
        return EFI_INVALID_PARAMETER;
    }

    return EFI_SUCCESS;
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
    Media->LastBlock        = DivU64x32(
                                  PrivateData->Size + RAM_DISK_BLOCK_SIZE - 1,
                                  RAM_DISK_BLOCK_SIZE,
                                  NULL
                              ) - 1;

    // Undefined behavior is produced by the 'for' loop below. And I have no idea why...
    //     Print(L"BlockSize (%d); LastBlock (%d); Remainder (%d)  ", Media->BlockSize, Media->LastBlock, Remainder);
    //   for (int i = 0; ; ++i) {
    //     Media->LastBlock = DivU64x32(PrivateData->Size, RAM_DISK_BLOCK_SIZE >> i, &Remainder) - 1;
    //     if (0 == Remainder || 0 == Media->BlockSize >> i) break;

    //     Media->BlockSize = RAM_DISK_BLOCK_SIZE >> i;
    //   }
    //     Print(L"BlockSize (%d); LastBlock (%d); Remainder (%d)  ", Media->BlockSize, Media->LastBlock, Remainder);

    //   ASSERT(Media->BlockSize != 0);
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

    PrivateData = RAM_DISK_PRIVATE_FROM_BLKIO (This);

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

    CopyMem(
        Buffer,
        (VOID *)(UINTN)(PrivateData->StartingAddr + MultU64x32(Lba, PrivateData->Media.BlockSize)),
        BufferSize
    );

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

    CopyMem(
        (VOID *)(UINTN)(PrivateData->StartingAddr + MultU64x32(Lba, PrivateData->Media.BlockSize)),
        Buffer,
        BufferSize
    );

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

    Status = RamDiskBlkIoReadBlocks(
        &PrivateData->BlockIo,
        MediaId,
        Lba,
        BufferSize,
        Buffer
    );
    if (EFI_ERROR(Status)) {
        return Status;
    }

    /* If the caller's event is given, signal it after the memory read completes. */
    if ((Token != NULL) && (Token->Event != NULL)) {
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

    Status = RamDiskBlkIoWriteBlocks(
        &PrivateData->BlockIo,
        MediaId,
        Lba,
        BufferSize,
        Buffer
    );
    if (EFI_ERROR(Status)) {
        return Status;
    }

    /* If the caller's event is given, signal it after the memory write completes. */
    if ((Token != NULL) && (Token->Event != NULL)) {
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
    if ((Token != NULL) && (Token->Event != NULL)) {
        Token->TransactionStatus = EFI_SUCCESS;
        uefi_call_wrapper(BS->SignalEvent, 1, Token->Event);
    }

    return EFI_SUCCESS;
}
