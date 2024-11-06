#include "../include/mftah_uefi.h"
#include "../include/drivers/all.h"



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



STATIC VOID EnvironmentInitialize(
    IN EFI_HANDLE ImageHandle
);


EFI_STATUS
EFIAPI
efi_main(EFI_HANDLE ImageHandle,
         EFI_SYSTEM_TABLE *SystemTable)
{
    /* Local variable initializations. */
    EFI_STATUS Status = EFI_SUCCESS;
    CONFIGURATION *c = NULL;

    /* Initialize the loader. */
    InitializeLib(ImageHandle, SystemTable);
    ENTRY_HANDLE = ImageHandle;

    /* Disable the UEFI watchdog timer. The code '0x1FFFF' is a dummy
        value and doesn't actually do anything. Not a magic number. */
    ERRCHECK_UEFI(BS->SetWatchdogTimer, 4, 0, 0x1FFFF, 0, NULL);

    /* Load all necessary MFTAH supplementary drivers. */
    EnvironmentInitialize(ImageHandle);

    /* Search for the configuration. If it's not found, the user is
        presented with a prompt to enter its filename. */
    Status = ConfigParse(ImageHandle, DefaultConfigFileName);
    if (EFI_ERROR(Status)) {
        PANIC("Invalid or missing configuration!");
    }

    /* Sleep for 1s to show the boot logo, because... because it just should, ok?? */
    // uefi_call_wrapper(BS->Stall, 1, 1000000);

    /* Build and display the menu based on the configured mode. This is
        actually the point where the application will enter 'graphical'
        mode, if selected. The application will spin here until it's done. */
    c = ConfigGet();
    if (NULL == c) {
        PANIC("Failed to acquire a configuration instance.");
    }

    /* The menus will be responsible for carrying out the rest of the process. */
    if (c->Mode & GRAPHICAL) {
        PRINTLN("Entering graphical mode...");

        if (EFI_ERROR((Status = GraphicsInit(c)))) {
            PANIC("Failed to initialize the GOP driver for GRAPHICAL `display` mode.");
        }
    } else {
        PRINTLN("Entering text mode...");

        if (EFI_ERROR((Status = TextModeInit(c)))) {
            PANIC("Failed to initialize the TEXT `display` mode.");
        }
    }

    Status = EnterMenu(c);

    if (EFI_ERROR(Status)) {
        /* If this was a TEXT mode error already, exit. */
        if (c->Mode & TEXT) return Status;

        /* From Graphics mode, try to enter the menu in TEXT mode. */
        c->Mode = TEXT;
        GraphicsDestroy(FALSE);   /* do not capture return values here */

        // TODO: Change configuration values to their defaults for TEXT-mode compatibility.

        TextModeInit(c);
        EFI_DANGERLN("DANGER:  Graphics driver failure; falling back to TEXT mode.");
        uefi_call_wrapper(BS->Stall, 1, 1000000);

        ERRCHECK(EnterMenu(c));
    }

    /* Execution should never return here. */
    FreePool(c);
    Status = EFI_END_OF_FILE;
    PANIC("Reached the end. How did you get here?");

    return EFI_SUCCESS;
}



STATIC
VOID
EnvironmentInitialize(IN EFI_HANDLE ImageHandle)
{
    EFI_STATUS Status;

    /* Banner antix */
    EFI_COLOR(MFTAH_COLOR_DEFAULT);
    uefi_call_wrapper(ST->ConOut->ClearScreen, 1, ST->ConOut);
    for (UINTN i = 0; i < 30; ++i) PRINT("\r\n");

    // EFI_COLOR(MFTAH_COLOR_ASCII_ART);
    // PRINTLN(
    //     "\r\nMFTAH Chainloader v%s (libmftah v%s)\r\n    Copyright (c) 2024 Zack Puhl <github@xmit.xyz>\r\n%s",
    //     MFTAH_UEFI_VERSION, LIBMFTAH_VERSION, MftahAsciiArt
    // );
    // EFI_COLOR(MFTAH_COLOR_DEFAULT);

    /* All drivers are initialized before the configuration is parsed. This
        means that they are all REQUIRED regardless of selected options. */
    PRINT("Initializing...   ");

    if (EFI_ERROR((Status = AcpiInit())) || NULL == AcpiGetInstance()) {
        ABORT("Failed to initialize a compatible ACPI driver.");
    }

    if (EFI_ERROR((Status = MftahInit())) || NULL == MftahGetInstance()) {
        ABORT("Failed to initialize the MFTAH adapter driver.");
    }

    if (EFI_ERROR((Status = RamdiskDriverInit(ImageHandle)))) {
        ABORT("Failed to initialize the ramdisk driver.");
    }

    if (EFI_ERROR((Status = ConfigInit()))) {
        ABORT("Failed to initialize the default configuration and driver.");
    }

    PRINTLN("ok");
}
