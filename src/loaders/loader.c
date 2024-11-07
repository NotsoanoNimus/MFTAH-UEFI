#include "../include/loaders/loader.h"

#include "../include/loaders/disk.h"
#include "../include/loaders/exe.h"
#include "../include/loaders/elf.h"
#include "../include/loaders/bin.h"

#include "../include/drivers/displays/graphics.h"
#include "../include/drivers/displays/text.h"
#include "../include/drivers/displays/fb.h"
#include "../include/drivers/mftah_adapter.h"
#include "../include/drivers/ramdisk.h"

#include "../include/core/input.h"

#include "../include/mftah_uefi.h"
#include "../include/core/util.h"



/* Gonna be a yikes from me. */
// TODO: Convert this shit into a function, idc what it takes it's code BLOAT and it's confusing.
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
} __attribute__((packed)) LOADER_PAIR;


STATIC BOUNDED_SHAPE *LoadingIconUnderlayBlt,
                     *ProgressBlt,
                     *MftahKeyPromptBlt;

STATIC COLOR_PAIR ProgressBarColors = {0},
                  TextColors = {0};

STATIC BOOLEAN IsQuick = FALSE;

STATIC CONST CHAR8 *ProgressStatusMessage = NULL,
                   *InputErrorMessage = NULL;



STATIC
EFIAPI
VOID
DrawMftahKeyPrompt__GraphicsMode(UINTN PassLength)
{
    CHAR8 *EnterPasswordMessage = "Enter Password:";
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL p = {0};

    BltPixelFromARGB(&p, TextColors.Background);
    FB->ClearBlt(FB, MftahKeyPromptBlt, &p);

    FB_VERTEX PromptRect = {
            .X = MftahKeyPromptBlt->Dimensions.Width / 8,
            .Y = MftahKeyPromptBlt->Dimensions.Height - 30 - (2 * FB->BaseGlyphSize.Height)
    };
    FB_VERTEX PromptRectTo = {
            .X = 7 * (MftahKeyPromptBlt->Dimensions.Width / 8),
            .Y = MftahKeyPromptBlt->Dimensions.Height - 30
    };

    GPrint(EnterPasswordMessage,
           MftahKeyPromptBlt,
           (MftahKeyPromptBlt->Dimensions.Width / 2)
               - ((AsciiStrLen(EnterPasswordMessage) * 2 * FB->BaseGlyphSize.Width) / 2),
           20,
           TextColors.Foreground,
           TextColors.Background,
           FALSE,
           2);

    if (NULL != InputErrorMessage) {
        GPrint(InputErrorMessage,
               MftahKeyPromptBlt,
               (MftahKeyPromptBlt->Dimensions.Width / 2)
                   - ((AsciiStrLen(InputErrorMessage) * 1 * FB->BaseGlyphSize.Width) / 2),
               20 + (2 * FB->BaseGlyphSize.Height) + 30,
               0xFFFF0000,
               TextColors.Background,
               FALSE,
               1);
    }

    /* Input bar background and border. */
    FB->DrawSimpleShape(FB, MftahKeyPromptBlt, FbShapeRectangle, PromptRect, PromptRectTo, 0, TRUE, 1, ProgressBarColors.Background);
    FB->DrawSimpleShape(FB, MftahKeyPromptBlt, FbShapeRectangle, PromptRect, PromptRectTo, 0, FALSE, 1, 0x00000000);

    /* Full BLT box border. */
    FB_VERTEX Origin = {0};
    FB_VERTEX FullBltEnd = { .X = MftahKeyPromptBlt->Dimensions.Width, .Y = MftahKeyPromptBlt->Dimensions.Height };
    FB->DrawSimpleShape(FB, MftahKeyPromptBlt, FbShapeRectangle, Origin, FullBltEnd, 0, FALSE, 1, TextColors.Foreground);

    /* Never exceed the width of the box when GPrint'ing. */
    UINTN StarsToPrintInBox = MIN(
        PassLength,
        ((PromptRectTo.X - PromptRect.X) / (2 * FB->BaseGlyphSize.Width))
    );

    /* Print the '*' characters to match the current length of the password or the max width. */
    for (UINTN i = 0; i < StarsToPrintInBox; ++i) {
        GPrint("*",
               MftahKeyPromptBlt,
               PromptRect.X + 5 + (i * 2 * FB->BaseGlyphSize.Width),
               PromptRect.Y,
               ProgressBarColors.Foreground,
               ProgressBarColors.Background,
               FALSE,
               2);
    }

    /* Finally, render it all. */
    FB->RenderComponent(FB, MftahKeyPromptBlt, TRUE);
}


