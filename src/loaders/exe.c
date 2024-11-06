#include "../include/loaders/exe.h"



STATIC
EFIAPI
VOID
LoadImage(IN LOADER_CONTEXT *Context)
{
    EFI_STATUS Status = EFI_SUCCESS;
    EFI_HANDLE LoadedImageHandle = NULL;
PRINTLN("EXE LOADER");

    /* This is relatively easy, just run the memory location in the context
        through LoadImage and StartImage Boot Services methods. */
    Status = uefi_call_wrapper(BS->LoadImage, 6,
                               FALSE,
                               ENTRY_HANDLE,
                               Context->LoadedImageDevicePath,
                               (VOID *)Context->LoadedImageBase,
                               Context->LoadedImageSize,
                               &LoadedImageHandle);
    if (EFI_ERROR(Status)) {
        // TODO: More granular panics here so the user knows wth is going on.
        PANIC("LoadImage: Critical exception encountered while readying the in-memory image.");
    }
PRINTLN("1");

    /* Clean up as many resources as we can think of. */
    // TODO: Trace all this. Tidy up.
    LoaderDestroyContext(Context);
PRINTLN("2");

    /* We should not ever come back from here. */
    Status = uefi_call_wrapper(BS->StartImage, 3,
                               LoadedImageHandle,
                               NULL,
                               NULL);
    if (EFI_INVALID_PARAMETER == Status) {
        PANIC("StartImage: Critical exception encountered while starting the loaded image.");
    } else if (EFI_SECURITY_VIOLATION == Status) {
        PANIC("StartImage: Platform security denied the request to chainload the loaded image.");
    }

    PANIC("Normal termination of MFTAH loader. Hanging indefinitely.");
}


EFI_EXECUTABLE_LOADER ExeLoader = { .Load = LoadImage };
