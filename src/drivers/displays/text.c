#include "../../include/drivers/displays.h"

#include "../../include/core/util.h"



typedef
struct {
    BOOLEAN     InputPopupShowedError;
    BOOLEAN     InitialMenuRendered;
    UINTN       PreviousSelectedIndex;
    CHAR8       *PreviousProgressMessage;
    UINTN       PreviousProgressMessageLength;
    CHAR16      *BlankRow;
} TEXT_CONTEXT;



STATIC EFI_SIMPLE_TEXT_OUT_PROTOCOL *STOP = NULL;

STATIC TEXT_CONTEXT *TextContext = NULL;

STATIC UINTN ModeColumns = 0;
STATIC UINTN ModeRows = 0;

STATIC CHAR16 BoxShadow[2] = { BLOCKELEMENT_FULL_BLOCK, 0 };



/* Forward declarations. */
STATIC EFI_STATUS TextInit(
    IN SIMPLE_DISPLAY *This,
    IN CONFIGURATION *Configuration
);

STATIC EFI_STATUS TextDestroy(
    IN CONST SIMPLE_DISPLAY *This
);

STATIC VOID TextClearScreen(
    IN CONST SIMPLE_DISPLAY *This,
    IN UINT32 Color
);

STATIC VOID TextPanic(
    IN CONST SIMPLE_DISPLAY *This,
    IN CHAR8 *Message,
    IN EFI_STATUS Status,
    IN BOOLEAN IsShutdown,
    IN UINTN ShutdownTimer
);

STATIC VOID TextPrintProgress(
    IN CONST SIMPLE_DISPLAY *This,                      
    IN CONST CHAR8 *Message,                      
    IN UINTN Current,                      
    IN UINTN OutOfTotal
);

STATIC VOID TextStall(
    IN CONST SIMPLE_DISPLAY *This,
    IN UINTN TimeInMilliseconds
);

STATIC VOID TextInputPopup(
    IN CONST SIMPLE_DISPLAY *This,
    IN CHAR8 *Prompt,
    IN CHAR8 *CurrentInput,
    IN BOOLEAN IsHidden,
    IN CHAR8 *ErrorMessage OPTIONAL
);

/* Construct the static display protocol instance. */
SIMPLE_DISPLAY TUI = {
    .Initialize     = TextInit,
    .Destroy        = TextDestroy,
    .ClearScreen    = TextClearScreen,
    .Panic          = TextPanic,
    .Progress       = TextPrintProgress,
    .Stall          = TextStall,
    .InputPopup     = TextInputPopup,

    .MENU           = NULL
};


STATIC VOID TPrint(
    IN CHAR8 *Str,
    IN UINT8 Column,
    IN UINT8 Row,
    IN UINT8 Color,
    IN BOOLEAN Wrap
);

STATIC EFI_STATUS LoadingAnimationLoop(
    IN UINT8 AtColumn,
    IN UINT8 AtRow,
    IN UINT8 Width,
    IN UINT8 Height,
    IN VOLATILE BOOLEAN *Stall
);

STATIC VOID Rect(
    IN UINTN StartColumn,
    IN UINTN StartRow,
    IN UINTN BoxWidth,
    IN UINTN BoxHeight,
    IN UINT8 Color,
    IN CHAR16 BorderChar,
    IN BOOLEAN HasShadow
);



/* LAYOUT SPECIFICATIONS. */
//#define



STATIC INLINE
UINT8
InvertColor(IN UINT8 Color)
{
    return 0x7F & (((Color & 0x0F) << 4) | ((Color & 0xF0) >> 4));
}