STATIC
EFIAPI
VOID
DrawMftahKeyPrompt__TextMode(LOADER_CONTEXT *Context,
                             UINTN PassLength)
{
}


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

    if (TRUE == IsQuick) return;

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
    if (TRUE == IsQuick) return;

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

    FB->RenderComponent(FB, ProgressBlt, TRUE);
}


STATIC
EFIAPI
EFI_STATUS
LoaderReadImage(IN LOADER_CONTEXT *Context)
{
    EFI_STATUS Status = EFI_SUCCESS;
    UINTN at = 0, total = 100;

    if (NULL == Context) return EFI_INVALID_PARAMETER;

    ProgressStatusMessage = "Locating File...";

    /* Render the initial progress details and the stall art */
    Context->ProgressFunc(&at, &total, NULL);
    Context->StallFunc(500);

    if (NULL == Context->Chain->PayloadPath || '\0' == *(Context->Chain->PayloadPath)) {
        return EFI_LOAD_ERROR;   /* bad/null filename */
    }

    /* Use the base image handle as the relative filesystem to load from. If a path
        is prefixed by a volume name, try to get that volume's handle instead. */
    EFI_HANDLE TargetHandle = ENTRY_HANDLE;

    CHAR8 *p = Context->Chain->PayloadPath;
    CHAR8 *s = p;

    for (; *p; ++p) {
        if (':' == *p) {
            s = p;
            break;
        }
    }

    if (s != Context->Chain->PayloadPath) {
        *s = '\0';   /* terminate the string here */
        ++s;   /* increment by one to set it to the filename */
        if ('\0' == *s) return EFI_LOAD_ERROR;

        /* Get the unicode version of the volume name string. */
        CHAR16 *VolumeName = AsciiStrToUnicode(Context->Chain->PayloadPath);

        ProgressStatusMessage = "Opening Volume...";
        at = 50; Context->ProgressFunc(&at, &total, NULL);
        Context->StallFunc(500);

        Status = GetFileSystemHandleByVolumeName(VolumeName, &TargetHandle);
        FreePool(VolumeName);

        if (EFI_ERROR(Status)) return Status;
    }

    /* Set the context's device handle for chainloaded images,
            in case we're loading another EFI application. */
    Context->LoadedImageDevicePath = DevicePathFromHandle(TargetHandle);

    /* Do stuff with slight stalls between progress messages. */
    ProgressStatusMessage = "Reading Payload...";
    at = 0; Context->ProgressFunc(&at, &total, NULL);
    Context->StallFunc(500);

    /* NOTE: Use the 's' starting point here because it's always set to the full file path. */
    CHAR16 *PayloadPath = AsciiStrToUnicode(s);
    if (NULL == PayloadPath) return EFI_OUT_OF_RESOURCES;

    /* Convert the path separators to the m$ version ('\'). Not doing this
        will cause `ReadFile` to return errors. */
    for (CHAR16 *s = PayloadPath; *s; ++s) if (L'/' == *s) *s = L'\\';

    ERRCHECK(ReadFile(TargetHandle,
                      PayloadPath,
                      0U,
                      (UINT8 **)&(Context->LoadedImageBase),
                      &(Context->LoadedImageSize),
                      (TargetHandle == ENTRY_HANDLE),
                      EfiReservedMemoryType,   /* always mark payload as reserved in e820/memmap */
                      RAM_DISK_BLOCK_SIZE,
                      (Context->Chain->IsMFTAH ? sizeof(mftah_payload_header_t) : 0),
                      Context->ProgressFunc));
    FreePool(PayloadPath);

    /* Close out with a completed progress detail and a small stall. */
    ProgressStatusMessage = "Success!";
    at = total;
    Context->ProgressFunc(&at, &total, NULL);
    Context->StallFunc(1500);

    return EFI_SUCCESS;
}


