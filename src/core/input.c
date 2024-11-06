#include "../include/core/input.h"



STATIC EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *STIEP = NULL;



EFI_STATUS
ReadKey(OUT EFI_KEY_DATA *KeyData,
        IN UINTN TimeoutMilliseconds)
{
    EFI_STATUS Status = EFI_SUCCESS;
    EFI_GUID gEfiSimpleTextInputExProtocolGuid = EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL_GUID;

    /* Helpful but also janky af: https://github.com/tianocore-docs/edk2-UefiDriverWritersGuide/blob/master/5_uefi_services/52_services_that_uefi_drivers_rarely_use/5210_installconfigurationtable.md#52101-waitforevent */
    EFI_EVENT InputEvents[2] = {0};
    UINTN FiredEventIndex = 0;

    if (TimeoutMilliseconds > 0) {
        /* Create a timeout event. */
        Status = uefi_call_wrapper(BS->CreateEvent, 5,
                                   EVT_TIMER,
                                   TPL_NOTIFY,
                                   NULL,
                                   NULL,
                                   &InputEvents[1]);
        if (EFI_ERROR(Status)) {
            EFI_DANGERLN("Failed to create input timeout event (%u).", Status);
            return Status;
        }

        Status = uefi_call_wrapper(BS->SetTimer, 3,
                                   InputEvents[1],
                                   TimerRelative,
                                   EFI_100NS_TO_MILLISECONDS(TimeoutMilliseconds));
        if (EFI_ERROR(Status)) {
            uefi_call_wrapper(BS->CloseEvent, 1, InputEvents[1]);
            EFI_DANGERLN("Failed to start input timeout timer (%u).", Status);
            return Status;
        }
    }

    if (NULL == STIEP) {
        Status = uefi_call_wrapper(BS->LocateProtocol, 3,
                                   &gEfiSimpleTextInputExProtocolGuid,
                                   NULL,
                                   (VOID **)&STIEP);
        if (EFI_ERROR(Status) || NULL == STIEP) goto ReadKey__Fallback;
    }

    /* Call both resets here regardless of which is used. */
    uefi_call_wrapper(STIEP->Reset, 2, STIEP, FALSE);
    uefi_call_wrapper(ST->ConIn->Reset, 2, ST->ConIn, FALSE);

    do {
        InputEvents[0] = STIEP->WaitForKeyEx;

        Status = uefi_call_wrapper(BS->WaitForEvent, 3,
                                   (TimeoutMilliseconds > 0 ? 2 : 1),
                                   InputEvents,
                                   &FiredEventIndex);
        if (EFI_ERROR(Status)) {
            goto ReadKey__Fallback;
        }

        if (1 == FiredEventIndex && TimeoutMilliseconds > 0) {
            /* The input event timed out. */
            uefi_call_wrapper(BS->CloseEvent, 1, InputEvents[1]);
            return EFI_TIMEOUT;
        }

        Status = uefi_call_wrapper(STIEP->ReadKeyStrokeEx, 2, STIEP, KeyData);

        if (EFI_NOT_READY == Status) {
            /* No keystroke data is available. Move on. */
            continue;
        } else if (EFI_ERROR(Status)) {
            goto ReadKey__Fallback;
        }

        break;
    } while (TRUE);

    uefi_call_wrapper(STIEP->Reset, 2, STIEP, FALSE);
    goto ReadKey__Success;

ReadKey__Fallback:
    SetMem(KeyData, sizeof(EFI_KEY_DATA), 0x00);
    uefi_call_wrapper(ST->ConIn->Reset, 2, ST->ConIn, FALSE);

    while (TRUE) {
        InputEvents[0] = ST->ConIn->WaitForKey;

        Status = uefi_call_wrapper(BS->WaitForEvent, 3,
                                  (TimeoutMilliseconds > 0 ? 2 : 1),
                                  InputEvents,
                                  &FiredEventIndex);
        if (EFI_ERROR(Status)) {
            EFI_DANGERLN("Error awaiting fallback `WaitForKey` event (%u).", Status);
            return Status;
        }

        if (1 == FiredEventIndex && TimeoutMilliseconds > 0) {
            /* The input event timed out. */
            uefi_call_wrapper(BS->CloseEvent, 1, InputEvents[1]);
            return EFI_TIMEOUT;
        }

        Status = uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, &(KeyData->Key));

        if (EFI_NOT_READY == Status) {
            /* No keystroke data is available. Move on. */
            continue;
        } else if (EFI_ERROR(Status)) {
            uefi_call_wrapper(BS->CloseEvent, 1, InputEvents[1]);
            EFI_DANGERLN("Call to fallback `ReadKeyStroke` returned error code %u.", Status);
            return Status;
        }

        break;
    }

    /* Indicate that the fallback method was used to gather input. */
    KeyData->KeyState.KeyShiftState = READKEY_FALLBACK_INDICATOR;
    uefi_call_wrapper(ST->ConIn->Reset, 2, ST->ConIn, FALSE);

ReadKey__Success:
    if (TimeoutMilliseconds > 0 && 0 != InputEvents[1]) {
        uefi_call_wrapper(BS->CloseEvent, 1, InputEvents[1]);
    }

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