STATIC
VOID
DrawMenu(IN MENU_STATE *m)
{
    if (NULL == m) return;

    /* Draw the left "panel". Remember, the assumed minimum resolution is 80x25. */
    UINTN PanelVertMargin = (ModeColumns <= 80) ? 2 : MAX(1, (ModeColumns / 16));
    UINTN PanelTopMargin = (ModeRows <= 25) ? 2 : MAX(1, (ModeRows / 32));
    UINTN PanelWidth = MAX(36, (ModeColumns / 2) - (2 * PanelVertMargin));

    UINTN LeftPanelHeight = (4 + CONFIG_MAX_CHAINS + 2);   /* 4 (header), 2 (bottom pad) */
    UINTN LeftPanelStartRow = PanelTopMargin;
    UINTN LeftPanelStartColumn = PanelVertMargin;

    UINTN RightPanelHeight = 8;
    UINTN RightPanelSpacing = 3;
    UINTN RightPanelStartRow = PanelTopMargin;
    UINTN RightPanelStartColumn = (ModeColumns / 2) + PanelVertMargin;
    UINTN BannerPanelHeight = 5;

    if (TRUE == TextContext->InitialMenuRendered) {
        goto DrawMenu__OnlyMenuUpdate;
    }

    /* Draw the left panel rect. */
    Rect(LeftPanelStartColumn,
         LeftPanelStartRow,
         PanelWidth,
         LeftPanelHeight,
         CONFIG->Colors.Border,
         0,
         TRUE);

    /* Write in the title and draw an inner unshadowed rect for the chains. */
    TPrint(CONFIG->Title,
           LeftPanelStartColumn + ((PanelWidth / 2) - (AsciiStrLen(CONFIG->Title) / 2)),
           LeftPanelStartRow + 1,
           CONFIG_TEXT_COLOR(Title),
           FALSE);

    Rect(LeftPanelStartColumn + 1,
         LeftPanelStartRow + 4,   /* account for the header */
         PanelWidth - 2,
         CONFIG_MAX_CHAINS,
         CONFIG_TEXT_COLOR(Text),
         0,
         FALSE);

    /* Print each chain's `name` field. */
    for (UINTN i = 0; i < CONFIG->ChainsLength; ++i) {
        UINTN ChainNameLength = AsciiStrLen(CONFIG->Chains[i]->Name);

        CHAR8 *ChainNameCopy = (CHAR8 *)AllocateZeroPool(sizeof(CHAR8) * (ChainNameLength + 1));
        if (NULL == ChainNameCopy) {
            TUI.Panic(&TUI, "Out of memory!", EFI_OUT_OF_RESOURCES, TRUE, EFI_SECONDS_TO_MICROSECONDS(10));
        }
        CopyMem(ChainNameCopy, CONFIG->Chains[i]->Name, ChainNameLength);

        /* If the name of the chain is too long, cut it off so it doesn't bleed over. */
        if (ChainNameLength > (PanelWidth - 2 - 1)) {
            ChainNameCopy[(PanelWidth - 2 - 1)] = '\0';
        }

        TPrint(ChainNameCopy,
               LeftPanelStartColumn + 2,
               LeftPanelStartRow + 4 + i,
               CONFIG_TEXT_COLOR(Text),
               FALSE);

        FreePool(ChainNameCopy);
    }

    /* Banner field and message. */
    UINTN BannerStartRow = (RightPanelStartRow + RightPanelHeight + RightPanelSpacing);
    Rect(RightPanelStartColumn,
         BannerStartRow,
         PanelWidth,
         BannerPanelHeight,
         CONFIG_TEXT_COLOR(Banner),
         L'#',
         (0 != (0xF0 & CONFIG_TEXT_COLOR(Banner))));

    if (NULL != CONFIG->Banner) {
        CHAR16 BannerChar[2] = {0};
        UINTN BannerX = (RightPanelStartColumn + 2);
        UINTN BannerY = (BannerStartRow + 1);

        STOP->SetAttribute(STOP, CONFIG_TEXT_COLOR(Banner));

        for (CHAR8 *p = CONFIG->Banner; *p; ++p) {
            /* Skip unprintable characters. */
            if ('\n' != *p && (*p < ' ' || *p > '~')) continue;

            STOP->SetCursorPosition(STOP, BannerX, BannerY);

            switch (*p) {
                default:
                    if (BannerX < (RightPanelStartColumn + PanelWidth - 2)) {
                        *((CHAR8 *)BannerChar) = *p;
                        STOP->OutputString(STOP, BannerChar);
                    }

                    ++BannerX;
                    break;

                case '\n':
                    BannerX = (RightPanelStartColumn + 2);
                    ++BannerY;

                    if (BannerY >= (BannerStartRow + BannerPanelHeight)) {
                        goto DrawMenu__FinishBanner;
                    }

                    break;
            }
        }
    }

DrawMenu__FinishBanner:
    /* Timeouts should only be rendered here if none are set. */
    if (0 == CONFIG->Timeout && 0 == CONFIG->MaxTimeout) {
        TPrint(NoTimeoutStr,
               RightPanelStartColumn,
               PanelTopMargin + RightPanelHeight
                + BannerPanelHeight + (RightPanelSpacing * 2),
               CONFIG_TEXT_COLOR(Timer),
               FALSE);
    }

    TextContext->InitialMenuRendered = TRUE;

    /* Purposefully set this out of bounds so this fall-through won't have a valid previous. */
    TextContext->PreviousSelectedIndex = CONFIG_MAX_CHAINS + 1;

DrawMenu__OnlyMenuUpdate:
    /* When this is only an update (the user is just moving around), only update the
        previous entry with the baseline color and set the newly selected entry to the
        negated color. */

    /* Don't waste our time rendering anything if no selection was changed.. */
    if (TextContext->PreviousSelectedIndex == m->CurrentItemIndex) return;

    UINTN NameLength = 0;
    CHAR8 *NameCopy = NULL;

    /* Always redraw the right panel. */
    Rect(RightPanelStartColumn,
         RightPanelStartRow,
         PanelWidth,
         RightPanelHeight,
         CONFIG_TEXT_COLOR(Text),
         L'%',
         (0 != (0xF0 & CONFIG_TEXT_COLOR(Text))));

    if (m->CurrentItemIndex < CONFIG_MAX_CHAINS) {
        NameLength = AsciiStrLen(CONFIG->Chains[m->CurrentItemIndex]->Name);
        NameCopy = (CHAR8 *)AllocateZeroPool(sizeof(CHAR8) * (NameLength + 1));
        if (NULL == NameCopy) {
            TUI.Panic(&TUI, "Out of memory!", EFI_OUT_OF_RESOURCES, TRUE, EFI_SECONDS_TO_MICROSECONDS(10));
        }
        CopyMem(NameCopy, CONFIG->Chains[m->CurrentItemIndex]->Name, NameLength);

        /* If the name of the chain is too long, cut it off so it doesn't bleed over. */
        if (NameLength > (PanelWidth - 2 - 1)) {
            NameCopy[(PanelWidth - 2 - 1)] = '\0';
        }

        Rect(LeftPanelStartColumn + 1,
             LeftPanelStartRow + 4 + m->CurrentItemIndex,
             PanelWidth - 2,
             1,
             InvertColor(CONFIG_TEXT_COLOR(Text)),
             0,
             FALSE);

        TPrint(CONFIG->Chains[m->CurrentItemIndex]->Name,
               LeftPanelStartColumn + 2,
               LeftPanelStartRow + 4 + m->CurrentItemIndex,
               InvertColor(CONFIG_TEXT_COLOR(Text)),
               FALSE);
        
        FreePool(NameCopy);

        /* Update right panel text. */
        // TODO: generate these during config parsing and attach them to the chains themselves as a property
        // This is otherwise annoyingly expensive
        CHAR8 *ChainInfo = NULL;
        ConfigDumpChain(CONFIG->Chains[m->CurrentItemIndex], &ChainInfo);

        if (NULL != ChainInfo) {
            // TODO This is a copy of the above with some adjustments. Move to function.
            CHAR16 BannerChar[2] = {0};
            UINTN BannerX = (RightPanelStartColumn + 2);
            UINTN BannerY = (RightPanelStartRow + 1);

            STOP->SetAttribute(STOP, CONFIG_TEXT_COLOR(Text));

            for (CHAR8 *p = ChainInfo; *p; ++p) {
                /* Skip unprintable characters. */
                if ('\n' != *p && (*p < ' ' || *p > '~')) continue;

                STOP->SetCursorPosition(STOP, BannerX, BannerY);

                switch (*p) {
                    default:
                        if (BannerX < (RightPanelStartColumn + PanelWidth - 2)) {
                            *((CHAR8 *)BannerChar) = *p;
                            STOP->OutputString(STOP, BannerChar);
                        }

                        ++BannerX;
                        break;

                    case '\n':
                        BannerX = (RightPanelStartColumn + 2);
                        ++BannerY;

                        if (BannerY >= (RightPanelStartRow + RightPanelHeight - 1)) {
                            goto DrawMenu__FinishBanner;
                        }

                        break;
                }
            }
        }

        FreePool(ChainInfo);
    }

    /* Restore the previous entry. */
    if (TextContext->PreviousSelectedIndex < CONFIG_MAX_CHAINS) {
        NameLength = AsciiStrLen(CONFIG->Chains[TextContext->PreviousSelectedIndex]->Name);
        NameCopy = (CHAR8 *)AllocateZeroPool(sizeof(CHAR8) * (NameLength + 1));
        if (NULL == NameCopy) {
            TUI.Panic(&TUI, "Out of memory!", EFI_OUT_OF_RESOURCES, TRUE, EFI_SECONDS_TO_MICROSECONDS(10));
        }
        CopyMem(NameCopy, CONFIG->Chains[TextContext->PreviousSelectedIndex]->Name, NameLength);

        /* If the name of the chain is too long, cut it off so it doesn't bleed over. */
        if (NameLength > (PanelWidth - 2 - 1)) {
            NameCopy[(PanelWidth - 2 - 1)] = '\0';
        }

        Rect(LeftPanelStartColumn + 1,
             LeftPanelStartRow + 4 + TextContext->PreviousSelectedIndex,
             PanelWidth - 2,
             1,
             CONFIG_TEXT_COLOR(Text),
             0,
             FALSE);

        TPrint(CONFIG->Chains[TextContext->PreviousSelectedIndex]->Name,
               LeftPanelStartColumn + 2,
               LeftPanelStartRow + 4 + TextContext->PreviousSelectedIndex,
               CONFIG_TEXT_COLOR(Text),
               FALSE);
        
        FreePool(NameCopy);
    }

    /* Update the previous index. */
    TextContext->PreviousSelectedIndex = m->CurrentItemIndex;

    return;
}