STATIC
EFIAPI
EFI_STATUS
LoaderGetAndValidateMftahKey(IN LOADER_CONTEXT *Context)
{
    mftah_status_t MftahStatus = MFTAH_SUCCESS;
    mftah_protocol_t *MftahProtocol = NULL;
    CHAR8 *p = NULL, *Password = NULL;
    UINTN PassLength = 0;
    EFI_KEY_DATA Key = {0};

    if (0 == Context->LoadedImageBase || 0 == Context->LoadedImageSize) {
        EFI_DANGERLN("Invalid base image parameters in context.");
        return EFI_BAD_BUFFER_SIZE;
    }

    Password = (CHAR8 *)AllocateZeroPool(sizeof(CHAR8) * (MFTAH_MAX_PW_LEN + 1));
    if (NULL == Password) {
        EFI_DANGERLN("Failed to allocate a MFTAH password buffer: out of memory.");
        return EFI_OUT_OF_RESOURCES;
    }

    if (EFI_ERROR(MftahInit()) || NULL == (MftahProtocol = MftahGetInstance())) {
        EFI_DANGERLN("Failed to create a MFTAH protocol instance.");
        return EFI_OUT_OF_RESOURCES;
    }

    Context->MftahPayloadWrapper = (mftah_payload_t *)AllocateZeroPool(sizeof(mftah_payload_t));
    if (NULL == Context->MftahPayloadWrapper) {
        EFI_DANGERLN("Failed to allocate a MFTAH payload meta object.");
        return EFI_OUT_OF_RESOURCES;
    }

    MftahStatus = MftahProtocol->create_payload(MftahProtocol,
                                                (immutable_ref_t )Context->LoadedImageBase,
                                                (size_t)Context->LoadedImageSize,
                                                Context->MftahPayloadWrapper,
                                                NULL);
    if (MFTAH_ERROR(MftahStatus)) {
        EFI_DANGERLN("Failed to create a MFTAH payload meta object.");
        FreePool(Context->MftahPayloadWrapper);
        return EFI_LOAD_ERROR;
    }

    if (NULL != Context->Chain->MFTAHKey && 0 < AsciiStrLen(Context->Chain->MFTAHKey)) {
        /* NOTE: This will make the password validation on autoboots much slower, since
            the HMAC is calculated before checking the password in the MFTAH `decrypt`
            method. But ehh, not a big deal. */
        FreePool(Password);
        return EFI_SUCCESS;
    }

    /* Set the 'cursor' to the base of the allocated space. */
    p = Password;

GetPassword__Repeat:
    do {
        PassLength = (p - Password);

        DrawMftahKeyPrompt__GraphicsMode(PassLength);
        InputErrorMessage = NULL;

        SetMem(&Key, sizeof(EFI_KEY_DATA), 0x00);

        if (EFI_ERROR(ReadKey(&Key, 0))) {
            // TODO: Fix error handling mess.
            GPrint("Unknown keyboard input failure.", MftahKeyPromptBlt, 20, 20, 0xFFFF0000, 0, FALSE, 2);
            FB->RenderComponent(FB, MftahKeyPromptBlt, TRUE);
            HALT;
        }

        switch (Key.Key.UnicodeChar) {
            case CHAR_BACKSPACE:
                if (0 == PassLength) continue;

                --p;
                *p = '\0';

                continue;

            case CHAR_CARRIAGE_RETURN:
            case CHAR_LINEFEED:
                if (0 == PassLength) continue;

                goto GetPassword__EnterKey;

            default:
                if (PassLength >= MFTAH_MAX_PW_LEN) continue;

                CHAR8 EnteredKey = (CHAR8)(Key.Key.UnicodeChar & 0xFF);
                if (' ' > EnteredKey || '~' < EnteredKey) continue;   /* filter bad characters */

                *p = EnteredKey;   /* TODO: What about muh Unicode? :/ */
                ++p;

                continue;
        }
    } while (TRUE);

GetPassword__EnterKey:
    Password[MFTAH_MAX_PW_LEN] = '\0';

    /* Check the password. */
    if (1 == PassLength && 'q' == Password[0]) {
        /* 'q' entered by itself; user wants to quit. */
        Reboot(EFI_SUCCESS);
    }

    else if (MFTAH_ERROR(
            (MftahStatus = MftahProtocol->check_password(MftahProtocol,
                                                         Context->MftahPayloadWrapper,
                                                         Password,
                                                         PassLength,
                                                         MFTAH_CRYPT_HOOK_DEFAULT,
                                                         NULL))
    )) {
        /* Tell the user what went wrong. */
        switch (MftahStatus) {
            case MFTAH_INVALID_PASSWORD:
            case MFTAH_BAD_PW_HASH:
                InputErrorMessage = "Invalid password. Try again.";
                break;
            default:
                InputErrorMessage = "Yikes! Invalid protocol parameters.";
                break;
        }

        /* Re-enter the password prompt (keeping the currently-entered password). */
        goto GetPassword__Repeat;
    }

    /* Set the password pointer and exit success. */
    Context->Chain->MFTAHKey = Password;
    return EFI_SUCCESS;
}


