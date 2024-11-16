#include "../../include/drivers/displays.h"

#include "../../include/core/util.h"



typedef
struct {
    UINTN       Columns;
    UINTN       Rows;
    UINTN       PreviousSelection;
    CHAR16      *BlankRow;
} NATIVE_CONTEXT;



/* Same output protocol as TEXT mode, but NATIVE's use is much simpler. */
STATIC EFI_SIMPLE_TEXT_OUT_PROTOCOL *STOP = NULL;

STATIC NATIVE_CONTEXT *NativeContext = NULL;



/* Forward declarations. */
STATIC EFI_STATUS NativeInit(
    IN SIMPLE_DISPLAY *This,
    IN CONFIGURATION *Configuration
);

STATIC EFI_STATUS NativeDestroy(
    IN CONST SIMPLE_DISPLAY *This
);

STATIC VOID NativeClearScreen(
    IN CONST SIMPLE_DISPLAY *This,
    IN UINT32 Color
);

STATIC VOID NativePanic(
    IN CONST SIMPLE_DISPLAY *This,
    IN CHAR8 *Message,
    IN EFI_STATUS Status,
    IN BOOLEAN IsShutdown,
    IN UINTN ShutdownTimer
);

STATIC VOID NativePrintProgress(
    IN CONST SIMPLE_DISPLAY *This,                      
    IN CONST CHAR8 *Message,                      
    IN UINTN Current,                      
    IN UINTN OutOfTotal
);

STATIC VOID NativeStall(
    IN CONST SIMPLE_DISPLAY *This,
    IN UINTN TimeInMilliseconds
);

STATIC VOID NativeInputPopup(
    IN CONST SIMPLE_DISPLAY *This,
    IN CHAR8 *Prompt,
    IN CHAR8 *CurrentInput,
    IN BOOLEAN IsHidden,
    IN CHAR8 *ErrorMessage OPTIONAL
);

/* Construct the static display protocol instance. */
SIMPLE_DISPLAY NUI = {
    .Initialize     = NativeInit,
    .Destroy        = NativeDestroy,
    .ClearScreen    = NativeClearScreen,
    .Panic          = NativePanic,
    .Progress       = NativePrintProgress,
    .Stall          = NativeStall,
    .InputPopup     = NativeInputPopup,

    .MENU           = NULL
};



STATIC
VOID
DrawMenu(IN MENU_STATE *m)
{
    if (NULL == m) return;

    if (m->CurrentItemIndex == NativeContext->PreviousSelection) return;

    NUI.ClearScreen(&NUI, CONFIG->Colors.Background);

    /* First, print the banner and the title. */
    /* NOTE: No special banner/title colors. Native mode is too basic to care. */
    PRINT("%a\r\n%a\r\n", CONFIG->Banner, CONFIG->Title);

    /* For each menu item, print the index and the chain name. */
    for (UINTN i = 0; i < m->ItemsListLength; ++i) {
        PRINTLN("   %02u: %a", i, CONFIG->Chains[i]->Name);
    }

    PRINT("\r\nSelected   >>> %02u\r\n", m->CurrentItemIndex);

    CHAR8 *ChainInfo = NULL;
    ConfigDumpChain(CONFIG->Chains[m->CurrentItemIndex], &ChainInfo);

    PRINTLN("%a", ChainInfo);

    FreePool(ChainInfo);
}


