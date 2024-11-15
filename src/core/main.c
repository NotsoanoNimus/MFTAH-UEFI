#include "../include/mftah_uefi.h"

#include "../include/drivers/all.h"

#include "../include/core/input.h"
#include "../include/core/util.h"

#include "../include/loaders/loader.h"



/* "Fixup" for GNU-EFI's print.c compilation module. idk */
UINTN _fltused = 0;

/* Image handle instance from the entry call. */
EFI_HANDLE ENTRY_HANDLE;

/* Concrete definition of the vendor ID. */
EFI_GUID gXmitVendorGuid = XMIT_XYZ_VENDOR_GUID;

/* Ramdisk image base. */
UINT8 *gRamdiskImage;
/* Ramdisk image length. */
UINT64 gRamdiskImageLength;



STATIC VOID EnvironmentInitialize(IN EFI_HANDLE ImageHandle);

STATIC EFI_STATUS EnterMenu();
STATIC EFI_STATUS MenuLoop(IN MENU_STATE *m);


EFI_STATUS
EFIAPI
efi_main(EFI_HANDLE ImageHandle,
         EFI_SYSTEM_TABLE *SystemTable)
{
    /* Local variable initializations. */
    EFI_STATUS Status = EFI_SUCCESS;

    /* Initialize the loader. */
    InitializeLib(ImageHandle, SystemTable);
    ENTRY_HANDLE = ImageHandle;

    /* Load all necessary MFTAH supplementary drivers. */
    EnvironmentInitialize(ImageHandle);

    /* Search for the configuration. If it's not found, the user is
        presented with a prompt to enter its filename. */
    Status = ConfigParse(ImageHandle, DefaultConfigFileName);
    if (EFI_ERROR(Status)) {
        // TODO Provide more information about the problem and status.
        PANIC("Invalid or missing configuration.");
    }

    /* Sleep for 1s to show the boot logo, because... because it just should, ok?? */
    BS->Stall(EFI_SECONDS_TO_MICROSECONDS(1));

    /* The menus will be responsible for carrying out the rest of the process. */
    Status = DisplaysSetMode(CONFIG->Mode, TRUE);
    if (EFI_ERROR(Status) || NULL == DISPLAY) {
        PANIC("Failed to set display mode.");
    }

    /* Initialize the display driver. */
    Status = DISPLAY->Initialize(DISPLAY, CONFIG);
    if (EFI_ERROR(Status)) {
        EFI_WARNINGLN("Native mode initialization failed with code (%u). Falling back.", Status);

        // TODO: Change configuration values to their defaults for opposite-mode compatibility.
        DISPLAY_MODE AlternateMode = (CONFIG->Mode & GRAPHICAL) ? TEXT : GRAPHICAL;
        //ConfigChangeMode(AlternateMode);

        DisplaysSetMode(AlternateMode, TRUE);
        Status = DISPLAY->Initialize(DISPLAY, CONFIG);
        if (EFI_ERROR(Status)) {
            PANIC("Failed to initialize any valid display driver.");
        }
    }

    /* Disable the UEFI watchdog timer. The code '0x1FFFF' is a dummy
        value and doesn't actually do anything. Not a magic number. */
    BS->SetWatchdogTimer(0, 0x1FFFF, 0, NULL);

    Status = EnterMenu();
    if (EFI_ERROR(Status)) {
        // TODO better info
        PANIC("Menu retured error");
    }

    PANIC("The loader menu exited with no error code. This should never happen; please reboot.");

    /* Execution should never return here. */
    return EFI_SUCCESS;
}



STATIC
VOID
EnvironmentInitialize(IN EFI_HANDLE ImageHandle)
{
    EFI_STATUS Status;

    /* Banner antix */
    EFI_COLOR(MFTAH_COLOR_DEFAULT);
    ST->ConOut->ClearScreen(ST->ConOut);
    for (UINTN i = 0; i < 30; ++i) PRINT("\r\n");

    EFI_COLOR(MFTAH_COLOR_ASCII_ART);
    PRINTLN(
        "\r\nMFTAH Chainloader v%s (libmftah v%s)\r\n    Copyright (c) 2024 Zack Puhl <github@xmit.xyz>\r\n%s",
        MFTAH_UEFI_VERSION, LIBMFTAH_VERSION, MftahAsciiArt
    );
    EFI_COLOR(MFTAH_COLOR_DEFAULT);

    /* All drivers are initialized before the configuration is parsed. This
        means that they are all REQUIRED regardless of selected options. */
    PRINT("Initializing...   ");

    if (EFI_ERROR((Status = AcpiInit())) || NULL == AcpiGetInstance()) {
        ABORT("Failed to initialize a compatible ACPI driver.");
    }

    if (EFI_ERROR((Status = MftahInit())) || NULL == MftahGetInstance()) {
        ABORT("Failed to initialize the MFTAH driver.");
    }

    if (EFI_ERROR((Status = RamdiskDriverInit(ImageHandle)))) {
        ABORT("Failed to initialize the ramdisk driver.");
    }

    if (EFI_ERROR((Status = ConfigInit())) || NULL == CONFIG) {
        ABORT("Failed to initialize the configuration driver.");
    }

    PRINTLN("ok");
}


