#include "../include/loaders/loader.h"

// #include "../include/loaders/disk.h"
// #include "../include/loaders/exe.h"
// #include "../include/loaders/elf.h"
// #include "../include/loaders/bin.h"

#include "../include/drivers/displays/graphics.h"
#include "../include/drivers/displays/text.h"
#include "../include/drivers/displays/fb.h"

#include "../include/mftah_uefi.h"
#include "../include/core/util.h"



/* Gonna be a yikes from me. */
#define LOADER_PANIC(Message) \
{ \
    ErrorMsg = (CHAR8 *)AllocateZeroPool(sizeof(CHAR8) * 256); \
    if (DisplayMode & GRAPHICAL && NULL != ErrorMsg) { \
        AsciiSPrint(ErrorMsg, 256-1, Message " Exit Code: %u", Status); \
        GPrint(ErrorMsg, FB->BLT, 20, 20, 0xFFFF0000, 0, TRUE, 2); \
        FB->Flush(FB); \
        FreePool(ErrorMsg); \
        uefi_call_wrapper(BS->Stall, 1, 10000000); \
        Shutdown(Status); \
    } \
    FreePool(ErrorMsg); \
    EFI_COLOR(MFTAH_COLOR_PANIC); \
    PRINT(Message); \
    uefi_call_wrapper(BS->Stall, 1, 10000000); \
    Shutdown(Status); \
}



typedef
struct {
    CHAIN_TYPE              Type;
    EFI_EXECUTABLE_LOADER   *FormatHandle;
} _PACKED LOADER_PAIR;

STATIC CONST LOADER_PAIR
ChainTypesToLoaders[] = {
    // { DISK, DiskLoader },
    // { EXE,  ExeLoader  },
    // { ELF,  ElfLoader  },
    // { BIN,  BinLoader  },
    { 0,    NULL       }
};


STATIC VOLATILE BOOLEAN IsLoading = FALSE;

STATIC BOUNDED_SHAPE *LoadingIconUnderlayBlt,
                     *ProgressBlt;
STATIC COLOR_PAIR ProgressBarColors = {0},
                  TextColors = {0};
STATIC CONST CHAR8 *ProgressStatusMessage = NULL;



STATIC
VOID
FlipToFalse(EFI_EVENT Event,
            VOID *Context)
{ BOOLEAN *b = (BOOLEAN *)Context; if (NULL != b) *b = FALSE; }

STATIC
EFIAPI
VOID
Stall_GraphicalMode(UINTN TimeInMilliseconds)
{
    VOLATILE BOOLEAN Stall = TRUE;
    EFI_EVENT StallEvent = {0};

    uefi_call_wrapper(BS->CreateEvent, 5, (EVT_TIMER | EVT_NOTIFY_SIGNAL), TPL_NOTIFY, FlipToFalse, (VOID *)&Stall, &StallEvent);
    uefi_call_wrapper(BS->SetTimer, 3, StallEvent, TimerPeriodic, 10 * 1000 * TimeInMilliseconds);

    if (EFI_ERROR(LoadingAnimationLoop(LoadingIconUnderlayBlt, 0xFFFFFFFF, &Stall))) {
        /* Stall normally if the animation can't be rendered. */
        uefi_call_wrapper(BS->Stall, 1, TimeInMilliseconds * 1000);
    }

    uefi_call_wrapper(BS->CloseEvent, 1, StallEvent);
}


STATIC
EFIAPI
VOID
Stall_TextMode(UINTN TimeInMilliseconds)
{
    // TODO: Is it possible to make an entropy-based text animation?
    uefi_call_wrapper(BS->Stall, 1, TimeInMilliseconds * 1000);
}


STATIC
VOID
Progress_TextMode(IN CONST UINTN *Current,
                  IN CONST UINTN *OutOfTotal,
                  IN VOID *Extra)
{
}