STATIC
VOID
TimerTick(IN EFI_EVENT Event,
          IN VOID *Context)
{
    MENU_STATE *m = (MENU_STATE *)Context;

    CHAR8 *NormalTimeoutText = (CHAR8 *)AllocateZeroPool(128);
    CHAR8 *MaxTimeoutText = (CHAR8 *)AllocateZeroPool(128);

    if (
        FALSE == m->KeyPressReceived
        && CONFIG->Timeout > 0
        && m->MillisecondsElapsed >= CONFIG->Timeout
    ) {
        BS->CloseEvent(Event);

        STOP->SetAttribute(STOP, MFTAH_COLOR_WARNING);
        VARPRINTLN8(DefaultTimeoutStr);
        BS->Stall(EFI_SECONDS_TO_MICROSECONDS(3));

        m->TimeoutOccurred = TRUE;
        goto TimerTick__Exit;
    }

    if (
        m->MillisecondsElapsed > CONFIG->MaxTimeout
        && CONFIG->MaxTimeout > 0
    ) {
        BS->CloseEvent(Event);

        STOP->SetAttribute(STOP, MFTAH_COLOR_PANIC);
        VARPRINTLN8(MaxTimeoutExceededtStr);
        BS->Stall(EFI_SECONDS_TO_MICROSECONDS(3));

        Shutdown(EFI_TIMEOUT);
        HALT;
    }

    /* NOTE: The timer is never rendered in NATIVE mode. */

TimerTick__Exit:
    FreePool(NormalTimeoutText);
    FreePool(MaxTimeoutText);

    /* Keep track of how many seconds have elapsed. */
    m->MillisecondsElapsed += 100;
}


STATIC
EFI_STATUS
NativeInit(IN SIMPLE_DISPLAY *This,
           IN CONFIGURATION *Configuration)
{
    if (NULL == This || NULL == Configuration) {
        return EFI_INVALID_PARAMETER;
    }

    EFI_STATUS Status = EFI_SUCCESS;
    UINTN ModeColumns = 0, ModeRows = 0;

    STOP = ST->ConOut;
    if (NULL == STOP) {
        return EFI_NOT_STARTED;
    }

    NativeContext = (NATIVE_CONTEXT *)AllocateZeroPool(sizeof(NATIVE_CONTEXT));
    if (NULL == NativeContext) {
        EFI_DANGERLN("FATAL: STOP:  Out of resources while allocating context.");
        return EFI_OUT_OF_RESOURCES;
    }

    /* We ONLY query the mode here to get the columns. If there's an error,
        be quiet about it and guess the traditional 80 columns. */
    Status = STOP->QueryMode(STOP,
                             STOP->Mode->Mode,
                             &NativeContext->Columns,
                             &NativeContext->Rows);

    NativeContext->Columns = (NativeContext->Columns ? NativeContext->Columns : 80);
    NativeContext->Rows    = (NativeContext->Rows    ? NativeContext->Rows    : 80);

    /* NOTE: Pretty much 1-to-1 copy-paste of TEXT mode follows below. */

    /* Set up the blank row string. This is used to ensure a clear-screen
        does so correctly and fully. */
    CHAR8 *BlankRow = (CHAR8 *)AllocateZeroPool(sizeof(CHAR8) * (ModeColumns + 1));
    if (NULL == BlankRow) {
        EFI_DANGERLN("FATAL: STOP:  Out of resources while allocating blank row text.");
        return EFI_OUT_OF_RESOURCES;
    }

    SetMem(BlankRow, ModeColumns, ' ');
    NativeContext->BlankRow = AsciiStrToUnicode(BlankRow);
    FreePool(BlankRow);

    This->ClearScreen(This, Configuration->Colors.Background);

    /* Set the 'normal' console color. */
    STOP->SetAttribute(STOP, CONFIG_TEXT_COLOR(Text));

    EFI_MENU_RENDERER_PROTOCOL *Renderer = (EFI_MENU_RENDERER_PROTOCOL *)
        AllocateZeroPool(sizeof(EFI_MENU_RENDERER_PROTOCOL));
    if (NULL == Renderer) {
        EFI_DANGERLN("FATAL: STOP:  Out of resources while allocating menu renderer.");
        return EFI_OUT_OF_RESOURCES;
    }

    Renderer->Redraw    = DrawMenu;
    Renderer->Tick      = TimerTick;
    This->MENU = Renderer;

    /* Force the menu to render the first time, even if the selection is 0. */
    NativeContext->PreviousSelection = -1U;

    return EFI_SUCCESS;
}


STATIC
EFI_STATUS
NativeDestroy(IN CONST SIMPLE_DISPLAY *This)
{
    /* TEXT mode is much simpler: it doesn't have entire pixel buffers to clean. */
    FreePool(NativeContext->BlankRow);
    FreePool(NativeContext);

    FreePool(This->MENU);

    return EFI_SUCCESS;
}


