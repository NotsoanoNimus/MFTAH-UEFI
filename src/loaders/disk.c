#include "../include/loaders/disk.h"
#include "../include/loaders/elf.h"
#include "../include/loaders/exe.h"
#include "../include/loaders/bin.h"

#include "../include/drivers/ramdisk.h"



STATIC
EFIAPI
VOID
LoadImage(IN LOADER_CONTEXT *Context)
{
    /* The context contains the base address and length of a ramdisk to load
     * a target executable from (specified in the chain). Search for the file
     * then load it and execute it based on its specified sub-type. */
    /* ===== */

    /* First, set up the ramdisk through the driver. This registers it as an available SFS handle. */
    //

    /* Next, find the target image to chainload and load it to another segment of reserved memory. */
    //

    /* Ramdisks are a bit different: the sub-type loader is now called on the loaded image.
     * Even though the LoadedImage properties are changed on the chain from the loader context,
     * the original ramdisk remains a loaded piece of reserved memory for the chainloaded OS to
     * use and discover through the newly-entered ACPI NFIT entry. */
    switch (Context->Chain->SubType) {
        case EXE:   ExeLoader  .Load(Context); break;
        case ELF:   ElfLoader  .Load(Context); break;
        case BIN:   BinLoader  .Load(Context); break;
        default: break;
    }
}


EFI_EXECUTABLE_LOADER DiskLoader = { .Load = LoadImage };
