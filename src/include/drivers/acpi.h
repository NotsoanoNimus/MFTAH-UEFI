#ifndef MFTAH_ACPI_H
#define MFTAH_ACPI_H

#include "../mftah_uefi.h"


#define EFI_ACPI_TABLE_PROTOCOL_GUID \
    { 0xffe06bdd, 0x6107, 0x46a6, { 0x7b, 0xb2, 0x5a, 0x9c, 0x7e, 0xc5, 0x27, 0x5c } }


typedef UINT32  EFI_ACPI_TABLE_HEADER;
typedef UINT32  EFI_ACPI_TABLE_VERSION;
typedef VOID *  EFI_ACPI_HANDLE;
typedef UINT32  EFI_ACPI_DATA_TYPE;


/* ACPI Common Description Table Header */
typedef
struct _EFI_ACPI_DESCRIPTION_HEADER {
    UINT32  Signature;
    UINT32  Length;
    UINT8   Revision;
    UINT8   Checksum;
    UINT8   OemId[6];
    UINT64  OemTableId;
    UINT32  OemRevision;
    UINT32  CreatorId;
    UINT32  CreatorRevision;
} _PACKED EFI_ACPI_DESCRIPTION_HEADER;
/*************************************/


/* ACPI Table Protocol Declarations & Types */
typedef
struct S_EFI_ACPI_TABLE_PROTOCOL
EFI_ACPI_TABLE_PROTOCOL;

typedef
EFI_STATUS
(EFIAPI *EFI_ACPI_TABLE_INSTALL_ACPI_TABLE)(
    IN   EFI_ACPI_TABLE_PROTOCOL        *This,
    IN   VOID                           *AcpiTableBuffer,
    IN   UINTN                          AcpiTableBufferSize,
    OUT  UINTN                          *TableKey
);

typedef
EFI_STATUS
(EFIAPI *EFI_ACPI_TABLE_UNINSTALL_ACPI_TABLE)(
    IN  EFI_ACPI_TABLE_PROTOCOL         *This,
    IN  UINTN                           TableKey
);

struct S_EFI_ACPI_TABLE_PROTOCOL {
    EFI_ACPI_TABLE_INSTALL_ACPI_TABLE   InstallAcpiTable;
    EFI_ACPI_TABLE_UNINSTALL_ACPI_TABLE UninstallAcpiTable;
};
/*************************************/


/* ACPI Table Protocol implementation. */
extern EFI_GUID gEfiAcpiTableProtocolGuid;


/**
 * Calculate the checksum of an ACPI table given its header pointer.
 *
 * @param[in]   sdt The base of the table.
 *
 * @returns Nothing. The table's checksum is automatically updated.
 */
VOID
EFIAPI
AcpiChecksumTable(
    IN EFI_ACPI_DESCRIPTION_HEADER *sdt
);


/**
 * Initialize the ACPI driver, either from a firmware-provided instance
 *  or a local instance of the protocol. MFTAH needs ACPI to register tables.
 * 
 * @retval EFI_SUCCESS      The driver was successfully initialized.
 * @retval EFI_NOT_READY    The driver failed to initialize and the fallback didn't work.
 */
EFI_STATUS
EFIAPI
AcpiInit(VOID);


/**
 * Load the custom implementation of the ACPI Table protocol if
 *  the firmware does not already provide one.
 * 
 * @returns A handle to the protocol instance.
 */
EFI_ACPI_TABLE_PROTOCOL *
EFIAPI
AcpiGetInstance(VOID);


/**
 * Destroy any memory allocations that were made if the ACPI driver
 *  were our own custom version. Does nothing to the protocol handle
 *  if this application doesn't own it.
 */
VOID
EFIAPI
AcpiDestruct(VOID);



#endif   /* MFTAH_ACPI_H */