STATIC
VOID
TimerTick(IN EFI_EVENT Event,
          IN VOID *Context)
{
    MENU_STATE *m = (MENU_STATE *)Context;

    UINTN PanelVertMargin = (ModeColumns <= 80) ? 2 : MAX(1, (ModeColumns / 16));
    UINTN PanelTopMargin = (ModeRows <= 25) ? 2 : MAX(1, (ModeRows / 32));
    UINTN PanelWidth = MAX(36, (ModeColumns / 2) - (2 * PanelVertMargin));
    UINTN RightPanelHeight = 8;
    UINTN RightPanelSpacing = 3;
    UINTN RightPanelStartColumn = (ModeColumns / 2) + PanelVertMargin;
    UINTN BannerPanelHeight = 5;

    UINTN TimerTextStartRow =
        (PanelTopMargin + RightPanelHeight + BannerPanelHeight + (2 * RightPanelSpacing));
    UINTN TimerTextStartColumn = RightPanelStartColumn;

    CHAR8 *NormalTimeoutText = (CHAR8 *)AllocateZeroPool(128);
    CHAR8 *MaxTimeoutText = (CHAR8 *)AllocateZeroPool(128);

    if (
        FALSE == m->KeyPressReceived
        && CONFIG->Timeout > 0
        && m->MillisecondsElapsed >= CONFIG->Timeout
    ) {
        /* A normal timeout has occurred. Signal to the menu handler. */
        BS->CloseEvent(Event);

        /* Clear the current timeouts section. */
        STOP->SetAttribute(STOP, CONFIG_TEXT_COLOR(Timer));
        for (UINTN i = 0; i < PanelWidth; ++i) {
            STOP->SetCursorPosition(STOP, (TimerTextStartColumn + i), TimerTextStartRow);
            STOP->OutputString(STOP, L" ");
            STOP->SetCursorPosition(STOP, (TimerTextStartColumn + i), (TimerTextStartRow + 1));
            STOP->OutputString(STOP, L" ");
        }

        TPrint(DefaultTimeoutStr,
               TimerTextStartColumn,
               TimerTextStartRow,
               MFTAH_COLOR_PANIC,
               FALSE);
        BS->Stall(2000000);

        m->TimeoutOccurred = TRUE;
        goto TimerTick__Exit;
    }

    if (
        m->MillisecondsElapsed > CONFIG->MaxTimeout
        && CONFIG->MaxTimeout > 0
    ) {
        BS->CloseEvent(Event);

        /* Clear the current timeouts section. */
        STOP->SetAttribute(STOP, CONFIG_TEXT_COLOR(Timer));
        for (UINTN i = 0; i < PanelWidth; ++i) {
            STOP->SetCursorPosition(STOP, (TimerTextStartColumn + i), TimerTextStartRow);
            STOP->OutputString(STOP, L" ");
            STOP->SetCursorPosition(STOP, (TimerTextStartColumn + i), (TimerTextStartRow + 1));
            STOP->OutputString(STOP, L" ");
        }

        TPrint(MaxTimeoutExceededtStr,
               TimerTextStartColumn,
               TimerTextStartRow,
               MFTAH_COLOR_PANIC,
               FALSE);
        BS->Stall(2000000);

        Shutdown(EFI_TIMEOUT);
        HALT;
    }

    if (
        0 == (m->MillisecondsElapsed % 1000)
        && FALSE == m->PauseTickRenders
    ) {
        BOOLEAN NormalTimeoutActive = (
            m->MillisecondsElapsed < CONFIG->Timeout
            && FALSE == m->KeyPressReceived
            && CONFIG->Timeout > 0
        );
        BOOLEAN MaxTimeoutActive = (
            CONFIG->MaxTimeout > 0
            && m->MillisecondsElapsed < CONFIG->MaxTimeout
        );

        /* Clear the current timeouts section. */
        UINTN SavedAttribute = STOP->Mode->Attribute;
        STOP->SetAttribute(STOP, CONFIG_TEXT_COLOR(Timer));
        for (UINTN i = 0; i < PanelWidth; ++i) {
            STOP->SetCursorPosition(STOP, (TimerTextStartColumn + i), TimerTextStartRow);
            STOP->OutputString(STOP, L" ");
            STOP->SetCursorPosition(STOP, (TimerTextStartColumn + i), (TimerTextStartRow + 1));
            STOP->OutputString(STOP, L" ");
        }

        if (NormalTimeoutActive) {
            AsciiSPrint(NormalTimeoutText,
                        128,
                        NormalTimeoutStr,
                        (CONFIG->Timeout - m->MillisecondsElapsed) / 1000);

            CHAR16 *DupedNormal = AsciiStrToUnicode(NormalTimeoutText);
            STOP->SetCursorPosition(STOP, TimerTextStartColumn, TimerTextStartRow);
            STOP->OutputString(STOP, DupedNormal);
            FreePool(DupedNormal);
        }

        if (MaxTimeoutActive) {
            AsciiSPrint(MaxTimeoutText,
                        128,
                        MaxTimeoutStr,
                        (CONFIG->MaxTimeout - m->MillisecondsElapsed) / 1000);

            CHAR16 *DupedMax = AsciiStrToUnicode(MaxTimeoutText);
            STOP->SetCursorPosition(STOP, TimerTextStartColumn, (TimerTextStartRow + 1));
            STOP->OutputString(STOP, DupedMax);
            FreePool(DupedMax);
        }

        STOP->SetAttribute(STOP, SavedAttribute);
    }

TimerTick__Exit:
    FreePool(NormalTimeoutText);
    FreePool(MaxTimeoutText);

    /* Keep track of how many seconds have elapsed. */
    m->MillisecondsElapsed += 100;
}


