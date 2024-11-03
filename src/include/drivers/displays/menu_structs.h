#ifndef MFTAH_MENU_STRUCTS_H
#define MFTAH_MENU_STRUCTS_H

#include "../../mftah_uefi.h"
#include "../config.h"



typedef
struct {
    CHAR8               *Text;
    UINTN               ConfiguredChainIndex;
    CONFIG_CHAIN_BLOCK  *Chain;
    BOOLEAN             Enabled;
} _PACKED MENU_ITEM;

typedef
struct {
    MENU_ITEM           ItemsList[CONFIG_MAX_CHAINS];
    UINTN               ItemsListLength;
    UINTN               CurrentItemIndex;
    UINTN               DefaultItemindex;
    VOLATILE BOOLEAN    TimeoutOccurred;
    VOLATILE BOOLEAN    PauseTickRenders;
    VOLATILE BOOLEAN    KeyPressReceived;
    VOLATILE UINTN      MillisecondsElapsed;
} _PACKED MENU_STATE;


typedef
EFI_STATUS
(EFIAPI *MENU_HOOK__INIT)(
    IN CONFIGURATION    *c,
    IN MENU_STATE       *State
);

typedef
CHAR8 *
(EFIAPI *MENU_HOOK__INPUT_POPUP)(
    IN CHAR8            *Prompt,
    IN UINTN            MaxLength,
    IN BOOLEAN          IsHidden
);

typedef
BOOLEAN
(EFIAPI *MENU_HOOK__CONFIRMATION_POPUP)(
    IN CHAR8            *Prompt
);

typedef
VOID
(EFIAPI *MENU_HOOK__POPUP)(
    IN CHAR8            *Message
);

typedef
VOID
(EFIAPI *MENU_HOOK__POPUP_WARNING)(
    IN CHAR8            *Message
);

typedef
VOID
(EFIAPI *MENU_HOOK__REDRAW)(
    IN CONFIGURATION    *c,
    IN MENU_STATE       *m
);

typedef
VOID
(EFIAPI *MENU_HOOK__CLEAR_SCREEN)(
    IN UINT32           Color
);


typedef
struct {
    MENU_HOOK__INIT                 Initialize;
    MENU_HOOK__INPUT_POPUP          InputPopup;
    MENU_HOOK__CONFIRMATION_POPUP   Confirmation;
    MENU_HOOK__POPUP                Popup;
    MENU_HOOK__POPUP_WARNING        WarningPopup;
    MENU_HOOK__REDRAW               Redraw;
    MENU_HOOK__CLEAR_SCREEN         ClearScreen;
    EFI_EVENT_NOTIFY                Tick;
} _PACKED EFI_MENU_RENDERER_PROTOCOL;

EXTERN CONST EFI_MENU_RENDERER_PROTOCOL *MENU;


typedef
struct {
    MENU_STATE      *m;
    CONFIGURATION   *c;
} _PACKED TIMER_CTX;



#endif   /* MFTAH_MENU_STRUCTS_H */
