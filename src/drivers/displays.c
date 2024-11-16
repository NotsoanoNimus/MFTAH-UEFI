#include "../include/drivers/displays.h"



CHAR8 *NormalTimeoutStr = "Choosing default in %u seconds.";
CHAR8 *MaxTimeoutStr = "Global timeout in %u seconds.";
CHAR8 *NoTimeoutStr = "No timeout values are set.";
CHAR8 *DefaultTimeoutStr = "Choosing default menu option...";
CHAR8 *MaxTimeoutExceededtStr = "Maximum timeout exceeded! Shutting down!";
CHAR8 *PanicPrefix = "PANIC: ";

CHAR16 ErrorStringBuffer[ERROR_STRING_BUFFER_SIZE] = {0};


SIMPLE_DISPLAY *DISPLAY = NULL;


/* The primary display protocol object for GRAPHICAL mode. */
EXTERN SIMPLE_DISPLAY GUI;

/* The primary display protocol object for TEXT mode. */
EXTERN SIMPLE_DISPLAY TUI;

/* The primary display protocol object for NATIVE mode. */
EXTERN SIMPLE_DISPLAY NUI;


EFI_STATUS
DisplaysSetMode(IN CONST DISPLAY_MODE Mode,
                IN BOOLEAN Reset)
{
    if (NULL != DISPLAY) {
        DISPLAY->Destroy(DISPLAY);
        FreePool(DISPLAY);
    }

    SIMPLE_DISPLAY *DisplayObject = NULL;

    switch (Mode) {
        case GRAPHICAL: DisplayObject = &GUI;   break;
        case TEXT:      DisplayObject = &TUI;   break;
        case NATIVE:    DisplayObject = &NUI;   break;

        default: return EFI_NOT_FOUND;
    }

    /* ur mom is undefined behavior */
    CopyMem(&DISPLAY, &DisplayObject, sizeof(SIMPLE_DISPLAY *));

    return EFI_SUCCESS;
}
