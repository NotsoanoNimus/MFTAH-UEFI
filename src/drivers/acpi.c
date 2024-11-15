#include "../include/drivers/acpi.h"



typedef
struct {
    UINT64  Signature;
    UINT8   Checksum;
    UINT8   OemId[6];
    UINT8   Revision;
    UINT32  RsdtAddress;
    UINT32  Length;
    UINT64  XsdtAddress;
    UINT8   ExtendedChecksum;
    UINT8   Reserved[3];
} __attribute__((packed)) EFI_ACPI_SDT_RSDP;

STATIC EFI_ACPI_SDT_RSDP *mRsdp = NULL;

STATIC EFI_ACPI_TABLE_PROTOCOL *mAcpiTableProtocol = NULL;
STATIC EFI_GUID gEfiAcpiTableProtocolGuid = EFI_ACPI_TABLE_PROTOCOL_GUID;


#define MAX_ACPI_TRACKED_TABLES     32

STATIC EFI_ACPI_DESCRIPTION_HEADER **mKeyedTables = NULL;
STATIC UINTN mKeyedTablesIndex = 0;

STATIC BOOLEAN mSelfAllocated = FALSE;
STATIC BOOLEAN mHasAlreadyReplaced = FALSE;

STATIC CONST CHAR *mTableNames[68];



STATIC
EFI_STATUS
EFIAPI
InstallTable(IN EFI_ACPI_TABLE_PROTOCOL *This,
             IN VOID *AcpiTableBuffer,
             IN UINTN AcpiTableBufferSize,
             OUT UINTN *TableKey)
{
    /* Go to the extent of the XSDT and get ready to append another pointer.
        Ensure that the data to be overwritten doesn't look like a valid or
        well-known SDT signature. If it does, move it and update its table
        pointer in the XSDT. Finally, insert the new table's reference, track
        it locally, and return the index into the pointer array. */
    EFI_STATUS Status = EFI_SUCCESS;
    EFI_PHYSICAL_ADDRESS p = 0;
    EFI_ACPI_DESCRIPTION_HEADER *xsdt = (EFI_ACPI_DESCRIPTION_HEADER *)(mRsdp->XsdtAddress);
    EFI_ACPI_DESCRIPTION_HEADER *rsdt = (EFI_ACPI_DESCRIPTION_HEADER *)(mRsdp->RsdtAddress);
    EFI_ACPI_DESCRIPTION_HEADER *RelocatedHandle = NULL,
                                *OldTableHandle = NULL,
                                *NewHandle = NULL;

    /* Sanity check... */
    if (
        NULL == This
        || NULL == AcpiTableBuffer
        || 0 == AcpiTableBufferSize
        || NULL == TableKey
        || NULL == mRsdp
    ) return EFI_INVALID_PARAMETER;

    if (mKeyedTablesIndex + 1 > MAX_ACPI_TRACKED_TABLES) {
        EFI_DANGERLN("ERROR: ACPI: Cannot install table. Out of space!");
        return EFI_OUT_OF_RESOURCES;
    }

    /* TODO: This check is only valid for SDTs and will not work for FACS, DSDT,
        and some other ACPI tables. */
    if (AcpiTableBufferSize != ((EFI_ACPI_DESCRIPTION_HEADER *)AcpiTableBuffer)->Length) {
        return EFI_INVALID_PARAMETER;
    }

    if (TRUE == mHasAlreadyReplaced) {
        FreePool(xsdt);
        FreePool(rsdt);
    }

    UINTN NewXsdtLength = xsdt->Length + sizeof(EFI_ACPI_DESCRIPTION_HEADER *);
    BS->AllocatePool(EfiACPIMemoryNVS, NewXsdtLength, &xsdt);
    if (NULL == xsdt) return EFI_OUT_OF_RESOURCES;
    CopyMem(xsdt, (VOID *)(mRsdp->XsdtAddress), NewXsdtLength - sizeof(EFI_ACPI_DESCRIPTION_HEADER *));

    UINTN NewRsdtLength = rsdt->Length + sizeof(UINT32);
    BS->AllocatePool(EfiACPIMemoryNVS, NewRsdtLength, &rsdt);
    if (NULL == rsdt) return EFI_OUT_OF_RESOURCES;
    CopyMem(rsdt, (VOID *)(mRsdp->RsdtAddress), NewRsdtLength - sizeof(UINT32));

    /* Enumerate all XSDT pointers. If the table already exists and is tracked,
        return an ACCESS DENIED. */
    for (
        p = (EFI_PHYSICAL_ADDRESS)xsdt + sizeof(EFI_ACPI_DESCRIPTION_HEADER);
        p < (EFI_PHYSICAL_ADDRESS)xsdt + xsdt->Length;
        p += sizeof(EFI_ACPI_DESCRIPTION_HEADER *)
    ) {
        if (*((EFI_ACPI_DESCRIPTION_HEADER **)p) == (EFI_ACPI_DESCRIPTION_HEADER *)AcpiTableBuffer) {
            FreePool(xsdt);
            FreePool(rsdt);
            return EFI_ACCESS_DENIED;
        }
    }

    /* Alright, we're good. Insert the handle at the end of the table. */
    ERRCHECK(
        BS->AllocatePool(EfiACPIMemoryNVS,
                         AcpiTableBufferSize,
                         (VOID **)&NewHandle)
    );
    if (NULL == NewHandle) {
        EFI_DANGERLN("ERROR: ACPI: Out of resources!");
        FreePool(xsdt);
        FreePool(rsdt);
        return EFI_OUT_OF_RESOURCES;
    }

    CopyMem(NewHandle, AcpiTableBuffer, AcpiTableBufferSize);

    /* Finally, add the entry. */
    *((EFI_ACPI_DESCRIPTION_HEADER **)p) = NewHandle;
    xsdt->Length += sizeof(EFI_ACPI_DESCRIPTION_HEADER *);

    *((UINT32 *)((EFI_PHYSICAL_ADDRESS)rsdt + rsdt->Length)) = (UINT32)NewHandle;
    rsdt->Length += sizeof(UINT32);

    /* We're nice and we'll double-check the checksum for them. :) */
    AcpiChecksumTable(NewHandle);
    AcpiChecksumTable(xsdt);   /* This is a MUST. */

    mRsdp->XsdtAddress = (UINT64)xsdt;
    mRsdp->RsdtAddress = (UINT32)rsdt;
    mHasAlreadyReplaced = TRUE;

    /* Checksum the RSDP table in both places. */
    {
        UINT32 sum = 0;

        mRsdp->Checksum = 0;
        for (UINTN i = 0; i < 20; ++i) {
            sum += *((UINT8 *)((EFI_PHYSICAL_ADDRESS)mRsdp + i));
        }
        mRsdp->Checksum = (UINT8)(0x100 - (sum % 0x100));

        sum = 0;

        mRsdp->ExtendedChecksum = 0;
        for (UINTN i = 0; i < sizeof(EFI_ACPI_SDT_RSDP); ++i) {
            sum += *((UINT8 *)((EFI_PHYSICAL_ADDRESS)mRsdp + i));
        }
        mRsdp->ExtendedChecksum = (UINT8)(0x100 - (sum % 0x100));
    }

    /* Update the index of tracked installed tables. */
    *TableKey = mKeyedTablesIndex;

    mKeyedTables[mKeyedTablesIndex] = NewHandle;
    ++mKeyedTablesIndex;

    return EFI_SUCCESS;
}


