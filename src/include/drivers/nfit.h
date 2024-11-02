#ifndef NFIT_H
#define NFIT_H

#include "acpi.h"


#define EFI_ACPI_NFIT_SIGNATURE             EFI_SIGNATURE_32('N', 'F', 'I', 'T')
#define EFI_ACPI_NFIT_SPA_STRUCTURE_TYPE    0
#define EFI_ACPI_NFIT_REVISION              1


typedef
struct {
    EFI_ACPI_DESCRIPTION_HEADER Header;
    UINT32                      Reserved;
} _PACKED EFI_ACPI_SDT_NFIT;

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
} _PACKED EFI_ACPI_NFIT_SPA_STRUCTURE;



#endif   /* NFIT_H */
