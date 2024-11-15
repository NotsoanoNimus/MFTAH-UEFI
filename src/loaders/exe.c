#include "../include/loaders/exe.h"

#include "../include/drivers/displays.h"



STATIC
EFIAPI
VOID
LoadImage(IN LOADER_CONTEXT *Context)
{
    EFI_STATUS Status = EFI_SUCCESS;
    EFI_HANDLE LoadedImageHandle = NULL;
    EFI_LOADED_IMAGE_PROTOCOL *LIP = NULL;

    /* This is relatively easy, just run the memory location in the context
        through LoadImage and StartImage Boot Services methods. */
    Status = BS->LoadImage(FALSE,
                           ENTRY_HANDLE,
                           Context->LoadedImageDevicePath,
                           (VOID *)Context->LoadedImageBase,
                           Context->LoadedImageSize,
                           &LoadedImageHandle);
    if (EFI_ERROR(Status) || NULL == LoadedImageHandle) {
        // TODO: More granular panics here so the user knows wth is going on.
        // TODO: Use displays PANIC instead
        PANIC("LoadImage: Critical exception encountered while readying the in-memory image.");
    }

    /* Skip over the below, no errors.
        Never-nesting goes HARD when you know what you're doin. */
    if (NULL == Context->Chain->CmdLine) goto LoadImage__NoCmdLine;

    Status = BS->HandleProtocol(LoadedImageHandle, &gEfiLoadedImageProtocolGuid, &LIP);
    if (!EFI_ERROR(Status) && NULL != LIP) goto LoadImage__ErrorNoCmdLine;

    /* First, reserve a copy of the cmdline data as a Reserved region. */
    CHAR8 *CmdCopy = NULL;
    UINTN CmdLen = AsciiStrLen(Context->Chain->CmdLine);

    BS->AllocatePool(EfiReservedMemoryType, CmdLen + 1, &CmdCopy);
    if (NULL == CmdCopy) goto LoadImage__ErrorNoCmdLine;
    CopyMem(CmdCopy, Context->Chain->CmdLine, CmdLen);

    /* Set the load option to the reserved command line. */
    LIP->LoadOptions = (VOID *)CmdCopy;
    LIP->LoadOptionsSize = CmdLen + 1;

LoadImage__ErrorNoCmdLine:
    DISPLAY->Panic(DISPLAY,
                   "Failed to pass the command line to the loaded image.",
                   TRUE,
                   10000000);
    HALT;

LoadImage__NoCmdLine:
    /* Clean up as many resources as we can think of. */
    // TODO: Trace all this. Tidy up.
    LoaderDestroyContext(Context);

    /* We should not ever come back from here. */
    Status = BS->StartImage(LoadedImageHandle, NULL, NULL);

    // TODO: Should really just fall back to the `native` mode here and call `Print`.
    if (EFI_INVALID_PARAMETER == Status) {
        PANIC("StartImage: Critical exception encountered while starting the loaded image.");
    } else if (EFI_SECURITY_VIOLATION == Status) {
        PANIC("StartImage: Platform security denied the request to chainload the loaded image.");
    }

    PANIC("Normal termination of MFTAH loader. Hanging indefinitely.");
}


EFI_EXECUTABLE_LOADER ExeLoader = { .Load = LoadImage };