STATIC
EFI_STATUS
TextInit(IN SIMPLE_DISPLAY *This,
         IN CONFIGURATION *Configuration)
{
    if (NULL == This || NULL == Configuration) {
        return EFI_INVALID_PARAMETER;
    }

    EFI_STATUS Status = EFI_SUCCESS;
    UINTN Columns = 0, Rows = 0, NumberOfModes = 0, NativeMode = 0,
        BestMode = 0, BestColumns = 0, BestRows = 0;

    STOP = ST->ConOut;
    if (NULL == STOP) {
        return EFI_NOT_STARTED;
    }

    if (FALSE == Configuration->AutoMode) goto TextInit__SkipMode;

TextInit__QueryMode:
    Status = STOP->QueryMode(STOP,
                             (NULL == STOP->Mode ? 0 : STOP->Mode->Mode),
                             &Columns,
                             &Rows);
    if (EFI_NOT_STARTED == Status) {
        ERRCHECK(STOP->SetMode(STOP, 0));
        goto TextInit__QueryMode;
    } else if (EFI_ERROR(Status)) {
        EFI_DANGERLN("Failure querying console video modes.");
        return Status;
    } else if (NULL == STOP->Mode) {
        EFI_DANGERLN("Empty STOP native mode information.");
        return EFI_NOT_READY;
    }

    NativeMode = STOP->Mode->Mode;
    NumberOfModes = STOP->Mode->MaxMode;

    PRINTLN("STOP:  Inspecting %u console modes...", NumberOfModes);

    for (UINTN i = 0; i < NumberOfModes; ++i) {
        Status = STOP->QueryMode(STOP, i, &Columns, &Rows);
        if (EFI_ERROR(Status)) {
            EFI_WARNINGLN("NOTICE: STOP:  Error while querying mode #%u (Code %u).", i, Status);
            continue;
        }

        /* Auto-select the console mode with the best combined CxR size. */
        if ((Columns * Rows) > (BestColumns * BestRows)) {
            BestMode = i; BestColumns = Columns; BestRows = Rows;
        }

        EFI_COLOR(MFTAH_COLOR_DEBUG);
        PRINTLN("Detected console mode %03u (C by R -> %u x %u)", i, Columns, Rows);
        EFI_COLOR(MFTAH_COLOR_DEFAULT);
    }

    PRINTLN("Finished enumerating console modes.");

    PRINTLN("Setting console mode #%u...", BestMode);
    Status = STOP->SetMode(STOP, BestMode);
    if (EFI_ERROR(Status)) {
        EFI_WARNINGLN("Failed to set the console mode. Keeping the native resolution.");
        ERRCHECK(STOP->SetMode(STOP, NativeMode));
    }

TextInit__SkipMode:
    Status = STOP->QueryMode(STOP,
                             STOP->Mode->Mode,
                             &ModeColumns,
                             &ModeRows);
    if (0 == ModeRows || 0 == ModeColumns) {
        EFI_DANGERLN(
            "ERROR: STOP:  Current mode returned 0 rows or 0 columns (%u/%u:%u).",
            ModeRows, ModeColumns, Status
        );
        return EFI_DEVICE_ERROR;
    }

    /* Generally we don't need to show a cursor, it looks sloppy. If the firmware
        supports it, turn it off. Otherwise, discard the return code; we tried. */
    STOP->EnableCursor(STOP, FALSE);

    TextContext = (TEXT_CONTEXT *)AllocateZeroPool(sizeof(TEXT_CONTEXT));
    if (NULL == TextContext) {
        EFI_DANGERLN("FATAL: STOP:  Out of resources while allocating context.");
        return EFI_OUT_OF_RESOURCES;
    }

    /* Set up the blank row string. This is used to ensure a clear-screen
        does so correctly and fully. */
    CHAR8 *BlankRow = (CHAR8 *)AllocateZeroPool(sizeof(CHAR8) * (ModeColumns + 1));
    if (NULL == BlankRow) {
        EFI_DANGERLN("FATAL: STOP:  Out of resources while allocating blank row text.");
        return EFI_OUT_OF_RESOURCES;
    }

    SetMem(BlankRow, ModeColumns, ' ');
    TextContext->BlankRow = AsciiStrToUnicode(BlankRow);
    FreePool(BlankRow);

    This->ClearScreen(This, Configuration->Colors.Background);

    EFI_MENU_RENDERER_PROTOCOL *Renderer = (EFI_MENU_RENDERER_PROTOCOL *)
        AllocateZeroPool(sizeof(EFI_MENU_RENDERER_PROTOCOL));
    if (NULL == Renderer) {
        EFI_DANGERLN("FATAL: STOP:  Out of resources while allocating menu renderer.");
        return EFI_OUT_OF_RESOURCES;
    }

    Renderer->Redraw    = DrawMenu;
    Renderer->Tick      = TimerTick;
    This->MENU = Renderer;

    return EFI_SUCCESS;
}


