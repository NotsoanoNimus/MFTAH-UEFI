#include "../include/loaders/elf.h"
#include "../include/loaders/multiboot_exec.h"

#include "../include/drivers/displays.h"

#include "../include/core/util.h"



typedef struct {
    ELF_STATUS  Code;
    CONST CHAR8 *Desc;
} ElfErrorCodeTableType;
ElfErrorCodeTableType ElfErrorCodeTable[] = {
    { ELF_SUCCESS,                          "Success" },
    { ELF_INVALID_MAGIC,                    "Invalid ELF Magic" },
    { ELF_INVALID_CLASS,                    "Invalid ELF Class" },
    { ELF_INVALID_ENCODING,                 "Invalid ELF Encoding" },
    { ELF_INVALID_VERSION,                  "Invalid ELF Ident Version" },
    { ELF_INVALID_TYPE,                     "Invalid ELF Type" },
    { ELF_INVALID_MACHINE,                  "Invalid ELF Machine" },
    { ELF_INVALID_ENTRYPOINT,               "Invalid ELF Entrypoint" },
    { ELF_INVALID_PROGRAM_HEADER_OFFSET,    "Invalid ELF PH Offset" },
    { ELF_INVALID_SECTION_HEADER_OFFSET,    "Invalid ELF Section Header Offset" },
    { ELF_INVALID_ELF_HEADER_LENGTH,        "Invalid ELF Header Length" },
    { ELF_INVALID_PH_ENTRY_SIZE,            "Invalid ELF PH Entry Size" },
    { ELF_INVALID_SH_ENTRY_SIZE,            "Invalid ELF Section Header Entry Size" },
    { ELF_NO_SECTIONS,                      "No ELF Sections Defined" },
    { ELF_MISSING_SHSTRNDX,                 "Missing ELF Section Header String Index" },
    { ELF_PH_INVALID_SEGMENT_OFFSET,        "Invalid ELF PH Segment Offset" },
    { ELF_PH_INVALID_VIRTUAL_ADDRESS,       "Invalid ELF PH Virtual Address" },
    { ELF_PH_INVALID_PHYSICAL_ADDRESS,      "Invalid ELF PH Physical Address" },
    { ELF_PH_INVALID_ALIGNMENT,             "Invalid ELF PH Alignment" },
    { ELF_PH_MISALIGNED_VIRTUAL_ADDRESS,    "ELF PH Misaligned Virtual Address" },
    { ELF_SH_INVALID_SHSTRTAB_OFFSET,       "Invalid ELF SH String Table Offset" },
    { ELF_SH_INVALID_LOAD_ADDRESS,          "Invalid ELF SH Load Address" },
    { ELF_SH_INVALID_OFFSET,                "Invalid ELF SH Offset" },
    { ELF_SH_INVALID_ALIGNMENT,             "Invalid ELF SH Alignment" },
    { ELF_TOO_LARGE,                        "Out of resources: ELF too large" },
    { ELF_UNSUPPORTED_CLASS,                "Unsupported class (64-bit only)" },
    { ELF_UNSUPPORTED_ENCODING,             "Unsupported encoding (LSB only)" },
    { ELF_UNSUPPORTED_TYPE,                 "Unsupported type (Exec or Rel only)" },
    { ELF_FAILURE_LOAD_SEGMENT,             "Failed to load program header segment(s)" },
    { ELF_GENERIC_ERROR,                    "Generic ELF Error" },
    { 0,                                    NULL }
};

STATIC
CONST CHAR8 *
ElfStatusToString(IN ELF_STATUS Status)
{
    for (UINTN i = 0; ElfErrorCodeTable[i].Desc; ++i) {
        if (Status == ElfErrorCodeTable[i].Code) {
            return ElfErrorCodeTable[i].Desc;
        }
    }

    return "Unknown ELF Error";
}


