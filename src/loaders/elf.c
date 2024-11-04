#include "../include/loaders/elf.h"



STATIC
EFIAPI
VOID
LoadImage(IN LOADER_CONTEXT *Context)
{
    EFI_STATUS Status = EFI_SUCCESS;
    PANIC("Loading ELF");
}


EFI_EXECUTABLE_LOADER ElfLoader = { .Load = LoadImage };