STATIC
EFIAPI
EFI_STATUS
LoaderMftahDecrypt(IN LOADER_CONTEXT *Context)
{
    mftah_protocol_t *MftahProtocol = NULL;
    mftah_status_t MftahStatus = MFTAH_SUCCESS;

    if (
        0 == Context->LoadedImageBase
        || 0 == Context->LoadedImageSize
        || NULL == Context->MftahPayloadWrapper
    ) {
        EFI_DANGERLN("Invalid payload or base image parameters in context.");
        return EFI_BAD_BUFFER_SIZE;
    }

    if (EFI_ERROR(MftahInit()) || NULL == (MftahProtocol = MftahGetInstance())) {
        EFI_DANGERLN("Failed to create a MFTAH protocol instance.");
        return EFI_OUT_OF_RESOURCES;
    }

    MftahStatus = MftahProtocol->decrypt(MftahProtocol,
                                         Context->MftahPayloadWrapper,
                                         Context->Chain->MFTAHKey,
                                         AsciiStrLen(Context->Chain->MFTAHKey),
                                         MFTAH_CRYPT_HOOK_DEFAULT,   /* TODO Use threading? */
                                         NULL);
    if (MFTAH_ERROR(MftahStatus)) {
        /* TODO: Better reasons/error messages. */
        EFI_DANGERLN("Failed to decrypt the MFTAH payload object. Code '%u'.", MftahStatus);
        return EFI_LOAD_ERROR;
    }

    /* Now that the payload is decrypted, lop off the initial 128-byte header and adjust. */
    Context->LoadedImageBase += sizeof(mftah_payload_header_t);
    Context->LoadedImageSize -= sizeof(mftah_payload_header_t);

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
    FreePool(Context->MftahPayloadWrapper);
    FreePool(Context);

    BltDestroy(ProgressBlt);
    BltDestroy(LoadingIconUnderlayBlt);
    BltDestroy(MftahKeyPromptBlt);

    /* Black out the screen one last time. */
    FB->ClearScreen(FB, 0); FB->Flush(FB);

    FramebufferDestroy();
    MftahDestroy();
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

    /* Just steal some colors from the palette. */
    ProgressBarColors = c->Colors.Title;
    TextColors = c->Colors.Text;
    IsQuick = c->Quick;

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

            Status = NewObjectBlt(ProgressBlt->Position.X,
                                  ProgressBlt->Position.Y,
                                  ProgressBlt->Dimensions.Width,
                                  ProgressBlt->Dimensions.Height,
                                  1,
                                  &MftahKeyPromptBlt);
            if (EFI_ERROR(Status)) {
                LOADER_PANIC("Failed to allocate a BLT for the MFTAH key prompt.");
            }
            FB->ClearBlt(FB, MftahKeyPromptBlt, &p);

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

    /* Set up the context object and send it to the right chain. */
    LOADER_CONTEXT *Context = (LOADER_CONTEXT *)AllocateZeroPool(sizeof(LOADER_CONTEXT));
    if (NULL == Context) {
        Status = EFI_OUT_OF_RESOURCES;
        LOADER_PANIC("Failed to allocate loader context: out of resources!");
    }

    Context->Chain = chain;
    Context->ProgressFunc = ProgressFunc;
    Context->StallFunc = StallFunc;

    /* Read the payload file from block storage. */
    if (EFI_ERROR((Status = LoaderReadImage(Context)))) {
        LOADER_PANIC("Failed to read the target payload into memory.");
    }

    /* Check the chain's properties. This occurs in a certain order. For example,
        MFTAH is always the OUTERMOST layer when compared to compression, because
        compressing AES-256 data is rather useless. */
    if (TRUE == chain->IsMFTAH) {
        /* Clear the screen. */
        FB->ClearScreen(FB, 0); FB->Flush(FB);

        /* Prompt for a password if necessary. This also creates the MFTAH
            payload wrapper object. */
        if (EFI_ERROR((Status = LoaderGetAndValidateMftahKey(Context)))) {
            LOADER_PANIC("Fatal exception while capturing MFTAH key.");
        }

        /* Clear the screen. */
        FB->ClearScreen(FB, 0); FB->Flush(FB);
        ProgressStatusMessage = "Decrypting...";

        if (EFI_ERROR((Status = LoaderMftahDecrypt(Context)))) {
            LOADER_PANIC("MFTAH decryption encountered a fatal exception.");
        }
    }

    if (TRUE == chain->IsCompressed) {
        // TODO: Compression compatibility.
        /* Clear the screen. */
        FB->ClearScreen(FB, 0); FB->Flush(FB);
        if (EFI_ERROR((Status = LoaderDecompress(Context)))) {
            LOADER_PANIC("Decompression returned an irrecoverable failure code.");
        }
    }

    /* Clear the screen. */
    // TODO Compatibility with both modes please.
    FB->ClearScreen(FB, 0);
    ProgressStatusMessage = "Chainloading...";
    GPrint(ProgressStatusMessage,
           FB->BLT,
           (FB->Resolution.Width / 2)
               - (((AsciiStrLen(ProgressStatusMessage) * 3 * FB->BaseGlyphSize.Width)) / 2),
           (FB->Resolution.Height / 2) - ((3 * FB->BaseGlyphSize.Height) / 2),
           0xFFFFFFFF,
           0x00000000,
           FALSE,
           3);
    FB->Flush(FB);

    switch (chain->Type) {
        case DISK:  DiskLoader .Load(Context); break;
        case EXE:   ExeLoader  .Load(Context); break;
        case ELF:   ElfLoader  .Load(Context); break;
        case BIN:   BinLoader  .Load(Context); break;
        default: break;
    }

    /* End of the road. */
    Status = EFI_NOT_STARTED;
    LOADER_PANIC("Could not transfer control to the right loader.");
}
