#ifndef MULTIBOOT2_EXEC_H
#define MULTIBOOT2_EXEC_H

#include "multiboot.h"


#define EFI_MULTIBOOT2_PROTOCOL_GUID \
    { 0x8c7abc40, 0xb62c, 0x468f, {0x4c, 0x46, 0xc8, 0xc0, 0x31, 0x3b, 0x8a, 0x06} }

EXTERN EFI_GUID gEfiMultiboot2ProtocolGuid;



typedef
struct {
    MultibootTagInfoRequest     *TagRequest;
    MultibootTagInfoRequest     *TagRequestAlternate;
    MultibootTagAddress         *TagAddresses;
    MultibootTagEntry           *TagEntry;
    MultibootTagEntry           *Tag386Entry;
    MultibootTagEntry           *TagAmd64Entry;
    MultibootTagFlags           *TagFlags;
    MultibootTagFramebuffer     *TagFramebuffer;
    MultibootTagBoolean         *TagIsModulePageAligned;
    MultibootTagBoolean         *TagDoNotExitBootServices;
    MultibootTagRelocatable     *TagRelocatable;

    MultibootInfoHeader         *LoadedInfoHeader;
} EFI_MULTIBOOT2_CONTEXT;


typedef
struct MULTIBOOT2_PROTOCOL
EFI_MULTIBOOT2_PROTOCOL;


typedef
EFI_STATUS
(EFIAPI *EFI_MULTIBOOT2_SEEK_HEADER)(
    IN      EFI_MULTIBOOT2_PROTOCOL *This,
    IN      EFI_PHYSICAL_ADDRESS    LoadedImageBase,
    OUT     EFI_PHYSICAL_ADDRESS    *HeaderLocation
);

typedef
EFI_STATUS
(EFIAPI *EFI_MULTIBOOT2_PARSE_HEADER)(
    IN      EFI_MULTIBOOT2_PROTOCOL *This,
    IN      EFI_PHYSICAL_ADDRESS    HeaderAddress,
    OUT     EFI_MULTIBOOT2_CONTEXT  **ResultContext
);

typedef
EFI_STATUS
(EFIAPI *EFI_MULTIBOOT2_VALIDATE_CONTEXT)(
    IN      EFI_MULTIBOOT2_PROTOCOL *This,
    IN      EFI_MULTIBOOT2_CONTEXT  *Context
);

typedef
EFI_STATUS
(EFIAPI *EFI_MULTIBOOT2_BUILD_INFO_HEADER_FROM_TAGS)(
    IN      EFI_MULTIBOOT2_PROTOCOL *This,
    IN      VOID                    **TagsPointers,
    IN      UINTN                   TagsCount,
    OUT     MultibootInfoHeader     **InfoHeader,
    OUT     UINTN                   *TagsLoaded
);


INTERFACE_DECL(MULTIBOOT2_PROTOCOL)
{
    EFI_MULTIBOOT2_SEEK_HEADER                  SeekHeader;
    EFI_MULTIBOOT2_PARSE_HEADER                 Parse;
    EFI_MULTIBOOT2_VALIDATE_CONTEXT             ValidateContext;
    EFI_MULTIBOOT2_BUILD_INFO_HEADER_FROM_TAGS  BuildInfoHeaderFromTags;
};



CONST EFI_MULTIBOOT2_PROTOCOL *
GetMultiboot2ProtocolInstance(VOID);



#endif   /* MULTIBOOT2_EXEC_H */