STATIC
VOID
Progress_GraphicalMode(IN CONST UINTN *Current,
                       IN CONST UINTN *OutOfTotal,
                       IN VOID *Extra)
{
    if (NULL == Current || NULL == OutOfTotal) return;

    EFI_GRAPHICS_OUTPUT_BLT_PIXEL p = {0};
    BltPixelFromARGB(&p, TextColors.Background);
    FB->ClearBlt(FB, ProgressBlt, &p);

    FB_VERTEX ProgressRect = {
        .X = ProgressBlt->Dimensions.Width / 8,
        .Y = ProgressBlt->Dimensions.Height - 60
    };
    FB_VERTEX ProgressRectTo = {
        .X = 7 * (ProgressBlt->Dimensions.Width / 8),
        .Y = ProgressBlt->Dimensions.Height - 10
    };
    FB_VERTEX ProgressRectForegroundTo = {
        .X = ProgressRect.X + ((*Current * (ProgressRectTo.X - ProgressRect.X)) / *OutOfTotal),
        .Y = ProgressRectTo.Y
    };

    if (NULL != ProgressStatusMessage) {
        GPrint(ProgressStatusMessage,
               ProgressBlt,
               (ProgressBlt->Dimensions.Width / 2) - ((AsciiStrLen(ProgressStatusMessage) * 2 * FB->BaseGlyphSize.Width) / 2),
               30,
               TextColors.Foreground,
               TextColors.Background,
               FALSE,
               2);
    }

    /* Progress bar background, foreground, then box border. */
    FB->DrawSimpleShape(FB, ProgressBlt, FbShapeRectangle, ProgressRect, ProgressRectTo, 0, TRUE, 1, ProgressBarColors.Background);
    FB->DrawSimpleShape(FB, ProgressBlt, FbShapeRectangle, ProgressRect, ProgressRectForegroundTo, 0, TRUE, 1, ProgressBarColors.Foreground);
    FB->DrawSimpleShape(FB, ProgressBlt, FbShapeRectangle, ProgressRect, ProgressRectTo, 0, FALSE, 1, 0x00000000);

    /* Full progress BLT box border. */
    FB_VERTEX Origin = {0};
    FB_VERTEX FullBltEnd = { .X = ProgressBlt->Dimensions.Width, .Y = ProgressBlt->Dimensions.Height };
    FB->DrawSimpleShape(FB, ProgressBlt, FbShapeRectangle, Origin, FullBltEnd, 0, FALSE, 1, TextColors.Foreground);

    FB->BltToBlt(FB, FB->BLT, ProgressBlt, ProgressBlt->Position, Origin, ProgressBlt->Dimensions);
    FB->FlushPartial(FB, ProgressBlt->Position.X, ProgressBlt->Position.Y,
        ProgressBlt->Position.X, ProgressBlt->Position.Y,
        ProgressBlt->Dimensions.Width, ProgressBlt->Dimensions.Height);
}


STATIC
EFIAPI
EFI_STATUS
LoaderReadImage(IN CONFIG_CHAIN_BLOCK *Chain,
                IN HOOK_PROGRESS ProgressFunc,
                IN HOOK_STALL StallFunc,
                OUT EFI_PHYSICAL_ADDRESS *ImageBase,
                OUT UINTN *ImageSize)
{
    EFI_STATUS Status = EFI_SUCCESS;
    UINTN at = 0, total = 100;

    ProgressStatusMessage = "Locating File...";

    /* Render the initial progress details and the stall art */
    ProgressFunc(&at, &total, NULL);
    StallFunc(500);

    if (NULL == Chain->PayloadPath || '\0' == *(Chain->PayloadPath)) {
        return EFI_LOAD_ERROR;   /* bad/null filename */
    }

    /* Use the base image handle as the relative filesystem to load from. If a path
        is prefixed by a volume name, try to get that volume's handle instead. */
    EFI_HANDLE TargetHandle = gImageHandle;

    CHAR8 *p = Chain->PayloadPath;
    CHAR8 *s = p;

    for (; *p; ++p) {
        if (':' == *p) {
            s = p;
            break;
        }
    }

    if (s != Chain->PayloadPath) {
        *s = '\0';   /* terminate the string here */
        ++s;   /* increment by one to set it to the filename */
        if ('\0' == *s) return EFI_LOAD_ERROR;

        /* Get the unicode version of the volume name string. */
        CHAR16 *VolumeName = AsciiStrToUnicode(Chain->PayloadPath);

        ProgressStatusMessage = "Opening Volume...";
        at = 50; ProgressFunc(&at, &total, NULL);
        StallFunc(500);

        ERRCHECK(GetFileSystemHandleByVolumeName(VolumeName, &TargetHandle));
    }

    /* Do stuff with slight stalls between progress messages. */
    ProgressStatusMessage = "Reading Payload...";
    at = 0; ProgressFunc(&at, &total, NULL);
    StallFunc(500);

    /* NOTE: Use the 's' starting point here because it's always set to the full file path. */
    CHAR16 *PayloadPath = AsciiStrToUnicode(s);
    if (NULL == PayloadPath) return EFI_OUT_OF_RESOURCES;

    /* Convert the path separators to the m$ version ('\'). Not doing this
        will cause `ReadFile` to return errors. */
    for (CHAR16 *s = PayloadPath; *s; ++s) if (L'/' == *s) *s = L'\\';

    ERRCHECK(ReadFile(TargetHandle,
                      PayloadPath,
                      0U,
                      (VOID **)ImageBase,
                      ImageSize,
                      (TargetHandle == gImageHandle),
                      EfiReservedMemoryType,   /* always mark payload as reserved in e820/memmap */
                      ProgressFunc));
    FreePool(PayloadPath);

    /* Close out with a completed progress detail and a small stall. */
    ProgressStatusMessage = "Success!";
    at = total;
    ProgressFunc(&at, &total, NULL);
    StallFunc(1500);

    return EFI_SUCCESS;
}