STATIC
EFI_STATUS
TextDestroy(IN CONST SIMPLE_DISPLAY *This)
{
    /* TEXT mode is much simpler: it doesn't have entire pixel buffers to clean. */
    FreePool(TextContext->BlankRow);
    FreePool(TextContext);

    FreePool(This->MENU);

    return EFI_SUCCESS;
}


STATIC
VOID
TextClearScreen(IN CONST SIMPLE_DISPLAY *This,
                IN UINT32 Color)
{
    if (
        NULL == STOP
        || NULL == This
        || NULL == TextContext
        || NULL == TextContext->BlankRow
    ) return;

    /* Both clear the screen AND print an entire screen's worth of new-lines.
        This is because ClearScreen sometimes doesn't preserve background colors. */
    STOP->SetAttribute(STOP, (UINT8)Color);
    STOP->ClearScreen(STOP);

    STOP->SetAttribute(STOP, (UINT8)Color);

    /* By manually setting the cursor to the origin and having prints overwrite
        the current 'display', we avoid slowness caused by emulated terminal scrolling. */
    STOP->SetCursorPosition(STOP, 0, 0);
    for (UINTN i = 0; i <= ModeRows; ++i) {
        STOP->OutputString(STOP, TextContext->BlankRow);
    }
}


STATIC
VOID
TextPanic(IN CONST SIMPLE_DISPLAY *This,
          IN CHAR8 *Message,
          IN EFI_STATUS Status,
          IN BOOLEAN IsShutdown,
          IN UINTN ShutdownTimer)
{
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
        TPrint(Message, 3, 3, MFTAH_COLOR_PANIC, TRUE);
    } else {
        CopyMem(Shadow, PanicPrefix, PanicPrefixLength);
        CopyMem((VOID *)&Shadow[PanicPrefixLength], Message, MessageLength);
        Shadow[PanicPrefixLength + MessageLength] = ' ';
        CopyMem((VOID *)&Shadow[PanicPrefixLength + MessageLength + 1], ErrorStr, ErrorStringLength);

        TPrint(Shadow, 3, 3, MFTAH_COLOR_PANIC, TRUE);

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
TextPrintProgress(IN CONST SIMPLE_DISPLAY *This,                      
                  IN CONST CHAR8 *Message,                      
                  IN UINTN Current,                      
                  IN UINTN OutOfTotal)
{
    if (NULL == This) return;

    UINTN ProgressBarWidth = (ModeColumns / 2);
    UINTN ProgressBarStartColumn = (ModeColumns / 2) - (ProgressBarWidth / 2);
    UINTN ProgressBarStartRow = 8;

    /* Need to ensure the message has changed when displaying updates to it.
        This stops delays and flickers in the status text. */
    if (Message != TextContext->PreviousProgressMessage || 0 == Current) {
        if (TextContext->PreviousProgressMessageLength > 0) {
            STOP->SetCursorPosition(STOP, (ModeColumns / 2) - (TextContext->PreviousProgressMessageLength / 2), 4);
            STOP->SetAttribute(STOP, CONFIG->Colors.Background);
            for (UINTN i = 0; i < TextContext->PreviousProgressMessageLength; ++i) {
                STOP->OutputString(STOP, L" ");
            }
        }

        if (NULL != Message) {
            TPrint(Message,
                   (ModeColumns / 2) - (AsciiStrLen(Message) / 2),
                   4,
                   CONFIG_TEXT_COLOR(Text),
                   FALSE);
        }

        TextContext->PreviousProgressMessageLength
            = (NULL == Message) ? 0 : AsciiStrLen(Message);

        TextContext->PreviousProgressMessage = Message;
    }

    STOP->SetAttribute(STOP, InvertColor(CONFIG_TEXT_COLOR(Text)));
    for (UINTN i = ProgressBarStartColumn; i < (ProgressBarStartColumn + ProgressBarWidth); ++i) {
        STOP->SetCursorPosition(STOP, i, ProgressBarStartRow);
        if (i < (ProgressBarStartColumn + ((Current * ProgressBarWidth) / OutOfTotal))) {
            CHAR16 BlockChar[2] = { BLOCKELEMENT_FULL_BLOCK, 0 };
            STOP->OutputString(STOP, BlockChar);
        } else {
            STOP->OutputString(STOP, L" ");
        }
    }
}


STATIC
VOID
FlipToFalse(EFI_EVENT Event,
            VOID *Context)
{ BOOLEAN *b = (BOOLEAN *)Context; if (NULL != b) *b = FALSE; }

STATIC
VOID
TextStall(IN CONST SIMPLE_DISPLAY *This,
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

    /* Always placed at the center-bottom of the screen. */
    Status = LoadingAnimationLoop(((ModeColumns / 2) - 5),
                                  (ModeRows - 7),
                                  10,
                                  5,
                                  &Stall);
    if (EFI_ERROR(Status)) {
        /* Stall normally if the animation can't be rendered. */
        BS->Stall(TimeInMilliseconds * 1000);
    }

    BS->CloseEvent(StallEvent);
}


STATIC
VOID
TextInputPopup(IN CONST SIMPLE_DISPLAY *This,
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

    // TODO Move static numbers to `define` area for LAYOUT_x at the top of this file
    /* based on prompt, minimum width of 40 */
    UINTN BoxWidth = MAX((AsciiStrLen(Prompt) + 6), 40);
    UINTN BoxHeight = 10;

    UINTN StartColumn = (ModeColumns / 2) - (BoxWidth / 2);
    UINTN StartRow = 3;

    /* This is kinda hacky, but if we get a call here with a zero-length
        input string, then we can set the popup to re-render. This will
        save cycles as the user is typing and only redraw the box (1) each
        time it is shown and (2) when a user deletes their entire input. */
    if (
        0 == InputLength
        || NULL != ErrorMessage
        || TRUE == TextContext->InputPopupShowedError
    ) {
        /* Keep track of whether this pass is showing an error. This will trigger
            a render a second time after an error is shown that clears it (provided
            no other error is still persisting). */
        TextContext->InputPopupShowedError = (NULL != ErrorMessage);

        /* Enforce the popup terminal color. */
        STOP->SetAttribute(STOP, CONFIG_TEXT_COLOR(PromptPopup));

        /* Draw the popup rectangle. */
        Rect(StartColumn,
             StartRow,
             BoxWidth,
             BoxHeight,
             (UINT8)STOP->Mode->Attribute,
             L'#',
             (0 != (0xF0 & CONFIG_TEXT_COLOR(PromptPopup))));

        /* Write Prompt text. */
        TPrint(Prompt,
               (ModeColumns / 2) - (AsciiStrLen(Prompt) / 2),
               StartRow + 2,
               CONFIG_TEXT_COLOR(PromptPopup),
               FALSE);

        /* Print the error message if it's set. This uses static colors. */
        if (NULL != ErrorMessage) {
            TPrint(ErrorMessage,
                   (ModeColumns / 2) - (AsciiStrLen(ErrorMessage) / 2),
                   StartRow + 5,
                   MFTAH_COLOR_PANIC,
                   FALSE);
        }
    }

    /* Write input as well as spaces to pad it. These will have inverted colors. */
    UINTN TextBoxWidth = MAX(MIN((BoxWidth - 10), ((3 * BoxWidth) / 4)), 10);
    TextBoxWidth -= (TextBoxWidth % 2);   /* even padding */
    UINTN TextBoxRow = (StartRow + BoxHeight - 3);
    UINTN TextBoxStartColumn = (ModeColumns / 2) - (TextBoxWidth / 2);
    CHAR16 TextCurrentChar[2] = {0};

    UINTN SavedAttribute = STOP->Mode->Attribute;
    STOP->SetAttribute(STOP, InvertColor(CONFIG_TEXT_COLOR(PromptPopup)));

    for (UINTN i = 0; i < TextBoxWidth; ++i) {
        STOP->SetCursorPosition(STOP, (TextBoxStartColumn + i), TextBoxRow);

        if (i < InputLength) {
            if (TRUE == IsHidden) {
                STOP->OutputString(STOP, L"*");
            } else {
                *((CHAR8 *)(TextCurrentChar)) = CurrentInput[i];
                STOP->OutputString(STOP, TextCurrentChar);
            }
        } else {
            STOP->OutputString(STOP, L" ");
        }
    }

    STOP->SetAttribute(STOP, SavedAttribute);
}


STATIC
VOID
TPrint(IN CHAR8 *Str,
       IN UINT8 Column,
       IN UINT8 Row,
       IN UINT8 Color,
       IN BOOLEAN Wrap)
{
    if (
        NULL == Str
        || 0 == AsciiStrLen(Str)
        || Column > ModeColumns
        || Row > ModeRows
    ) return;

    EFI_STATUS Status = EFI_SUCCESS;

    Status = STOP->SetCursorPosition(STOP, Column, Row);
    if (EFI_ERROR(Status)) return;

    CHAR8 *CopiedStr = (CHAR8 *)
        AllocateZeroPool(sizeof(CHAR8) * (AsciiStrLen(Str) + 1));
    if (NULL == CopiedStr) return;

    CopyMem(CopiedStr, Str, AsciiStrLen(Str));
    if ((Column + AsciiStrLen(Str)) > ModeColumns && FALSE == Wrap) {
        /* Cut off the string at the wrap location. */
        CopiedStr[ModeColumns - Column] = '\0';
    }

    CHAR16 *Printable = AsciiStrToUnicode(CopiedStr);
    if (NULL == Printable) {
        FreePool(CopiedStr);
        return;
    }

    UINTN SavedAttribute = STOP->Mode->Attribute;
    STOP->SetAttribute(STOP, Color);
    STOP->OutputString(STOP, Printable);
    STOP->SetAttribute(STOP, SavedAttribute);

    FreePool(CopiedStr);
    FreePool(Printable);
}


STATIC
EFI_STATUS
LoadingAnimationLoop(IN UINT8 AtColumn,
                     IN UINT8 AtRow,
                     IN UINT8 Width,
                     IN UINT8 Height,
                     IN VOLATILE BOOLEAN *Stall)
{
    if (
        NULL == Stall
        || FALSE == *Stall
        || (AtColumn + Width) >= ModeColumns
        || (AtRow + Height) >= ModeRows
    ) return EFI_INVALID_PARAMETER;

    UINTN CurrentAttribute = 0;
    CHAR16 RngChar[2] = { BLOCKELEMENT_FULL_BLOCK, 0 };

    EFI_RNG_PROTOCOL *RNG = NULL;
    EFI_GUID gEfiRngProtocolGuid = EFI_RNG_PROTOCOL_GUID;
    BS->LocateProtocol(&gEfiRngProtocolGuid,
                       NULL,
                       (VOID **)&RNG);

    while (TRUE == *Stall) {
        for (UINTN i = 0; i < Height; ++i) {
            STOP->SetCursorPosition(STOP, AtColumn, (AtRow + i));

            for (UINTN j = 0; j < Width; ++j) {
                if (NULL != RNG) {
                    RNG->GetRNG(RNG, NULL, sizeof(UINTN), &CurrentAttribute);
                } else ++CurrentAttribute;
                STOP->SetAttribute(STOP, 0x7F & CurrentAttribute);

                if (NULL != RNG) {
                    RNG->GetRNG(RNG, NULL, sizeof(CHAR16), &RngChar[0]);
                } else ++RngChar[0];

                RngChar[0] = (L' ' + (RngChar[0] % (L'~' - L' ')));
                STOP->OutputString(STOP, RngChar);
            }
        }

        /* NOTE: SMALL delay here. Let this be as fast and as red-hot as it can be.
            We ultimately want to most accurate stall times, which means we need
            to get to the 'while' condition sooner than later. */
        BS->Stall(70000);
    }

    /* Clean up the area. */
    STOP->SetAttribute(STOP, CONFIG->Colors.Background);
    for (UINTN i = 0; i < Height; ++i) {
        STOP->SetCursorPosition(STOP, AtColumn, (AtRow + i));

        for (UINTN j = 0; j < Width; ++j) {
            STOP->OutputString(STOP, L" ");
        }
    }

    return EFI_SUCCESS;
}


STATIC
VOID
Rect(IN UINTN StartColumn,
     IN UINTN StartRow,
     IN UINTN BoxWidth,
     IN UINTN BoxHeight,
     IN UINT8 Color,
     IN CHAR16 BorderChar,
     IN BOOLEAN HasShadow)
{
    if (
        0 == BoxWidth
        || 0 == BoxHeight
        || (StartColumn + BoxWidth) >= ModeColumns
        || (StartRow + BoxHeight) >= ModeRows
    ) return;

    /* Preserve the current terminal attribute/color. */
    UINT8 PushAttribute = STOP->Mode->Attribute;

    BorderChar = (L'\0' != BorderChar) ? BorderChar : L' ';
    CHAR16 BorderStr[2] = { BorderChar, 0 };

    /* Enforce the popup terminal color. */
    STOP->SetAttribute(STOP, Color);

    /* Draw the top row of the 'popup' */
    STOP->SetCursorPosition(STOP, StartColumn, StartRow);
    for (UINTN i = 0; i < BoxWidth; ++i) STOP->OutputString(STOP, BorderStr);

    if (BoxHeight > 1) {
        /* Bottom row. */
        STOP->SetCursorPosition(STOP, StartColumn, (StartRow + BoxHeight - 1));
        for (UINTN i = 0; i < BoxWidth; ++i) STOP->OutputString(STOP, BorderStr);

        /* Box edges and center spaces (no border char if set). */
        for (UINTN i = (StartRow + 1); i < (StartRow + BoxHeight - 1); ++i) {
            STOP->SetCursorPosition(STOP, StartColumn, i);
            STOP->OutputString(STOP, BorderStr);

            STOP->SetCursorPosition(STOP, (StartColumn + BoxWidth - 1), i);
            STOP->OutputString(STOP, BorderStr);

            for (UINTN j = 1; j < (BoxWidth - 1); ++j) {
                STOP->SetCursorPosition(STOP, (StartColumn + j), i);
                STOP->OutputString(STOP, L" ");
            }
        }
    }

    /* Box shadow. Done last to not have to worry about changing colors back. */
    if (
        TRUE == HasShadow
        && (StartRow + BoxHeight + 1) < ModeRows
        && (StartColumn + BoxWidth + 1) < ModeColumns
    ) {
        STOP->SetAttribute(STOP, 0);

        for (UINTN i = 2; i < BoxWidth; ++i) {
            STOP->SetCursorPosition(STOP, (StartColumn + i), (StartRow + BoxHeight));
            STOP->OutputString(STOP, BoxShadow);
        }
        for (UINTN i = 1; i <= BoxHeight; ++i) {
            STOP->SetCursorPosition(STOP, (StartColumn + BoxWidth), (StartRow + i));
            STOP->OutputString(STOP, BoxShadow);
            // STOP->OutputString(STOP, BoxShadow);   /* double up */
        }
    }

    /* Restore the atribute. */
    STOP->SetAttribute(STOP, PushAttribute);
}