STATIC
VOID
NativeClearScreen(IN CONST SIMPLE_DISPLAY *This,
                  IN UINT32 Color)
{
    if (
        NULL == STOP
        || NULL == This
        || NULL == NativeContext
    ) return;

    /* Both clear the screen AND print an entire screen's worth of new-lines.
        This is because ClearScreen sometimes doesn't preserve background colors. */
    STOP->SetAttribute(STOP, (UINT8)Color);
    STOP->ClearScreen(STOP);

    STOP->SetAttribute(STOP, (UINT8)Color);

    /* By manually setting the cursor to the origin and having prints overwrite
        the current 'display', we avoid slowness caused by emulated terminal scrolling. */
    if (NULL != NativeContext->BlankRow) {
        STOP->SetCursorPosition(STOP, 0, 0);
        for (UINTN i = 0; i <= NativeContext->Rows; ++i) {
            STOP->OutputString(STOP, NativeContext->BlankRow);
        }
    }

    /* Always reset the curent color to the Text colors. */
    STOP->SetAttribute(STOP, CONFIG_TEXT_COLOR(Text));
}


STATIC
VOID
NativePanic(IN CONST SIMPLE_DISPLAY *This,
            IN CHAR8 *Message,
            IN EFI_STATUS Status,
            IN BOOLEAN IsShutdown,
            IN UINTN ShutdownTimer)
{
    // TODO: This code now appears in all 3 modes. Centralize and refactor to `displays`.
    STOP->SetAttribute(STOP, MFTAH_COLOR_PANIC);
    UINTN MessageLength = AsciiStrLen(Message),
          PanicPrefixLength = AsciiStrLen(PanicPrefix),
          ErrorStringLength = 0;

    SetMem(ErrorStringBuffer, ERROR_STRING_BUFFER_SIZE, 0x00);
    StatusToString(ErrorStringBuffer, Status);
    ErrorStringBuffer[ERROR_STRING_BUFFER_SIZE - 1] = L'\0';
    ErrorStringLength = StrLen(ErrorStringBuffer);

    CHAR8 *ErrorStr = UnicodeStrToAscii(ErrorStringBuffer);
    CHAR8 *Shadow = (CHAR8 *)
        AllocateZeroPool(sizeof(CHAR8) * (PanicPrefixLength + MessageLength + ErrorStringLength + 2));

    if (NULL == Shadow || NULL == ErrorStr) {
        STOP->SetAttribute(STOP, MFTAH_COLOR_PANIC);
        VARPRINTLN8(Message);
    } else {
        CopyMem(Shadow, PanicPrefix, PanicPrefixLength);
        CopyMem((VOID *)&Shadow[PanicPrefixLength], Message, MessageLength);
        Shadow[PanicPrefixLength + MessageLength] = ' ';
        CopyMem((VOID *)&Shadow[PanicPrefixLength + MessageLength + 1], ErrorStr, ErrorStringLength);

        STOP->SetAttribute(STOP, MFTAH_COLOR_PANIC);
        VARPRINTLN8(Shadow);

        FreePool(Shadow);
        FreePool(ErrorStr);
    }

    if (0 == ShutdownTimer) {
        BS->Stall(EFI_SECONDS_TO_MICROSECONDS(3));
    } else {
        BS->Stall(ShutdownTimer);
    }

    if (TRUE == IsShutdown) {
        Shutdown(EFI_SUCCESS);
    }
}


