#ifndef MFTAH_LOADER_H
#define MFTAH_LOADER_H

// #include "../core/mftah.h"
// #include "../core/compression.h"

#include "../drivers/config.h"
#include "../drivers/displays/menu_structs.h"



typedef
mftah_fp__progress_hook_t
HOOK_PROGRESS;

typedef
VOID
(EFIAPI *HOOK_STALL)(
    IN UINTN TimeInMilliseconds
);


typedef
struct {
    CONFIG_CHAIN_BLOCK      *Chain;
    HOOK_PROGRESS           ProgressFunc;
    HOOK_STALL              StallFunc;
    EFI_PHYSICAL_ADDRESS    LoadedImageBase;
    UINTN                   LoadedImageSize;
} _PACKED LOADER_CONTEXT;



/* NOTE: Leaving this as a struct in case more properties should be added. */
typedef
struct {
    VOID    (*Load)(LOADER_CONTEXT *Context);
} _PACKED EFI_EXECUTABLE_LOADER;



VOID
LoaderDestroyContext(
    IN LOADER_CONTEXT *Context
);


EFI_STATUS
LoaderValidateChain(
    IN CONFIGURATION    *c,
    IN MENU_STATE       *m,
    OUT CHAR8           *ErrorMsg
);


VOID
LoaderEnterChain(
    IN CONFIGURATION                *c,
    IN MENU_STATE                   *m,
    IN EFI_MENU_RENDERER_PROTOCOL   *MENU
);



#endif   /* MFTAH_LOADER_H */
