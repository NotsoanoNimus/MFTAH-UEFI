#ifndef MFTAH_INPUT_H
#define MFTAH_INPUT_H

#include "../mftah_uefi.h"



/**
 * Generic function prototype for printing input-related prompts
 *  and other 'live' data.
 */
typedef
VOID (EFIAPI *PRINT_HOOK)(
    CONST CHAR16 *Format,
    ...
);



/**
 * Read a key stroke from the keyboard. Blocks until one is received.
 * 
 * @param[out]  Key         The key that was pressed.
 * @param[out]  KeyEVent    The key event's data.
 * 
 * @returns EFI_SUCCESS when a key stroke was successfully processed, non-success on error.
 */
EFI_STATUS
ReadKey(
    OUT EFI_KEY_DATA    *KeyData
);

#define READKEY_FALLBACK_INDICATOR  0xFABAFABA


/**
 * A generic method call to read a newline-terminated keyboard input line.
 * 
 * @param[in]  Prompt  A message to display when collecting the user's input.
 * @param[out] InputBuffer  A pointer to a CHAR16 array or buffer which can store up to MaxLength characters.
 * @param[out] ResultingBufferLength  A point to a UINT8 whose value is changed to the returned length of the entered string.
 * @param[in]  IsSecret  Whether the user's input should be kept hidden or not. This is usually set to TRUE for password inputs.
 * @param[in]  MaxLength  The maximum length of the accepted input string. The InputBuffer param should be capable of accepting this many characters.
 * @param[in]  PrintHook  The print function to use, given by the current GUI/TUI mode.
 * 
 * @returns Whether the operation encountered an irrecoverable error.
 */
EFI_STATUS
EFIAPI
ReadChar16KeyboardInput(
    IN CONST CHAR16 *Prompt,
    OUT CHAR16      *InputBuffer,
    OUT UINT8       *ResultingBufferLength,
    IN BOOLEAN      IsSecret,
    IN UINT8        MaxLength,
    IN PRINT_HOOK   PrintHook
);


/**
 * Confirm or deny a request to perform an action with a binary yes/no choice.
 * 
 * @param[in]  Prompt  The prompt to issue to the user. DO NOT include the "[Y/n]" markings here, as this method will append them.
 * @param[in]  IsDefaultConfirm  Whether the default action on pressing the ENTER key is to issue confirmation.
 * @param[in]  PrintHook  The print function to use, given by the current GUI/TUI mode.
 * 
 * @retval  EFI_SUCCESS  The confirmation has been accepted by the user.
 * @retval  EFI_ERROR(any_type)  The confirmation has been denied by the user.
 */
EFI_STATUS
EFIAPI
Confirm(
    IN CONST CHAR16     *Prompt,
    IN CONST BOOLEAN    IsDefaultConfirm,
    IN CONST PRINT_HOOK PrintHook
);




#endif   /* MFTAH_INPUT_H */