STATIC
VOID
NativePrintProgress(IN CONST SIMPLE_DISPLAY *This,                      
                    IN CONST CHAR8 *Message,                      
                    IN UINTN Current,                      
                    IN UINTN OutOfTotal)
{
    if (NULL == This) return;

    UINT8 PercentDone, PercentByTen;
    double PercentRaw;

    /* Printing a progress bar should require a total of over 100.
        This is arbitrary but it keeps nonsense progress updates that
        look good in other modes from looking like total trash here. */
    if (OutOfTotal > 100) {
        PercentRaw = (double)(Current) / (double)(OutOfTotal);
        PercentDone = MIN(100, (int)(PercentRaw * 100.0f));
        PercentByTen = MIN(10, (PercentDone / 10));

        PRINT("\r %3d%% [", PercentDone);
        for (int i = 0; i < PercentByTen; ++i) PRINT("=");
        for (int i = 0; i < (10 - PercentByTen); ++i) PRINT(" ");
        PRINT("] (%8x / %8x) ", Current, OutOfTotal);
    }

    if (NULL != Message) VARPRINT8(Message);

    /* Usually, the '100% done' progress update comes through once when
        it's asserted at the end of an operation. This is a good time to
        go to the next line so we don't end up with funky run-over messages
        from one operation's `Message` to another's that comes directly
        afterwards. */
    if (Current == OutOfTotal) PRINT("\r\n");
}


// TODO: Move this common flip function out and into `displays`.
STATIC
VOID
FlipToFalse(EFI_EVENT Event,
            VOID *Context)
{ BOOLEAN *b = (BOOLEAN *)Context; if (NULL != b) *b = FALSE; }

STATIC
VOID
NativeStall(IN CONST SIMPLE_DISPLAY *This,
            IN UINTN TimeInMilliseconds)
{
    EFI_STATUS Status = EFI_SUCCESS;
    VOLATILE BOOLEAN Stall = TRUE;
    EFI_EVENT StallEvent = {0};

    if (TRUE == CONFIG->Quick) return;

    BS->CreateEvent((EVT_TIMER | EVT_NOTIFY_SIGNAL),
                    TPL_NOTIFY,
                    FlipToFalse,
                    (VOID *)&Stall,
                    &StallEvent);

    BS->SetTimer(StallEvent,
                 TimerPeriodic,
                 (10 * 1000 * TimeInMilliseconds));

    /* While the condition is true, write out a spinny thingy on a new line. */
    UINTN Step = 0;
    PRINT("\r\n");

    while (TRUE == Stall) {
        PRINT("\r  ");

        switch (Step % 3) {
            case 0: PRINT("/");     break;
            case 1: PRINT("-");     break;
            case 2: PRINT("\\");    break;
            case 3: PRINT("|");     break;
        }

        BS->Stall(EFI_SECONDS_TO_MICROSECONDS(0.2));
        ++Step;
    }

    PRINT("\r\n");
    BS->CloseEvent(StallEvent);
}


STATIC
VOID
NativeInputPopup(IN CONST SIMPLE_DISPLAY *This,
                 IN CHAR8 *Prompt,
                 IN CHAR8 *CurrentInput,
                 IN BOOLEAN IsHidden,
                 IN CHAR8 *ErrorMessage OPTIONAL)
{
    if (
        NULL == This
        || NULL == Prompt
        || NULL == CurrentInput
    ) return;

    UINTN InputLength = AsciiStrLen(CurrentInput);
    STOP->SetAttribute(STOP, CONFIG_TEXT_COLOR(Text));

    // TODO: This is not clearing the asterisks from the previous password input. Fix.
    PRINT("\r");
    for (UINTN i = 0; i < (NativeContext->Rows - 1); ++i) { PRINT(" "); }
    PRINT("\r");

    /* Print the error message if it's set. This uses static colors. */
    if (NULL != ErrorMessage) {
        STOP->SetAttribute(STOP, MFTAH_COLOR_PANIC);
        VARPRINT8(ErrorMessage);
        STOP->SetAttribute(STOP, CONFIG_TEXT_COLOR(Text));
        PRINT("\r\n\r\n");
    }

    VARPRINT8(Prompt); PRINT(" ");

    if (TRUE == IsHidden) {
        for (UINTN i = 0; i < MFTAH_MAX_PW_LEN; ++i) {
            if (i < InputLength) {
                PRINT("*");
            } else {
                PRINT(" ");
            }
        }
    } else {
        VARPRINT8(CurrentInput);
        for (UINTN i = (MFTAH_MAX_PW_LEN - InputLength); i > 0; --i) {
            PRINT(" ");
        }
    }
}
