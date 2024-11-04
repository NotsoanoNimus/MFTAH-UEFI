#include "../include/loaders/bin.h"



STATIC
EFIAPI
VOID
LoadImage(IN LOADER_CONTEXT *Context)
{
    EFI_STATUS Status = EFI_SUCCESS;
    PANIC("Loading BIN");
}


EFI_EXECUTABLE_LOADER BinLoader = { .Load = LoadImage };