STATIC
EFI_STATUS
EFIAPI
UninstallTable(IN EFI_ACPI_TABLE_PROTOCOL *This,
               IN UINTN TableKey)
{
    /* Find the given table key (just an indexer into the ptr array). If
        the pointer value is non-NULL, then find the pointer in the XSDT.
        Wehere there's a match, clear it and shrink the table. At the end,
        recompute a checksum on the table. */
    EFI_ACPI_DESCRIPTION_HEADER *TargetPointer = NULL;
    EFI_ACPI_DESCRIPTION_HEADER *xsdt
        = (EFI_ACPI_DESCRIPTION_HEADER *)(mRsdp->XsdtAddress);

    /* Sanity check... */
    if (NULL == This) return EFI_INVALID_PARAMETER;

    if (TableKey >= mKeyedTablesIndex) return EFI_NOT_FOUND;

    TargetPointer = mKeyedTables[TableKey];
    if (NULL == TargetPointer) return EFI_NOT_FOUND;

    for (
        UINTN p = (UINT64)xsdt + sizeof(EFI_ACPI_DESCRIPTION_HEADER);
        p < xsdt->Length;
        p += sizeof(VOID *)
    ) {
        if (*((UINT64 **)p) == (UINT64 *)TargetPointer) {
            *((UINT64 **)p) = NULL;   /* Clear the entry. */
            /* TODO: Should holes in the table be collapsed here?? */
        }
    }

    FreePool(TargetPointer);

    AcpiChecksumTable(xsdt);

    mKeyedTables[mKeyedTablesIndex] = NULL;
    --mKeyedTablesIndex;

    return EFI_SUCCESS;
}


/**
 * Initialize the local (custom) ACPI Table protocol instance and the static
 *  table of tracked XSDT entries.
 * 
 * @returns Nothing. If this returns early, protocol initialization will fail.
 */
