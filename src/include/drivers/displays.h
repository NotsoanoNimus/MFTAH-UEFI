#ifndef MFTAH_DISPLAYS_H
#define MFTAH_DISPLAYS_H

#include "../mftah_uefi.h"
#include "config.h"



#pragma pack(push, 1)
typedef
struct {
    CHAR8               *Text;
    UINTN               ConfiguredChainIndex;
    CONFIG_CHAIN_BLOCK  *Chain;
    BOOLEAN             Enabled;
} MENU_ITEM;

typedef
struct {
    MENU_ITEM           ItemsList[CONFIG_MAX_CHAINS];
    UINTN               ItemsListLength;
    UINTN               CurrentItemIndex;
    UINTN               DefaultItemindex;
    VOLATILE UINTN      MillisecondsElapsed;
    VOLATILE BOOLEAN    TimeoutOccurred;
    VOLATILE BOOLEAN    PauseTickRenders;
    VOLATILE BOOLEAN    KeyPressReceived;
} MENU_STATE;
#pragma pack(pop)


EXTERN CHAR8 *NormalTimeoutStr;
EXTERN CHAR8 *MaxTimeoutStr;
EXTERN CHAR8 *NoTimeoutStr;
EXTERN CHAR8 *DefaultTimeoutStr;
EXTERN CHAR8 *MaxTimeoutExceededtStr;
EXTERN CHAR8 *PanicPrefix;

#define ERROR_STRING_BUFFER_SIZE    64
EXTERN CHAR16 ErrorStringBuffer[];


typedef
VOID
(EFIAPI *MENU_HOOK__REDRAW)(
    IN MENU_STATE       *m
);


typedef
struct {
    MENU_HOOK__REDRAW               Redraw;
    EFI_EVENT_NOTIFY                Tick;
} EFI_MENU_RENDERER_PROTOCOL;



/* Different types of PRINT calls for the display. */
typedef
enum {
    Normal,
    Warning,
    Danger,
    Panic
} PRINT_TYPE;


/* Interface declaration. */
typedef
struct S_EFI_SIMPLE_ABSTRACT_DISPLAYS_PROTOCOL
EFI_SIMPLE_ABSTRACT_DISPLAYS_PROTOCOL;

/* Aliasing to a shorter type name. */
typedef
EFI_SIMPLE_ABSTRACT_DISPLAYS_PROTOCOL
SIMPLE_DISPLAY;


/**
 * Initialize a configuration-based displays mode.
 * 
 * @param[in]   Configuration   The parsed configuration instance for the runtime.
 * 
 * @returns Whether the mode was able to initialize successfully based on the configuration.
 */
typedef
EFI_STATUS
(EFIAPI *HOOK_DISPLAY_MODE__INIT)(
    IN  SIMPLE_DISPLAY  *This,
    IN  CONFIGURATION   *Configuration
);

/**
 * Destroy and free resources used by a particular displays mode.
 * 
 * @returns Whether the operation completed successfully.
 */
typedef
EFI_STATUS
(EFIAPI *HOOK_DISPLAY_MODE__DESTROY)(
    IN  CONST   SIMPLE_DISPLAY  *This
);

/**
 * Clears the screen using the current mode.
 * 
 * @param[in]   Color   The ARGB color to use when clearing the screen.
 * 
 * @returns Nothing.
 */
typedef
VOID
(EFIAPI *HOOK_DISPLAY_MODE__CLEAR_SCREEN)(
    IN  CONST   SIMPLE_DISPLAY  *This,
    IN          UINT32          Color
);

/**
 * Panic function to show an error message in a known location then hang or shutdown.
 */
typedef
VOID
(EFIAPI *HOOK_DISPLAY_MODE__PANIC)(
    IN  CONST   SIMPLE_DISPLAY  *This,
    IN          CHAR8           *Message,
    IN          EFI_STATUS      Status,
    IN          BOOLEAN         IsShutdown,
    IN          UINTN           ShutdownTimer
);

/**
 * Alias a MFTAH progress function for the display to use during await operations.
 */
typedef
VOID
(EFIAPI *HOOK_DISPLAY_MODE__PROGRESS)(
    IN  CONST   SIMPLE_DISPLAY  *This,
    IN  CONST   CHAR8           *Message,
    IN          UINTN           Current,
    IN          UINTN           OutOfTotal
);

/**
 * Generic Stall function. Each mode uses this to produce a cutesy entropy-based animation while waiting.
 * 
 * @returns Nothing.
 */
typedef
VOID
(EFIAPI *HOOK_DISPLAY_MODE__STALL)(
    IN  CONST   SIMPLE_DISPLAY  *This,
    IN  UINTN                   TimeInMilliseconds
);

/**
 * Draw an input pop-up onto the screen.
 * 
 * @param[in]   Current
 */
typedef
VOID
(EFIAPI *HOOK_DISPLAY_MODE__INPUT_POPUP)(
    IN  CONST   SIMPLE_DISPLAY  *This,
    IN  CHAR8                   *Prompt,
    IN  CHAR8                   *CurrentInput,
    IN  BOOLEAN                 IsHidden,
    IN  CHAR8                   *ErrorMessage   OPTIONAL
);

/**
 * Pass-through function to call each Display-specific loading animation.
 * 
 * @param[in]   Context Cast it to a boolean; tells whether the animation should keep going.
 */
typedef
VOID
(EFIAPI *HOOK_DISPLAY_MODE__ASYNC_ANIMATION)(
    IN  VOID                    *Context
);


struct S_EFI_SIMPLE_ABSTRACT_DISPLAYS_PROTOCOL {
    HOOK_DISPLAY_MODE__INIT                 Initialize;
    HOOK_DISPLAY_MODE__DESTROY              Destroy;
    HOOK_DISPLAY_MODE__CLEAR_SCREEN         ClearScreen;
    HOOK_DISPLAY_MODE__PANIC                Panic;
    HOOK_DISPLAY_MODE__PROGRESS             Progress;
    HOOK_DISPLAY_MODE__STALL                Stall;
    HOOK_DISPLAY_MODE__INPUT_POPUP          InputPopup;
    HOOK_DISPLAY_MODE__ASYNC_ANIMATION      AsyncLoadingAnimation;

    EFI_MENU_RENDERER_PROTOCOL              *MENU;
};


/* The primary global Displays protocol handle. */
EXTERN SIMPLE_DISPLAY *DISPLAY;



/**
 * Set the current display mode and reset the screen.
 * 
 * @param[in]   Mode    The mode to set from the configuration.
 * @param[in]   Reset   Whether the screen should be cleared and prepared for the mode change.
 * 
 * @returns Whether the mode could successfully be set.
 */
EFI_STATUS
DisplaysSetMode(
    IN  CONST   DISPLAY_MODE    Mode,
    IN          BOOLEAN         Reset
);



#endif   /* MFTAH_DISPLAYS_H */
