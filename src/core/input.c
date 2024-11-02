#include "../include/core/input.h"



STATIC EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *STIEP = NULL;



EFI_STATUS
ReadKey(EFI_KEY_DATA *KeyData)
{
    EFI_STATUS Status = EFI_SUCCESS;
    EFI_EVENT KeyEvent = {0};
    EFI_GUID gEfiSimpleTextInputExProtocolGuid = EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL_GUID;

    uefi_call_wrapper(ST->ConIn->Reset, 2, ST->ConIn, FALSE);

    if (NULL == STIEP) {
        Status = uefi_call_wrapper(BS->LocateProtocol, 3,
                                   &gEfiSimpleTextInputExProtocolGuid,
                                   NULL,
                                   (VOID **)&STIEP);
        if (EFI_ERROR(Status) || NULL == STIEP) goto ReadKey__Fallback;
    }

    uefi_call_wrapper(STIEP->Reset, 2, STIEP, FALSE);

    while (TRUE) {
        Status = uefi_call_wrapper(STIEP->ReadKeyStrokeEx, 2, STIEP, KeyData);

        if (EFI_NOT_READY == Status) {
            /* No keystroke data is available. Move on. */
            continue;
        } else if (EFI_ERROR(Status)) {
            goto ReadKey__Fallback;
        }

        break;
    }

    goto ReadKey__Success;

ReadKey__Fallback:
    SetMem(KeyData, sizeof(EFI_KEY_DATA), 0x00);
    ERRCHECK_UEFI(BS->WaitForEvent, 3, 1, &(ST->ConIn->WaitForKey), &KeyEvent);

    while (TRUE) {
        Status = uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, &(KeyData->Key));

        if (EFI_NOT_READY == Status) {
            /* No keystroke data is available. Move on. */
            continue;
        } else if (EFI_ERROR(Status)) {
            return Status;
        }

        break;
    }

    /* Indicate that the fallback method was used to gather input. */
    KeyData->KeyState.KeyShiftState = READKEY_FALLBACK_INDICATOR;

ReadKey__Success:
    uefi_call_wrapper(ST->ConIn->Reset, 2, ST->ConIn, FALSE);

    return EFI_SUCCESS;
}


EFI_STATUS
EFIAPI
ReadChar16KeyboardInput(IN CONST CHAR16 *Prompt,
                        OUT CHAR16 *InputBuffer,
                        OUT UINT8 *ResultingBufferLength,
                        IN BOOLEAN IsSecret,
                        IN UINT8 MaxLength,
                        IN PRINT_HOOK PrintHook)
{
    EFI_STATUS Status = EFI_SUCCESS;
    EFI_INPUT_KEY InputKey;
    UINTN KeyEvent;
    UINT8 InputLength = 0;

    ERRCHECK_UEFI(ST->ConIn->Reset, 2, ST->ConIn, FALSE);
    SetMem(InputBuffer, sizeof(CHAR16) * (MaxLength + 1), 0);

    do {
        PrintHook(L"\r"); PrintHook(Prompt);
        for (UINT8 p = 0; p < InputLength; ++p) {
            if (TRUE == IsSecret) {
                PrintHook(L"*");
            } else {
                PrintHook(L"%c", InputBuffer[p]);
            }
        }
        for (UINT8 p = (InputLength + 1); p > 0; p--) {
            PrintHook(L" ");
        }

        ERRCHECK_UEFI(BS->WaitForEvent, 3, 1, &(ST->ConIn->WaitForKey), &KeyEvent);

        Status = uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, &InputKey);
        if (EFI_NOT_READY == Status) {
            /* No keystroke data is available. Move on. */
            continue;
        } else if (EFI_DEVICE_ERROR == Status) {
            /* Keyboard hardware issue detected. */
            PANIC("Keyboard hardware fault detected.");
        } else if (EFI_UNSUPPORTED == Status) {
            PANIC("Unable to read input from this keyboard.");
        } else if (EFI_ERROR(Status)) {
            return Status;
        }

        ERRCHECK_UEFI(ST->ConIn->Reset, 2, ST->ConIn, FALSE);

        if (L'\x0A' == InputKey.UnicodeChar || L'\x0D' == InputKey.UnicodeChar) {
            break;
        } else if (L'\x08' == InputKey.UnicodeChar) {
            InputBuffer[InputLength] = 0;
            if (InputLength > 0) {
                InputLength--;
            }
        } else if (L'\x20' <= InputKey.UnicodeChar
                    && InputKey.UnicodeChar <= L'\x7E'
                    && InputLength < MaxLength) {
            InputBuffer[InputLength] = InputKey.UnicodeChar;
            InputLength++;
        }
    } while (TRUE);

    InputBuffer[MaxLength] = '\0';
    *ResultingBufferLength = InputLength * sizeof(CHAR16);

    PrintHook(L"\r\n");
    return Status;
}


EFI_STATUS
EFIAPI
Confirm(IN CONST CHAR16 *Prompt,
        IN CONST BOOLEAN IsDefaultConfirm,
        IN CONST PRINT_HOOK PrintHook)
{
    EFI_STATUS Status = EFI_SUCCESS;

    CHAR16 *FullPrompt = NULL;
    CHAR16 *ConfirmationIndicators = NULL;

    CHAR16 UserInput[2] = {0};
    UINT8 UserInputLength = 0;

    if (IsDefaultConfirm) {
        ConfirmationIndicators = L" [Y/n]  ";
    } else {
        ConfirmationIndicators = L" [y/N]  ";
    }

    FullPrompt = StrDuplicate(Prompt);
    StrCat(FullPrompt, ConfirmationIndicators);

    ERRCHECK(
        ReadChar16KeyboardInput(FullPrompt,
                                UserInput,
                                &UserInputLength,
                                FALSE,
                                1,
                                PrintHook)
    );

    FreePool(FullPrompt);

    if (L'Y' == UserInput[0] || L'y' == UserInput[0]) {
        return EFI_SUCCESS;
    } else if (L'N' == UserInput[0] || L'n' == UserInput[0]) {
        return EFI_ABORTED;
    } else if (L'\0' == UserInput[0]) {
        return IsDefaultConfirm ? EFI_SUCCESS : EFI_ABORTED;
    } else {
        return EFI_ABORTED;
    }
}
