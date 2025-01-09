#include "../include/loaders/multiboot_exec.h"


EFI_GUID gEfiMultiboot2ProtocolGuid = EFI_MULTIBOOT2_PROTOCOL_GUID;



STATIC
EFI_STATUS
EFIAPI
SeekHeader(IN EFI_MULTIBOOT2_PROTOCOL *This,
           IN EFI_PHYSICAL_ADDRESS LoadedImageBase,
           OUT EFI_PHYSICAL_ADDRESS *HeaderLocation)
{
    if (
        0 == LoadedImageBase
        || NULL == This
        || NULL == HeaderLocation
    ) return EFI_INVALID_PARAMETER;

    /* Note that the searched alignment is relative to the image's start. */
    for (
        UINTN i = 0;
        i < MULTIBOOT_SEARCH_LIMIT;
        i += MULTIBOOT_SEARCH_ALIGNMENT
    ) {
        if (MULTIBOOT_MAGIC_HEADER != *((UINT32 *)(LoadedImageBase + i))) continue;

        *HeaderLocation = (LoadedImageBase + i);
        return EFI_SUCCESS;
    }

    return EFI_NOT_FOUND;
}


STATIC
EFI_STATUS
EFIAPI
ParseHeader(IN EFI_MULTIBOOT2_PROTOCOL *This,
            IN EFI_PHYSICAL_ADDRESS HeaderAddress,
            OUT EFI_MULTIBOOT2_CONTEXT **ResultContext)
{
    if (
        NULL == This
        || 0 == HeaderAddress
        || NULL == ResultContext
    ) return EFI_INVALID_PARAMETER;

    /* Sanity check. */
    if (MULTIBOOT_MAGIC_HEADER != *((UINT32 *)HeaderAddress)) {
        return EFI_NOT_FOUND;
    }

    MultibootMagicHeader *Header = (MultibootMagicHeader *)HeaderAddress;

    /* Validate the checksum. */
    if (0U != (UINT32)(Header->Magic + Header->Architecture + Header->HeaderLength + Header->Checksum)) {
        return EFI_PROTOCOL_ERROR;
    }

    if (Header->HeaderLength < sizeof(MultibootMagicHeader)) {
        return EFI_BAD_BUFFER_SIZE;
    }

    /* Allocate the resulting context. */
    *ResultContext = (EFI_MULTIBOOT2_CONTEXT *)
        AllocateZeroPool(sizeof(EFI_MULTIBOOT2_CONTEXT));
    if (NULL == *ResultContext) return EFI_OUT_OF_RESOURCES;

    /* At this point, if there is anything to load in terms of tags, then do it.
        Otherwise, exit success and leave everything NULL as it would be anyways. */
    if (sizeof(MultibootMagicHeader) == Header->HeaderLength) return EFI_SUCCESS;

    UINTN HeaderTagsSize = (Header->HeaderLength - sizeof(MultibootMagicHeader));
    EFI_PHYSICAL_ADDRESS ReferenceAddr = (HeaderAddress + sizeof(MultibootMagicHeader));

    for (UINTN i = 0; i < HeaderTagsSize;) {
        MultibootTagHeader *StartOfTag = (MultibootTagHeader *)(ReferenceAddr + i);
        if (MB_END == StartOfTag->Type) break;

#       define ASSOC_TYPE_PTR(TypeId, FieldName, Type) \
            case TypeId: (*ResultContext)->FieldName = (Type *)StartOfTag; break;

        switch (StartOfTag->Type) {
            /* It's possible for there to be multiple info tag requests because some
                might be specifically flagged as optional while others are required. */
            case MB_INFO_REQ:
                if (NULL == (*ResultContext)->TagRequest) {
                    (*ResultContext)->TagRequest = (MultibootTagInfoRequest *)StartOfTag;
                } else {
                    (*ResultContext)->TagRequestAlternate = (MultibootTagInfoRequest *)StartOfTag;
                }
                break;

            ASSOC_TYPE_PTR(MB_ADDR, TagAddresses, MultibootTagAddress);
            ASSOC_TYPE_PTR(MB_ENTRY, TagEntry, MultibootTagEntry);
            ASSOC_TYPE_PTR(MB_I386_ENTRY, Tag386Entry, MultibootTagEntry);
            ASSOC_TYPE_PTR(MB_AMD64_ENTRY, TagAmd64Entry, MultibootTagEntry);
            ASSOC_TYPE_PTR(MB_FLAGS, TagFlags, MultibootTagFlags);
            ASSOC_TYPE_PTR(MB_FRAMEBUFFER, TagFramebuffer, MultibootTagFramebuffer);
            ASSOC_TYPE_PTR(MB_MOD_ALIGN, TagIsModulePageAligned, MultibootTagBoolean);
            ASSOC_TYPE_PTR(MB_EFI_BS, TagDoNotExitBootServices, MultibootTagBoolean);
            ASSOC_TYPE_PTR(MB_RELOCATABLE, TagRelocatable, MultibootTagRelocatable);

            default: break;   /* skip unknown or useless types */
        }

#       undef ASSOC_TYPE_PTR

        /* Next tag. */
        i += StartOfTag->Size;
    }

    return EFI_SUCCESS;
}


