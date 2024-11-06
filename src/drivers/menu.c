#include "../include/drivers/displays/menu.h"
#include "../include/loaders/loader.h"

#include "../include/core/input.h"
#include "../include/core/util.h"



CONST EFI_MENU_RENDERER_PROTOCOL *MENU;

STATIC MENU_STATE *MenuState;



STATIC EFIAPI EFI_STATUS
MenuLoop(
    CONFIGURATION *c,
    MENU_STATE *m
);

STATIC EFIAPI EFI_STATUS
SelectEntry(
    CONFIGURATION *c,
    MENU_STATE *m
);


EFI_STATUS
EnterMenu(CONFIGURATION *c)
{
    EFI_STATUS Status = EFI_SUCCESS;

    /* Sanity checking. */
    if (0 == c->ChainsLength) {
        PANIC("No chains were found to load!");
    } else if (CONFIG_MAX_CHAINS < c->ChainsLength) {
        PANIC("Too many chains were found to load!");
    }

    MENU = (EFI_MENU_RENDERER_PROTOCOL *)
        AllocateZeroPool(sizeof(EFI_MENU_RENDERER_PROTOCOL));
    if (NULL == MENU) {
        PANIC("Unable to allocate menu: out of resources.");
    }

    /* Populate the MENU protocol handler based on the current operating mode. */
    switch (c->Mode) {
        case GRAPHICAL:
            Status = GraphicsModePopulateMenu((EFI_MENU_RENDERER_PROTOCOL *)MENU);
            break;
        case TEXT:
            Status = TextModePopulateMenu((EFI_MENU_RENDERER_PROTOCOL *)MENU);
            break;
        default:
            Status = EFI_NOT_FOUND;
            PANIC("Invalid operating mode.");
    }
    if (EFI_ERROR(Status)) {
        PANIC("Failed to hook menu renderer.");
    }

    MenuState = (MENU_STATE *)AllocateZeroPool(sizeof(MENU_STATE));
    if (NULL == MenuState) {
        PANIC("Unable to allocate menu state: out of resources.");
    }

    for (UINTN i = 0; i < c->ChainsLength; ++i) {
        if (c->Chains[i]->IsImmediate) {
            CHAR8 *ValidationErrorMsg = (CHAR8 *)AllocateZeroPool(sizeof(CHAR8) * 256);
            if (NULL == ValidationErrorMsg) {
                // TODO: Error popup?? Out of memory, so this might mean the program needs to stop.
            }

            MenuState->CurrentItemIndex = i;
            Status = LoaderValidateChain(c, MenuState, ValidationErrorMsg);

            if (EFI_ERROR(Status)) {
                PANIC("Failed immediate load: chain failed to validate.");
            } else {
                /* The chain's rudimentary validation has passed. Pass along basically
                    the entire program context and attempt to load the chain.  */
                FreePool(ValidationErrorMsg);
                LoaderEnterChain(c, MenuState, MENU);

                return EFI_SUCCESS;   /* SHOULD never be reached, but just in case (tm) */
            }
        }

        if (c->Chains[i]->IsDefault) {
            /* NOTE: It is intentional that multiple 'default' statements will 
                automatically choose the last Chain set with it. */
            MenuState->CurrentItemIndex = i;
            /* This property is for global timeouts. */
            MenuState->DefaultItemindex = i;
        }

        MenuState->ItemsList[i].Chain = &(c->Chains[i]);
        MenuState->ItemsList[i].ConfiguredChainIndex = i;
        MenuState->ItemsList[i].Enabled = TRUE;
        MenuState->ItemsList[i].Text = c->Chains[i]->Name;

        ++MenuState->ItemsListLength;
    }

    /* Call the per-mode init hook for the menu. This typically initializes all visual
        component lifetimes (i.e., 'renderables') in one shot. */
    Status = MENU->Initialize(c, MenuState);
    if (EFI_ERROR(Status)) {
        EFI_DANGERLN("MENU:  Failed to initialize through rendering mode (%u).", Status);
        goto EnterMenu__Error;
    }

    /* Initial rendering. */
    MENU->ClearScreen(c->Colors.Background);
    MENU->Redraw(c, MenuState);

    /* Enter the loop. */
    Status = MenuLoop(c, MenuState);
    if (EFI_ERROR(Status)) {
        EFI_DANGERLN("MENU:  Loop broken with error (%u).", Status);
        goto EnterMenu__Error;
    }

    /* Technically, this function should never return here. */
    return EFI_SUCCESS;

EnterMenu__Error:
    FreePool(MENU);
    FreePool(MenuState);
    return Status;
}