STATIC
EFIAPI
EFI_STATUS
LoaderMftahDecrypt(IN LOADER_CONTEXT *Context)
{
    // TODO
    return EFI_SUCCESS;
}


STATIC
EFIAPI
EFI_STATUS
LoaderDecompress(IN LOADER_CONTEXT *Context)
{
    // TODO
    return EFI_SUCCESS;
}



VOID
LoaderDestroyContext(IN LOADER_CONTEXT *Context)
{
    /* One of the final methods called by this program. Therefore, it should also
        destroy the current framebuffer handle. */
    ConfigDestroyChain(Context->Chain);
    FreePool(Context);

    BltDestroy(ProgressBlt);
    BltDestroy(LoadingIconUnderlayBlt);

    FramebufferDestroy();
}


EFI_STATUS
LoaderValidateChain(IN CONFIGURATION *c,
                    IN MENU_STATE *m,
                    OUT CHAR8 *ErrorMsg)
{
    if (
        NULL == c
        || NULL == m
        || NULL == ErrorMsg
    ) return EFI_INVALID_PARAMETER;

    // TODO!
    /* Check to ensure chain properties qualify for loading before actually doing it. */

    return EFI_SUCCESS;
}


VOID
LoaderEnterChain(IN CONFIGURATION *c,
                 IN MENU_STATE *m,
                 IN EFI_MENU_RENDERER_PROTOCOL *MENU)
{
    /* Basically just free up resources and switch to the right type.
        Through this method, we're effectively just switching back to a simple text mode. */
    EFI_STATUS Status = EFI_SUCCESS;
    CHAR8 *ErrorMsg = NULL;
    DISPLAY_MODE DisplayMode = c->Mode;

    /* Clear the screen immediately. */
    MENU->ClearScreen(0);

    /* Just steal some colors from the pallette. */
    ProgressBarColors = c->Colors.Title;
    TextColors = c->Colors.Text;

    CONFIG_CHAIN_BLOCK *chain = (CONFIG_CHAIN_BLOCK *)AllocateZeroPool(sizeof(CONFIG_CHAIN_BLOCK));
    if (NULL == chain) {
        Status = EFI_OUT_OF_RESOURCES;
        LOADER_PANIC("Failed to duplicate the loaded chain: out of resources!");
    }

    /* A DEEP clone is required, since all pointers will be freed. */
    CopyMem(chain, c->Chains[m->CurrentItemIndex], sizeof(CONFIG_CHAIN_BLOCK));

    CHAR8 *Name = (CHAR8 *)AllocateZeroPool(AsciiStrLen(chain->Name) + 1);
    CopyMem(Name, chain->Name, AsciiStrLen(chain->Name));
    chain->Name = Name;

    CHAR8 *TargetPath = (CHAR8 *)AllocateZeroPool(AsciiStrLen(chain->TargetPath) + 1);
    CopyMem(TargetPath, chain->TargetPath, AsciiStrLen(chain->TargetPath));
    chain->TargetPath = TargetPath;

    CHAR8 *PayloadPath = (CHAR8 *)AllocateZeroPool(AsciiStrLen(chain->PayloadPath) + 1);
    CopyMem(PayloadPath, chain->PayloadPath, AsciiStrLen(chain->PayloadPath));
    chain->PayloadPath = PayloadPath;

    /* Load the target image into memory. */
    UINTN ImageBase = 0;
    UINTN ImageSize = 0;
    HOOK_PROGRESS ProgressFunc = NULL;
    HOOK_STALL StallFunc = NULL;

    switch (c->Mode) {
        case GRAPHICAL:
            /* GUI progress indication requires a bit more initialization. */
            ProgressFunc = Progress_GraphicalMode;
            StallFunc = Stall_GraphicalMode;

            EFI_GRAPHICS_OUTPUT_BLT_PIXEL p = {0};
            BltPixelFromARGB(&p, TextColors.Background);

            Status = NewObjectBlt((FB->Resolution.Width / 2) - MIN(250, (FB->Resolution.Width / 4)),
                                  (FB->Resolution.Height / 5),
                                  MIN(500, (FB->Resolution.Width / 2)),
                                  MIN(250, (FB->Resolution.Height / 3)),
                                  1,
                                  &ProgressBlt);
            if (EFI_ERROR(Status)) {
                LOADER_PANIC("Failed to allocate a BLT for the progress notifier.");
            }
            FB->ClearBlt(FB, ProgressBlt, &p);

            Status = NewObjectBlt((FB->Resolution.Width / 2) - 100,
                                  ProgressBlt->Position.Y + ProgressBlt->Dimensions.Height + 30,
                                  200,
                                  200,
                                  2,
                                  &LoadingIconUnderlayBlt);
            if (EFI_ERROR(Status)) {
                LOADER_PANIC("Failed to allocate the loading animation underlay BLT.");
            }
            SetMem(&p, sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL), 0);
            FB->ClearBlt(FB, LoadingIconUnderlayBlt, &p);

            break;

        case TEXT:
            ProgressFunc = Progress_TextMode;
            StallFunc = Stall_TextMode;
            break;
    }

    /* Based on the mode, deinit everything. Yea I know I could have put it with
        the above stuff. I like to keep it separate in case it needs relocating. */
    switch (c->Mode) {
        case GRAPHICAL:
            GraphicsDestroy(TRUE);
            break;
        case TEXT:
            TextModeDestroy();
            break;
    }

    /* Deinit the config objects. */
    ConfigDestroy();

    /* Destroy menu and menu renderer objects. */
    FreePool(m);
    FreePool(MENU);

    /* Read the payload file from block storage. */
    Status = LoaderReadImage(chain, ProgressFunc, StallFunc, &ImageBase, &ImageSize);
    if (EFI_ERROR(Status)) {
        LOADER_PANIC("Failed to read the target payload into memory.");
    }

    /* Set up the context object and send it to the right chain. */
    LOADER_CONTEXT *Context = (LOADER_CONTEXT *)AllocateZeroPool(sizeof(LOADER_CONTEXT));
    if (NULL == Context) {
        Status = EFI_OUT_OF_RESOURCES;
        LOADER_PANIC("Failed to allocate loader context: out of resources!");
    }

    Context->Chain = chain;
    Context->LoadedImageBase = ImageBase;
    Context->LoadedImageSize = ImageSize;
    Context->ProgressFunc = ProgressFunc;
    Context->StallFunc = StallFunc;

    /* Check the chain's properties. This occurs in a certain order. For example,
        MFTAH is always the OUTERMOST layer when compared to compression, because
        compressing AES-256 data is rather useless. */
    if (chain->IsMFTAH) {
        // TODO: Add an optional 'MFTAHKEY' config element to the chain for auto-decryption.
        Status = LoaderMftahDecrypt(Context);
        if (EFI_ERROR(Status)) {
            LOADER_PANIC("MFTAH payload decryption returned a failure code.");
        }
    }

    if (chain->IsCompressed) {
        // TODO: Compression compatibility.
        Status = LoaderDecompress(Context);
        if (EFI_ERROR(Status)) {
            LOADER_PANIC("Payload decompression returned a failure code.");
        }
    }

    for (UINTN i = 0; ; ++i) {
        if (
            NULL == ChainTypesToLoaders[i].Type
            || NULL == ChainTypesToLoaders[i].FormatHandle
        ) break;

        if (chain->Type == ChainTypesToLoaders[i].Type) {
            /* let's a-go! */
            ChainTypesToLoaders[i].FormatHandle->Load(Context);

            Status = EFI_LOAD_ERROR;
            LOADER_PANIC("Control somehow returned from the chainloader hook.");
        }
    }

    /* End of the road. */
    Status = EFI_NOT_STARTED;
    LOADER_PANIC("Could not transfer control to the right loader.");
}