STATIC
EFI_STATUS
EFIAPI
ValidateContext(IN EFI_MULTIBOOT2_PROTOCOL *This,
                IN EFI_MULTIBOOT2_CONTEXT *Context)
{
    if (
        NULL == This
        || NULL == Context
    ) return EFI_INVALID_PARAMETER;

    BOOLEAN CheckingAlternate = FALSE;
    MultibootTagInfoRequest *Request = Context->TagRequest;

RepeatValidationForAlt:
    if (NULL == Request) return EFI_SUCCESS;

    if (Request->Header.Size < sizeof(MultibootTagHeader)) return EFI_LOAD_ERROR;

    BOOLEAN IsOptional = (Request->Header.Flags & 1);
    UINTN TagsLength = (Request->Header.Size - sizeof(MultibootTagHeader));

    /* We don't care about validating optional requested tags. */
    if (TRUE == IsOptional) goto NextValidation;

    /* If it's not optional and no loaded details were provided, there's a problem. */
    if (TagsLength > 0 && NULL == Context->LoadedInfoHeader) return EFI_NOT_STARTED;

    for (UINTN i = 0; i < (TagsLength / sizeof(UINT32)); ++i) {
        UINT32 Type = Request->RequestedInfoTagTypes[i];

        if (MBI_END == Type) break;
        else if (MBI_IMG_LOAD_BASE < Type) return EFI_UNSUPPORTED;

        EFI_PHYSICAL_ADDRESS p = ((EFI_PHYSICAL_ADDRESS)(Context->LoadedInfoHeader) + sizeof(MultibootInfoHeader));
        BOOLEAN TagFound = FALSE;

        while (MBI_END != ((MultibootInfoTagHeader *)p)->Type) {
            if (Type == ((MultibootInfoTagHeader *)p)->Type) {
                TagFound = TRUE;
                break;
            }

            p += ((MultibootInfoTagHeader *)p)->Size;
        }

        if (FALSE == TagFound) return EFI_NOT_FOUND;
    }

NextValidation:
    /* Repeat once for the alt request, if set. */
    if (FALSE == CheckingAlternate) {
        Request = Context->TagRequestAlternate;
        CheckingAlternate = TRUE;

        goto RepeatValidationForAlt;
    }

    return EFI_SUCCESS;
}


STATIC
EFI_STATUS
EFIAPI
BuildInfoHeader(IN EFI_MULTIBOOT2_PROTOCOL *This,
                IN VOID **TagsPointers,
                IN UINTN TagsCount,
                OUT MultibootInfoHeader **InfoHeader,
                OUT UINTN *TagsLoaded)
{
    if (
        NULL == This
        || NULL == TagsPointers
        || NULL == *TagsPointers
        || 0 == TagsCount
        || NULL == InfoHeader
        || NULL == TagsLoaded
    ) return EFI_INVALID_PARAMETER;

    BOOLEAN ListNeedsTermination = FALSE;
    UINTN LoadedCount = 0;

    /* Iterate the tags once to get the total size so we can allocate it. */
    UINTN TotalSize = sizeof(MultibootInfoHeader);   /* start at 8 to acct for meta */
    for (UINTN i = 0; i < TagsCount; ++i) {
        if (NULL == TagsPointers[i]) continue;
        MultibootInfoTagHeader *StartOfTag = (MultibootInfoTagHeader *)(TagsPointers[i]);

        /* Skip empty tags. */
        if (0 == StartOfTag->Size) continue;

        TotalSize += StartOfTag->Size;

        if (i != (TagsCount - 1)) continue;

        /* If this is the last tag in the set and it's a terminator (NULL Type), note that. */
        ListNeedsTermination = (MBI_END != StartOfTag->Type);
    }

    /* Account for the space needed with a new terminating tag, as necessary. */
    if (TRUE == ListNeedsTermination) {
        TotalSize += sizeof(MultibootInfoTagHeader);
    }

    /* Fill out the info tags meta-structure. */
    MultibootInfoHeader *Result = (MultibootInfoHeader *)AllocateZeroPool(TotalSize);
    Result->TotalSize = TotalSize;
    Result->Reserved = 0;

    /* Starting address for info tags. */
    VOID *StoreTo = (VOID *)(((EFI_PHYSICAL_ADDRESS)Result) + sizeof(MultibootInfoHeader));

    /* Concatenate all tags by size, consecutively. */
    for (UINTN i = 0; i < TagsCount; ++i) {
        if (NULL == TagsPointers[i]) continue;
        MultibootInfoTagHeader *StartOfTag = (MultibootInfoTagHeader *)(TagsPointers[i]);

        /* Skip empty tags. */
        if (0 == StartOfTag->Size) continue;

        CopyMem(StoreTo, TagsPointers[i], StartOfTag->Size);
        ++LoadedCount;

        StoreTo = (VOID *)(((EFI_PHYSICAL_ADDRESS)StoreTo) + StartOfTag->Size);
    }

    /* Tie everything off. */
    if (TRUE == ListNeedsTermination) {
        ((MultibootInfoTagHeader *)StoreTo)->Size = sizeof(MultibootInfoTagHeader);
        ((MultibootInfoTagHeader *)StoreTo)->Type = MBI_END;
    }

    *InfoHeader = Result;
    *TagsLoaded = LoadedCount;
    return EFI_SUCCESS;
}



STATIC
CONST EFI_MULTIBOOT2_PROTOCOL Protocol = {
    .SeekHeader                 = SeekHeader,
    .Parse                      = ParseHeader,
    .ValidateContext            = ValidateContext,
    .BuildInfoHeaderFromTags    = BuildInfoHeader,
};

CONST EFI_MULTIBOOT2_PROTOCOL *
GetMultiboot2ProtocolInstance(VOID)
{
    return &Protocol;
}