STATIC
EFIAPI
EFI_STATUS
MenuLoop(CONFIGURATION *c,
         MENU_STATE *m)
{
    EFI_STATUS Status = EFI_SUCCESS;
    EFI_EVENT TimerEvent = {0};
    EFI_KEY_DATA KeyData = {0};

    TIMER_CTX TimerContext = { .c = c, .m = m };

    /* Create the timer's 'Tick' event. */
    ERRCHECK_UEFI(BS->CreateEvent, 5,
                  (EVT_TIMER | EVT_NOTIFY_SIGNAL),
                  TPL_NOTIFY,
                  MENU->Tick,
                  (VOID *)&TimerContext,
                  &TimerEvent);

    /* The UEFI spec says the timer interval is every 100 nanoseconds,
        so convert that to approximately 100 milliseconds. */
    ERRCHECK_UEFI(BS->SetTimer, 3,
                  TimerEvent,
                  TimerPeriodic,
                  1 * 100 * 1000 * 10);

MenuLoop__Main:
    Status = ReadKey(&KeyData,
                     (FALSE == m->KeyPressReceived && c->Timeout > 0)
                        ? (c->Timeout + 200)
                        : 0);

    if (EFI_ERROR(Status) && EFI_TIMEOUT != Status) {
        uefi_call_wrapper(BS->Stall, 1, 10000000);
        GPrint("Unknown keyboard input failure.", FB->BLT, 20, 20, 0xFFFF0000, 0, FALSE, 2);
        FB->Flush(FB);
        uefi_call_wrapper(BS->Stall, 1, 3000000);

        Status = EFI_DEVICE_ERROR;
        goto MenuLoop__Break;
    }

    if (TRUE == m->TimeoutOccurred || EFI_TIMEOUT == Status) {
        m->CurrentItemIndex = m->DefaultItemindex;
        goto MenuLoop__SelectEntry;
    }

    m->KeyPressReceived = TRUE;   /* Can use this to cancel the regular timeout */

    switch (KeyData.Key.ScanCode) {
        /* NOTE: Up and down are switched because of the menu rendering layout. */
        case SCAN_DOWN:
        case SCAN_PAGE_DOWN:
            if (m->CurrentItemIndex < (m->ItemsListLength-1)) m->CurrentItemIndex++;
            goto MenuLoop__Redraw;
        case SCAN_UP:
        case SCAN_PAGE_UP:
            if (m->CurrentItemIndex > 0) m->CurrentItemIndex--;
            goto MenuLoop__Redraw;
        case SCAN_ESC:
            if (
                (
                    KeyData.KeyState.KeyShiftState & EFI_SHIFT_STATE_VALID
                    && (
                        (KeyData.KeyState.KeyShiftState & EFI_LEFT_CONTROL_PRESSED)
                        || (KeyData.KeyState.KeyShiftState & EFI_RIGHT_CONTROL_PRESSED)
                    )
                )
                || READKEY_FALLBACK_INDICATOR == KeyData.KeyState.KeyShiftState
            ) {
                uefi_call_wrapper(BS->CloseEvent, 1, TimerEvent);
                MENU->ClearScreen(0);
                Reboot(EFI_SUCCESS);
            }
            break;
        default: break;
    }

    switch (KeyData.Key.UnicodeChar) {
        case L'\r':
        case L'\n':
        MenuLoop__SelectEntry:
            /* Be sure to temporarily stop the timer from updating the framebuffer. */
            m->PauseTickRenders = TRUE;

            CHAR8 *ValidationErrorMsg = (CHAR8 *)AllocateZeroPool(sizeof(CHAR8) * 256);
            if (NULL == ValidationErrorMsg) {
                // TODO: Error popup?? Out of memory, so this might mean the program needs to stop.
            }

            Status = LoaderValidateChain(c, m, ValidationErrorMsg);
            if (EFI_ERROR(Status)) {
                // TODO: Error popup??
                //
                m->PauseTickRenders = FALSE;
            } else {
                /* The chain's rudimentary validation has passed. Pass along basically
                    the entire program context and attempt to load the chain.  */
                uefi_call_wrapper(BS->CloseEvent, 1, TimerEvent);
                FreePool(ValidationErrorMsg);

                /* you are now leaving the main menu :) */
                LoaderEnterChain(c, m, MENU);
                return EFI_SUCCESS;   /* SHOULD never be reached, but just in case (tm) */
            }

            FreePool(ValidationErrorMsg);
            break;
        default: goto MenuLoop__Main;   /* Do nothing on invalid keystrokes */
    }

MenuLoop__Redraw:
    MENU->Redraw(c, m);
    goto MenuLoop__Main;

MenuLoop__Break:
    uefi_call_wrapper(BS->CloseEvent, 1, TimerEvent);
    return Status;
}


STATIC
EFIAPI
EFI_STATUS
SelectEntry(CONFIGURATION *c,
            MENU_STATE *m)
{
    EFI_STATUS Status = EFI_SUCCESS;
    CONFIG_CHAIN_BLOCK *chain = c->Chains[m->CurrentItemIndex];

    return EFI_SUCCESS;
}