EFI_STATUS
EnterMenu()
{
    EFI_STATUS Status = EFI_SUCCESS;

    /* Sanity checking. */
    if (0 == CONFIG->ChainsLength) {
        // TODO: Should return codes here instead of panicking.
        PANIC("No chains were found to load!");
    } else if (CONFIG_MAX_CHAINS < CONFIG->ChainsLength) {
        PANIC("Too many chains were found to load!");
    } else if (NULL == DISPLAY->MENU) {
        PANIC("The MENU object is not initialized.");
    }

    MENU_STATE *MenuState = (MENU_STATE *)AllocateZeroPool(sizeof(MENU_STATE));
    if (NULL == MenuState) {
        PANIC("Unable to allocate menu state: out of resources.");
    }

    for (UINTN i = 0; i < CONFIG->ChainsLength; ++i) {
        if (CONFIG->Chains[i]->IsImmediate) {
            CHAR8 *ValidationErrorMsg = (CHAR8 *)AllocateZeroPool(sizeof(CHAR8) * 256);
            if (NULL == ValidationErrorMsg) return EFI_OUT_OF_RESOURCES;

            /* Open the chain immediately. */
            FreePool(ValidationErrorMsg);
            FreePool(MenuState);

            LoaderEnterChain(i);

            return EFI_SUCCESS;   /* SHOULD never be reached, but just in case (tm) */
        }

        if (CONFIG->Chains[i]->IsDefault) {
            /* NOTE: It is intentional that multiple 'default' statements will 
                automatically choose the last Chain set with it. */
            MenuState->CurrentItemIndex = i;
            /* This property is for global timeouts. */
            MenuState->DefaultItemindex = i;
        }

        MenuState->ItemsList[i].Chain = CONFIG->Chains[i];
        MenuState->ItemsList[i].ConfiguredChainIndex = i;
        MenuState->ItemsList[i].Enabled = TRUE;
        MenuState->ItemsList[i].Text = CONFIG->Chains[i]->Name;

        ++MenuState->ItemsListLength;
    }

    /* Initial rendering. */
    DISPLAY->ClearScreen(DISPLAY, CONFIG->Colors.Background);
    DISPLAY->MENU->Redraw(MenuState);

    /* Enter the loop. */
    Status = MenuLoop(MenuState);
    if (EFI_ERROR(Status)) {
        EFI_DANGERLN("MENU:  Loop broken with error (%u).", Status);
        goto EnterMenu__Error;
    }

    /* Technically, this function should never return here. */
    return EFI_SUCCESS;

EnterMenu__Error:
    FreePool(MenuState);
    return Status;
}


STATIC
EFIAPI
EFI_STATUS
MenuLoop(IN MENU_STATE *m)
{
    EFI_STATUS Status = EFI_SUCCESS;
    EFI_EVENT TimerEvent = {0};
    EFI_KEY_DATA KeyData = {0};

    /* Create the timer's 'Tick' event. */
    ERRCHECK(
        BS->CreateEvent((EVT_TIMER | EVT_NOTIFY_SIGNAL),
                       TPL_NOTIFY,
                       DISPLAY->MENU->Tick,
                       (VOID *)m,
                       &TimerEvent)
    );

    /* The UEFI spec says the timer interval is every 100 nanoseconds,
        so convert that to approximately 100 milliseconds. */
    ERRCHECK(
        BS->SetTimer(TimerEvent, TimerPeriodic, EFI_MILLISECONDS_TO_100NS(100))
    );

MenuLoop__Main:
    Status = ReadKey(&KeyData,
                     (FALSE == m->KeyPressReceived && CONFIG->Timeout > 0)
                        ? (CONFIG->Timeout + 200)
                        : 0);

    if (EFI_ERROR(Status) && EFI_TIMEOUT != Status) {
        DISPLAY->Panic(DISPLAY, "Unknown keyboard input failure.", FALSE, 0);
        BS->Stall(EFI_SECONDS_TO_MICROSECONDS(3));

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
                BS->CloseEvent(TimerEvent);
                DISPLAY->ClearScreen(DISPLAY, 0);
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

            /* The chain's rudimentary validation has passed. Pass along basically
                the entire program context and attempt to load the chain.  */
            BS->CloseEvent(TimerEvent);
            FreePool(ValidationErrorMsg);

            UINTN index = m->CurrentItemIndex;
            FreePool(m);

            /* you are now leaving the main menu :) */
            DISPLAY->ClearScreen(DISPLAY, CONFIG->Colors.Background);
            LoaderEnterChain(index);

            return EFI_SUCCESS;   /* SHOULD never be reached, but just in case (tm) */

        default: goto MenuLoop__Main;   /* Do nothing on invalid keystrokes */
    }

MenuLoop__Redraw:
    DISPLAY->MENU->Redraw(m);
    goto MenuLoop__Main;

MenuLoop__Break:
    BS->CloseEvent(TimerEvent);
    return Status;
}

