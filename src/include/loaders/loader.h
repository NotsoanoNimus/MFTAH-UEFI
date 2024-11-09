#ifndef MFTAH_LOADER_H
#define MFTAH_LOADER_H

// #include "../core/mftah.h"
// #include "../core/compression.h"

#include "../drivers/config.h"



typedef
struct {
    CONFIG_CHAIN_BLOCK      *Chain;
    EFI_PHYSICAL_ADDRESS    LoadedImageBase;
    UINTN                   LoadedImageSize;
    EFI_DEVICE_PATH         *LoadedImageDevicePath;
    mftah_payload_t         *MftahPayloadWrapper;
} LOADER_CONTEXT;



/* NOTE: Leaving this as a struct in case more properties should be added. */
typedef
struct {
    VOID    (*Load)(LOADER_CONTEXT *Context);
    VOID    *ExtraInfo;
} EFI_EXECUTABLE_LOADER;


VOID
LoaderDestroyContext(IN LOADER_CONTEXT *Context);

VOID
LoaderEnterChain(IN UINTN SelectedItemIndex);



#endif   /* MFTAH_LOADER_H */