STATIC
ELF_STATUS
VerifyAndLoadElf(IN EFI_PHYSICAL_ADDRESS LoadedElfPhysAddr,
                 OUT LOADED_ELF *Elf)
{
    if (
        0 == LoadedElfPhysAddr
        || NULL == Elf
    ) return ELF_GENERIC_ERROR;

    EFI_STATUS Status = EFI_SUCCESS;

    ElfIdent *Ident = (ElfIdent *)LoadedElfPhysAddr;
    Elf64Header *Header = (Elf64Header *)LoadedElfPhysAddr;
    CONST CHAR8 ElfSignature[] = ELF_MAGIC_SIGNATURE;

    if (0 != CompareMem((VOID *)LoadedElfPhysAddr, ElfSignature, 4)) {
        return ELF_INVALID_MAGIC;
    }

    /* NOTE: This verification strategy assumes the target ELF is a 64-bit LSB type. */
    if (Ident->Class < EC_32 || Ident->Class > EC_64) return ELF_INVALID_CLASS;
    else if (EC_64 != Ident->Class) return ELF_UNSUPPORTED_CLASS;

    if (Ident->Data < ED_LSB || Ident->Data > ED_MSB) return ELF_INVALID_ENCODING;
    else if (ED_LSB != Ident->Data) return ELF_UNSUPPORTED_ENCODING;

    if (EV_CURRENT != Ident->Version) return ELF_INVALID_VERSION;

    /* Type, Version, and Machine below can be checked independent of the ELF's Class field. */
    if (Header->Type == ET_NONE) return ELF_INVALID_TYPE;
    else if (ET_REL != Header->Type && ET_EXEC != Header->Type) return ELF_UNSUPPORTED_TYPE;

    /* We don't really check the architecture here. It kinda varies too much. */
    if (EM_NONE == Header->Machine) return ELF_INVALID_MACHINE;

    if (EV_CURRENT != Header->Version) return ELF_INVALID_VERSION;

    /*** Verification complete. ***/
    /* Set ELF information and load program header segments. */
    // TODO: This should just become an `OUT EFI_PHYSICAL_ADDRESS` in the method signature.
    Elf->LoadedImageEntry = (EC_32 == Ident->Class)
        ? (((Elf32Header *)Header)->Entry)
        : (((Elf64Header *)Header)->Entry);

    EFI_PHYSICAL_ADDRESS PHdrBase = (EC_32 == Ident->Class)
        ? (LoadedElfPhysAddr + ((Elf32Header *)Header)->ProgramHeaderTableOffset)
        : (LoadedElfPhysAddr + ((Elf64Header *)Header)->ProgramHeaderTableOffset);

    UINTN PHdrEntrySize = (EC_32 == Ident->Class)
        ? (((Elf32Header *)Header)->ProgramHeaderEntrySizeBytes)
        : (((Elf64Header *)Header)->ProgramHeaderEntrySizeBytes);

    UINTN PHdrCount = (EC_32 == Ident->Class)
        ? (((Elf32Header *)Header)->ProgramHeaderCount)
        : (((Elf64Header *)Header)->ProgramHeaderCount);

    for (
        EFI_PHYSICAL_ADDRESS i = 0;
        i < (PHdrCount * PHdrEntrySize);
        i += PHdrEntrySize
    ) {
        /* Common variables to use for each segment, regardless of ELF32 or ELF64.
            Not the most effective way to do this, but at least it's easy on the eyes. */
        UINT64 P_Offset = 0;
        UINT64 P_VirtualAddress = 0;
        UINT64 P_PhysicalAddress = 0;
        UINT64 P_FileSize = 0;
        UINT64 P_MemorySize = 0;
        UINT64 P_Alignment = 0;

        if (EC_32 == Ident->Class) {
            Elf32ProgramHeader PHdr32 = {0};
            CopyMem(&PHdr32, (VOID *)(PHdrBase + i), sizeof(Elf32ProgramHeader));

            if (PT_LOAD != PHdr32.Type) continue;
            P_Offset            = PHdr32.Offset;
            P_VirtualAddress    = PHdr32.VirtualAddress;
            P_PhysicalAddress   = PHdr32.PhysicalAddress;
            P_FileSize          = PHdr32.FileSize;
            P_MemorySize        = PHdr32.MemorySize;
            P_Alignment         = PHdr32.Alignment;
        }
        
        else {
            Elf64ProgramHeader PHdr64 = {0};
            CopyMem(&PHdr64, (VOID *)(PHdrBase + i), sizeof(Elf64ProgramHeader));

            if (PT_LOAD != PHdr64.Type) continue;
            P_Offset            = PHdr64.Offset;
            P_VirtualAddress    = PHdr64.VirtualAddress;
            P_PhysicalAddress   = PHdr64.PhysicalAddress;
            P_FileSize          = PHdr64.FileSize;
            P_MemorySize        = PHdr64.MemorySize;
            P_Alignment         = PHdr64.Alignment;
        }

        /* Allocate pages based on the ELF's specification of its PH segments. */
        Status = BS->AllocatePages(AllocateAddress,
                                   EfiLoaderCode,
                                   EFI_SIZE_TO_PAGES(P_MemorySize),
                                   (EFI_PHYSICAL_ADDRESS *)(&P_PhysicalAddress));
        if (EFI_ERROR(Status)) {
            DISPLAY->Panic(DISPLAY,
                           "Error loading program header segment.",
                           Status,
                           FALSE,
                           EFI_SECONDS_TO_MICROSECONDS(3));
            return ELF_FAILURE_LOAD_SEGMENT;
        }

        /* Zero out the set of loaded pages. This takes care of zero-loading any difference
            between FileSize and MemorySize. */
        SetMem((VOID *)P_PhysicalAddress, EFI_SIZE_TO_PAGES(P_MemorySize), 0x00);

        /* Copy the segment into memory at the newly-allocated, zero-init'd set of pages. */
        if (P_FileSize > 0) {
            CopyMem((VOID *)P_PhysicalAddress,
                    (VOID *)(LoadedElfPhysAddr + P_Offset),
                    P_FileSize);
        }
    }

    return ELF_SUCCESS;
}


