#include "../include/drivers/displays.h"



CHAR8 *NormalTimeoutStr = "Choosing default in %u seconds.";
CHAR8 *MaxTimeoutStr = "Global timeout in %u seconds.";
CHAR8 *NoTimeoutStr = "No timeout values are set.";
CHAR8 *DefaultTimeoutStr = "Choosing default menu option...";
CHAR8 *MaxTimeoutExceededtStr = "Maximum timeout exceeded! Shutting down!";


SIMPLE_DISPLAY *DISPLAY = NULL;


/* The primary display protocol object for GRAPHICAL mode. */
EXTERN SIMPLE_DISPLAY GUI;

/* The primary display protocol object for TEXT mode. */
EXTERN SIMPLE_DISPLAY TUI;


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
        // TODO: NONE/BLANK display mode

        default: return EFI_NOT_FOUND;
    }

    /* ur mom is undefined behavior */
    CopyMem(&DISPLAY, &DisplayObject, sizeof(SIMPLE_DISPLAY *));

    return EFI_SUCCESS;
}