STATIC
EFIAPI
VOID
AcpiSelfInit(VOID)
{
    /* Enumerate the configuration table from the SystemTable and search for
        ACPI GUIDs that can give us the RSDP. */
    EFI_CONFIGURATION_TABLE *table = ST->ConfigurationTable;
    EFI_GUID acpi2Guid = ACPI_20_TABLE_GUID;

    for (UINTN i = 0; i < ST->NumberOfTableEntries; ++i) {
        if (0 != CompareMem(&(table[i].VendorGuid), &acpi2Guid, sizeof(EFI_GUID))) continue;

        mRsdp = (EFI_ACPI_SDT_RSDP *)(table[i].VendorTable);
    }

    /* If the RSDP just can't be found, then give up. */
    if (NULL == mRsdp) {
        EFI_DANGERLN("ERROR: ACPI: Protocol v2.0 was not found!");
        return;
    }

    mKeyedTables = (EFI_ACPI_DESCRIPTION_HEADER **)
        AllocatePool(MAX_ACPI_TRACKED_TABLES * sizeof(EFI_ACPI_DESCRIPTION_HEADER *));
    if (NULL == mKeyedTables) {
        EFI_DANGERLN("ERROR: ACPI: Out of memory!");
        mRsdp = NULL;
        return;
    }

    mAcpiTableProtocol = (EFI_ACPI_TABLE_PROTOCOL *)
        AllocatePool(sizeof(EFI_ACPI_TABLE_PROTOCOL));
    if (NULL == mAcpiTableProtocol) {
        EFI_DANGERLN("ERROR: ACPI: Out of memory!");
        mRsdp = NULL;
        return;
    }

    mAcpiTableProtocol->InstallAcpiTable    = InstallTable;
    mAcpiTableProtocol->UninstallAcpiTable  = UninstallTable;
    mSelfAllocated = TRUE;
}


STATIC
EFI_STATUS
EFIAPI
AcpiProtocolCheck(VOID)
{
    EFI_STATUS Status = EFI_SUCCESS;

    /* Attempt to locate the EFI_ACPI_TABLE_PROTOCOL from the firmware. */
    ERRCHECK(
        BS->LocateProtocol(&gEfiAcpiTableProtocolGuid,
                           NULL,
                           (VOID **)&mAcpiTableProtocol)
    );

    return (NULL == mAcpiTableProtocol)
        ? EFI_NOT_FOUND
        : Status;
}


VOID
EFIAPI
AcpiChecksumTable(IN EFI_ACPI_DESCRIPTION_HEADER *sdt)
{
    uint32_t sum = 0;

    sdt->Checksum = 0;

    for (uint32_t i = 0; i < sdt->Length; ++i) {
        sum += *(uint8_t *)((uintptr_t)sdt + i);
    }

    sdt->Checksum = (uint8_t)(0x100 - (sum % 0x100));
}


EFI_STATUS
EFIAPI
AcpiInit(VOID)
{
    if (EFI_ERROR(AcpiProtocolCheck())) {
        /* Yikes. We have to hook our own driver to perform ACPI tasks. */
        AcpiSelfInit();   /* Get RSDP and ensure ACPI revision is >2.0 */
    }

    return (NULL == mAcpiTableProtocol)
        ? EFI_NOT_READY
        : EFI_SUCCESS;
}


VOID
EFIAPI
AcpiDestruct(VOID)
{
    /* Only destroy the protocol objects when we own the handle. */
    if (FALSE == mSelfAllocated) return;

    FreePool(mAcpiTableProtocol);
    mAcpiTableProtocol = NULL;

    FreePool(mKeyedTables);
    mKeyedTables = NULL;
}


EFI_ACPI_TABLE_PROTOCOL *
EFIAPI
AcpiGetInstance(VOID)
{
    return mAcpiTableProtocol;
}



static const char *mTableNames[68] = {
    "RSDP",
    "RSDT",
    "XSDT",
    "DSDT",
    "SSDT",
    "PSDT",
    "NFIT",
    "APIC",
    "BERT",
    "BGRT",
    "CPEP",
    "ECDT",
    "EINJ",
    "ERST",
    "FACP",
    "FACS",
    "FPDT",
    "GTDT",
    "HEST",
    "MSCT",
    "MPST",
    "OEMx",
    "PCCT",
    "PHAT",
    "PMTT",
    "RASF",
    "SBST",
    "SDEV",
    "SLIT",
    "SRAT",
    "AEST",
    "BDAT",
    "BOOT",
    "CDIT",
    "CEDT",
    "CRAT",
    "CSRT",
    "DBGP",
    "DBPG",
    "DMAR",
    "DRTM",
    "ETDT",
    "HPET",
    "IBFT",
    "IORT",
    "IVRS",
    "LPIT",
    "MCFG",
    "MCHI",
    "MPAM",
    "MSDM",
    "PRMT",
    "RGRT",
    "SDEI",
    "SLIC",
    "SPCR",
    "SPMI",
    "STAO",
    "SVKL",
    "TCPA",
    "TPM2",
    "UEFI",
    "WAET",
    "WDAT",
    "WDRT",
    "WPBT",
    "WSMT",
    "XENV"
};
