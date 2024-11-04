#include "../include/loaders/exe.h"



STATIC
EFIAPI
VOID
LoadImage(IN LOADER_CONTEXT *Context)
{
    EFI_STATUS Status = EFI_SUCCESS;
    PANIC("Loading EXE");
}


EFI_EXECUTABLE_LOADER ExeLoader = { .Load = LoadImage };