typedef
void volatile
(__attribute__((sysv_abi, optnone)) *ELF_ENTRYPOINT)(VOID);


STATIC
EFIAPI
VOID
LoadImage(IN LOADER_CONTEXT *Context)
{
    EFI_STATUS Status = EFI_SUCCESS;
    ELF_STATUS ElfLoadStatus = ELF_SUCCESS;
    LOADED_ELF LoadedElf = {0};
    EFI_MULTIBOOT2_PROTOCOL *Multiboot2;
    EFI_MULTIBOOT2_CONTEXT *MultibootContext;
    EFI_PHYSICAL_ADDRESS MultibootHeaderAddress;
    EFI_MEMORY_MAP_META Meta = {0};

    /* Set and track a few ELF-related variables while we can. */
    LoadedElf.ImageBegin = Context->LoadedImageBase;
    LoadedElf.ImageEnd = (Context->LoadedImageBase + Context->LoadedImageSize);
    LoadedElf.ImagePages = EFI_SIZE_TO_PAGES(Context->LoadedImageSize);

    ElfLoadStatus = VerifyAndLoadElf(Context->LoadedImageBase, &LoadedElf);
    if (ELF_ERROR((ElfLoadStatus))) {
        DISPLAY->Panic(DISPLAY,
                       ElfStatusToString(ElfLoadStatus),
                       EFI_LOAD_ERROR,
                       TRUE,
                       EFI_SECONDS_TO_MICROSECONDS(10));
    }

    // TODO: Move all of this into a utility function? Because BIN will need it too.
    // TODO: Finish all MB2 tags from this section as well.
    Multiboot2 = GetMultiboot2ProtocolInstance();

    Status = Multiboot2->SeekHeader(Multiboot2, Context->LoadedImageBase, &MultibootHeaderAddress);
    if (EFI_SUCCESS != Status) goto ElfEntrySkipMultiboot;

    Status = Multiboot2->Parse(Multiboot2, MultibootHeaderAddress, &MultibootContext);
    if (EFI_ERROR(Status)) {
        DISPLAY->Panic(DISPLAY,
                        "Failed to parse a Multiboot2 structure. Trying anyway.",
                        Status,
                        FALSE,
                        EFI_SECONDS_TO_MICROSECONDS(3));

        goto ElfEntrySkipMultiboot;
    }

    /* We always provide every single piece of Multiboot2 info we can. */
    VOID *InfoTagSet[32] = {0};

    /* Command line. */
    UINTN CmdLineLength =
        sizeof(MultibootInfoTagHeader) + (
            (NULL == Context->Chain->CmdLine) ? 0 : AsciiStrLen(Context->Chain->CmdLine)
        ) + 1;
    InfoTagSet[0] = AllocateZeroPool(CmdLineLength);
    ((MultibootInfoTagHeader *)(InfoTagSet[0]))->Type = MBI_CMD_LINE;
    ((MultibootInfoTagHeader *)(InfoTagSet[0]))->Size = CmdLineLength;
    if (NULL != Context->Chain->CmdLine) {
        CopyMem(
            (VOID *)((EFI_PHYSICAL_ADDRESS)(InfoTagSet[0]) + sizeof(MultibootInfoTagHeader)),
            Context->Chain->CmdLine,
            AsciiStrLen(Context->Chain->CmdLine)
        );
    }

    /* Boot loader name. */
    CONST CHAR8 *LoaderName = "MFTAH Chainloader";
    UINTN LoaderNameSize = sizeof(MultibootInfoTagHeader) + AsciiStrLen(LoaderName) + 1;
    InfoTagSet[1] = AllocateZeroPool(LoaderNameSize);
    ((MultibootInfoTagHeader *)(InfoTagSet[1]))->Type = MBI_LOADER_NAME;
    ((MultibootInfoTagHeader *)(InfoTagSet[1]))->Size = LoaderNameSize;

    /* Memory maps (both for EFI and the ordinary type). */
    /* Get the system's current memory mapping. */
    EFI_MEMORY_MAP_META Mb2MemMap = {0};
    Status = GetMemoryMap(&Mb2MemMap);
    if (EFI_ERROR(Status)) {
        EFI_WARNINGLN("WARNING: Failed to get the current memory map for MBI tagging.");
        goto MultibootSkipMemoryMap;
    } else {
        EFI_WARNINGLN(
            "Got a recent memory map (%u bytes / %u entry size = %u entries).",
            Mb2MemMap.MemoryMapSize, Mb2MemMap.DescriptorSize, (Mb2MemMap.MemoryMapSize / Mb2MemMap.DescriptorSize)
        );
    }

    UINTN MemMapTagSize = (sizeof(MultibootInfoTagMemoryMap) +
        (sizeof(MultibootMemoryMapEntry) * (Mb2MemMap.MemoryMapSize / Mb2MemMap.DescriptorSize)));
    InfoTagSet[2] = AllocateZeroPool(MemMapTagSize);   /* outdates the memory map LOL */
    ((MultibootInfoTagHeader *)(InfoTagSet[2]))->Type = MBI_MEMORY_MAP;
    ((MultibootInfoTagHeader *)(InfoTagSet[2]))->Size = MemMapTagSize;
    ((MultibootInfoTagMemoryMap *)(InfoTagSet[2]))->EntrySize = sizeof(MultibootMemoryMapEntry);
    ((MultibootInfoTagMemoryMap *)(InfoTagSet[2]))->EntryVersion = 0;   /* stays Zero; see spec */
    for (UINTN i = 0; i < (Mb2MemMap.MemoryMapSize / Mb2MemMap.DescriptorSize); ++i) {
        // TODO: Testing...
        EFI_MEMORY_DESCRIPTOR *d = ((EFI_MEMORY_DESCRIPTOR *)
            ((EFI_PHYSICAL_ADDRESS)(Mb2MemMap.BaseDescriptor) + (i * Mb2MemMap.DescriptorSize)));
        EFI_WARNINGLN(
            "    Base %p -- Length 0x%08x -- Type %d",
            d->PhysicalStart, (EFI_PAGE_SIZE * d->NumberOfPages), d->Type
        );

        MultibootMemoryMapEntry *entry = (MultibootMemoryMapEntry *)
            ((EFI_PHYSICAL_ADDRESS)(InfoTagSet[2]) + sizeof(MultibootInfoTagMemoryMap))
            + (i * sizeof(MultibootMemoryMapEntry));

        /* Populate the map entry. */
        entry->Base = d->PhysicalStart;
        entry->Length = (d->NumberOfPages * EFI_PAGE_SIZE);
        entry->Type = d->Type;
    }

    UINTN EfiMemMapTagSize = (sizeof(MultibootInfoTagEfiMemoryMap) + Mb2MemMap.MemoryMapSize);
    InfoTagSet[3] = AllocateZeroPool(EfiMemMapTagSize);
    ((MultibootInfoTagHeader *)(InfoTagSet[3]))->Type = MBI_EFI_MEMORY_MAP;
    ((MultibootInfoTagHeader *)(InfoTagSet[3]))->Size = EfiMemMapTagSize;
    ((MultibootInfoTagEfiMemoryMap *)(InfoTagSet[3]))->DescriptorSize = Mb2MemMap.DescriptorSize;
    ((MultibootInfoTagEfiMemoryMap *)(InfoTagSet[3]))->DescriptorVersion = Mb2MemMap.DescriptorVersion;
    CopyMem((VOID *)((EFI_PHYSICAL_ADDRESS)(InfoTagSet[3]) + sizeof(MultibootInfoTagEfiMemoryMap)),
            Mb2MemMap.BaseDescriptor,
            Mb2MemMap.MemoryMapSize);
MultibootSkipMemoryMap:

    /* EFI SystemTable (64-bit) Pointer. */
    InfoTagSet[4] = AllocateZeroPool(sizeof(MultibootInfoTagPointer64));
    ((MultibootInfoTagHeader *)(InfoTagSet[4]))->Type = MBI_EFI_ST_64;
    ((MultibootInfoTagHeader *)(InfoTagSet[4]))->Size = sizeof(MultibootInfoTagPointer64);
    ((MultibootInfoTagPointer64 *)(InfoTagSet[4]))->PhysicalAddress = (EFI_PHYSICAL_ADDRESS)ST;

    /* Framebuffer. We use GOP here, even if the current DISPLAY object is not in GRAPHICAL Mode.
        NOTE: We also ignore Framebuffer tags from the OS kernel MB2 signature at this time. */
    InfoTagSet[5] = AllocateZeroPool(sizeof(MultibootInfoTagFramebuffer));
    ((MultibootInfoTagHeader *)(InfoTagSet[5]))->Type = MBI_FRAMEBUFFER;
    ((MultibootInfoTagHeader *)(InfoTagSet[5]))->Size = sizeof(MultibootInfoTagFramebuffer);
    // TODO ^ FB

    /* Finally, create the tags structure to pass to the loaded OS kernel. */
    MultibootInfoHeader *ResultantMb2Header = NULL;
    UINTN TagsLoadedCount = 0;
    Status = Multiboot2->BuildInfoHeaderFromTags(Multiboot2, InfoTagSet, 6, &ResultantMb2Header, &TagsLoadedCount);
    if (EFI_ERROR(Status)) {
        DISPLAY->Panic(DISPLAY,
                       "Failed to aggregate Multiboot2 tags. Trying anyway.",
                       Status,
                       FALSE,
                       EFI_SECONDS_TO_MICROSECONDS(3));

        goto ElfEntrySkipMultiboot;
    }
    else if (6 != TagsLoadedCount) {
        DISPLAY->Panic(DISPLAY,
                       "Some Multiboot2 tags were invalid or failed to load. Trying anyway.",
                       Status,
                       FALSE,
                       EFI_SECONDS_TO_MICROSECONDS(3));

        goto ElfEntrySkipMultiboot;
    }

    MultibootContext->LoadedInfoHeader = ResultantMb2Header;

    /* Validate the Multiboot2 header details (on both sides) before calling the ELF. */
    Status = Multiboot2->ValidateContext(Multiboot2, MultibootContext);
    if (EFI_ERROR(Status)) {
        DISPLAY->Panic(DISPLAY,
                        "Invalid Multiboot2 context. Trying anyway.",
                        Status,
                        FALSE,
                        EFI_SECONDS_TO_MICROSECONDS(3));

        goto ElfEntrySkipMultiboot;
    }

    /* De-init the loader before leaving forever. */
    LoaderDestroyContext(Context);
    EFI_WARNINGLN("BOOTING ELF (Multiboot2) entry at %p...", (VOID *)(LoadedElf.LoadedImageEntry));

    /* Exit Boot Services, as long as the loaded OS doesn't specifically say not to. */
    if (NULL == MultibootContext->TagDoNotExitBootServices) {
        /* Always exit Boot Services when no multiboot exists or could be configured. */
        Status = GetMemoryMap(&Meta);
        if (EFI_ERROR(Status)) {
            PANIC("Failed to call `GetMemoryMap`.");
        }

        /* IMPORTANT NOTE: DO NOT DO ANYTHING BETWEEN GETTING THE MAP AND EXITING BOOT SERVICES. */
        Status = BS->ExitBootServices(ENTRY_HANDLE, Meta.MapKey);
        if (EFI_ERROR(Status)) {
            PANIC("Failed to call `ExitBootServices` properly.");
        }

        FinalizeExitBootServices(&Meta);
    }
    // End TODO

    /* Set raw register values according to the Multiboot2 specification. */
    asm ("" :  : "a"(MULTIBOOT_INFO_MAGIC), "b"(MultibootContext->LoadedInfoHeader));

    goto ElfEntryWithMultiboot;


ElfEntrySkipMultiboot:
    /* Always exit Boot Services when no multiboot exists or could be configured. */

    /* De-init the loader before leaving forever. */
    LoaderDestroyContext(Context);
    EFI_WARNINGLN("BOOTING ELF (normal) entry at %p...", (VOID *)(LoadedElf.LoadedImageEntry));

    Status = GetMemoryMap(&Meta);
    if (EFI_ERROR(Status)) {
        PANIC("Failed to call `GetMemoryMap`.");
    }

    /* IMPORTANT NOTE: DO NOT DO ANYTHING BETWEEN GETTING THE MAP AND EXITING BOOT SERVICES. */
    Status = BS->ExitBootServices(ENTRY_HANDLE, Meta.MapKey);
    if (EFI_ERROR(Status)) {
        PANIC("Failed to call `ExitBootServices` properly.");
    }

    FinalizeExitBootServices(&Meta);


ElfEntryWithMultiboot:
    /* Call the ELF entrypoint. Do NOT expect a return value or to come back from it. */
    ((ELF_ENTRYPOINT)(LoadedElf.LoadedImageEntry))();

    /* Hang indefinitely. Since we've finalized Boot Services here, it's not
        very feasible to try displaying anything to indicate the loaded OS returned. */
    while (TRUE);
}


EFI_EXECUTABLE_LOADER ElfLoader = { .Load = LoadImage };
