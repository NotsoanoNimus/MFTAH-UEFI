#ifndef NFIT_H
#define NFIT_H

#include "acpi.h"


#define EFI_ACPI_NFIT_SIGNATURE             EFI_SIGNATURE_32('N', 'F', 'I', 'T')
#define EFI_ACPI_NFIT_SPA_STRUCTURE_TYPE    0
#define EFI_ACPI_NFIT_REVISION              1


#define NFIT_TABLE_TYPE_SPA                 0
#define NFIT_TABLE_TYPE_REGION_MAPPING      1
#define NFIT_TABLE_TYPE_INTERLEAVE          2
#define NFIT_TABLE_TYPE_SMBIOS_MI           3
#define NFIT_TABLE_TYPE_CONTROL_REGION      4
#define NFIT_TABLE_TYPE_BLOCK_DATA_WINDOW   5
#define NFIT_TABLE_TYPE_FLUSH_HINT_ADDR     6
#define NFIT_TABLE_TYPE_PLATFORM_CAPABILITY 7

/* Types 8 and above are reserved per the specification. */
#define NFIT_TABLE_TYPE_RESERVED            8

#define NFIT_SPA_FLAG_CONTROL_FOR_HOTSWAP       (1 << 0)
#define NFIT_SPA_FLAG_PROXIMITY_DOMAIN_VALID    (1 << 1)
#define NFIT_SPA_FLAG_LOCATION_COOKIE_VALID     (1 << 2)

#define NFIT_SPA_GUID_PERSISTENT_MEMORY     \
    { 0x66F0D379, 0xB4F3, 0x4074, { 0xAC, 0x43, 0x0D, 0x33, 0x18, 0xB7, 0x8C, 0xDB } }
#define NFIT_SPA_GUID_NVD_CONTROL_REGION    \
    { 0x92F701F6, 0x13B4, 0x405D, { 0x91, 0x0B, 0x29, 0x93, 0x67, 0xE8, 0x23, 0x4C } }
#define NFIT_SPA_GUID_NVD_BLOCK_DATA_WINDOW \
    { 0x91AF0530, 0x5D86, 0x470E, { 0xA6, 0xB0, 0x0A, 0x2D, 0xB9, 0x40, 0x82, 0x49 } }
#define NFIT_SPA_GUID_RAMDISK_VIRT_V_DISK   \
    { 0x77AB535A, 0x45FC, 0x624B, { 0x55, 0x60, 0xF7, 0xB2, 0x81, 0xD1, 0xF9, 0x6E } }
#define NFIT_SPA_GUID_RAMDISK_VIRT_V_CD     \
    { 0x3D5ABD30, 0x4175, 0x87CE, { 0x6D, 0x64, 0xD2, 0xAD, 0xE5, 0x23, 0xC4, 0xBB } }
#define NFIT_SPA_GUID_RAMDISK_VIRT_P_DISK   \
    { 0x5CEA02C9, 0x4D07, 0x69D3, { 0x26, 0x9F, 0x44, 0x96, 0xFB, 0xE0, 0x96, 0xF9 } }
#define NFIT_SPA_GUID_RAMDISK_VIRT_P_CD     \
    { 0x08018188, 0x42CD, 0xBB48, { 0x10, 0x0F, 0x53, 0x87, 0xD5, 0x3D, 0xED, 0x3D } }


typedef
struct {
    EFI_ACPI_DESCRIPTION_HEADER Header;
    UINT32                      Reserved;
} __attribute__((packed)) EFI_ACPI_SDT_NFIT;

typedef
struct {
    UINT16      Type;
    UINT16      Length;
    UINT16      SpaRangeStructureIndex;
    UINT16      Flags;
    UINT32      Reserved;
    UINT32      ProximityDomain;
    EFI_GUID    AddressRangeTypeGUID;
    UINT64      SystemPhysicalAddressRangeBase;
    UINT64      SystemPhysicalAddressRangeLength;
    UINT64      AddressRangeMemoryMappingAttribute;
} __attribute__((packed)) EFI_ACPI_NFIT_SPA_STRUCTURE;



#endif   /* NFIT_H */
