#ifndef MFTAH_UEFI_H
#define MFTAH_UEFI_H

#include "../../gnu-efi/inc/efi.h"
#include "../../gnu-efi/inc/efilib.h"

#include "../../MFTAH/src/include/mftah.h"


/* Semantic versioning in case we want it. */
#define MFTAH_UEFI_VERSION_MAJOR 1
#define MFTAH_UEFI_VERSION_MINOR 0
#define MFTAH_UEFI_VERSION_PATCH 7

#undef _STRINGIFY
#undef STRINGIFY
#define _STRINGIFY(x) L###x
#define STRINGIFY(x) _STRINGIFY(x)
#define MFTAH_UEFI_VERSION \
    STRINGIFY(MFTAH_UEFI_VERSION_MAJOR) L"." \
    STRINGIFY(MFTAH_UEFI_VERSION_MINOR) L"." \
    STRINGIFY(MFTAH_UEFI_VERSION_PATCH)

// TODO: Move these up into 'mftah.h'
#define CHAR        char
#define INLINE      inline
#define c8          (CHAR8)
#define c8p         (CHAR8 *)


#define MAX(x,y) \
    (((x) >= (y)) ? (x) : (y))
#define MIN(x,y) \
    (((x) <= (y)) ? (x) : (y))


/* Various IDs for ACPI entries inserted at runtime. */
#define MFTAH_CREATOR_ID    { 'X', 'M', 'I', 'T' }   /* Creator: 'XMIT' */
#define MFTAH_OEM_TABLE_ID  { 'M', 'F', 'T', 'A', 'H', 'N', 'V', 'D' }
#define MFTAH_OEM_ID        { 'M', 'F', 'T', 'A', 'H', ' ' }

/* The GUID that represents the developer (in this case, my personal site). */
#define XMIT_XYZ_VENDOR_GUID \
    { 0xf1338329, 0xb42f, 0x4d88, \
    { 0xd6, 0x3a, 0x96, 0xd4, 0x44, 0x99, 0xbe, 0x5b }}

/* The maximum amount of payloads discoverable by this software. */
#define MFTAH_MAX_PAYLOADS  32

/* The maximum length of the password buffer. */
#define MFTAH_MAX_PW_LEN    32

/* The chunk sizes at which the ramdisk is loaded. */
#define MFTAH_RAMDISK_LOAD_BLOCK_SIZE   (1 << 16)

/* Colors used by the application. */
#define MFTAH_COLOR_DEFAULT     (EFI_WHITE          | EFI_BACKGROUND_BLUE )
#define MFTAH_COLOR_ASCII_ART   (EFI_LIGHTMAGENTA   | EFI_BACKGROUND_BLUE )
#define MFTAH_COLOR_DEBUG       (EFI_CYAN           | EFI_BACKGROUND_BLACK)
#define MFTAH_COLOR_WARNING     (EFI_YELLOW         | EFI_BACKGROUND_BLACK)
#define MFTAH_COLOR_DANGER      (EFI_LIGHTMAGENTA   | EFI_BACKGROUND_BLACK)
#define MFTAH_COLOR_PANIC       (EFI_LIGHTRED       | EFI_BACKGROUND_BLACK)


/* Any custom EFI response codes are created below as needed. */
#define EFI_INVALID_PASSWORD        EFIERR(123)


/* Swaps the bute ordering of a 32-bit value. */
#define EFI_SWAP_ENDIAN_32(x) \
    ((((x) & 0xFF) << 24) | (((x) & 0xFF00) << 8) | (((x) & 0xFF0000) >> 8) | (((x) & 0xFF000000) >> 24))


/* "Error check" macro. Returns the EFI_STATUS if it's not EFI_SUCCESS. */
#define ERRCHECK(x) \
    { \
        Status = (x); \
        if (EFI_ERROR(Status)) { return Status; } \
    }

/* Quick conversion of UEFI time slices (100ns) to milliseconds. */
#define EFI_MILLISECONDS_TO_100NS(x)     (x * 10 * 1000)
/* Convert seconds to microseconds. */
#define EFI_SECONDS_TO_MICROSECONDS(x)     (x * 1000 * 1000)

/* Macro for setting console colors quickly. */
#define EFI_COLOR(x) \
    ST->ConOut->SetAttribute(ST->ConOut, (x));

/* Generic print macro, in case it ever needs to change later. */
#define PRINT(x, ...) \
    Print(L##x, ##__VA_ARGS__);
/* Same here. */
#define PRINTLN(x, ...) \
    Print(L##x L"\r\n", ##__VA_ARGS__);
#define VARPRINT(x) \
    Print((x));
#define VARPRINT8(x) \
    { \
        CHAR8 *x##_as16 = AsciiStrToUnicode(x); \
        if (NULL != x##_as16) { \
            Print(x##_as16); \
            FreePool(x##_as16); \
        } \
    }
#define VARPRINTLN(x) \
    { VARPRINT(x); Print(L"\r\n"); }
#define VARPRINTLN8(x) \
    { VARPRINT8(x); Print(L"\r\n"); }

/* Warning wrapper macros. */
#define EFI_WARNING(x, ...) \
    { EFI_COLOR(MFTAH_COLOR_WARNING); PRINT(x, ##__VA_ARGS__); EFI_COLOR(MFTAH_COLOR_DEFAULT); }
#define EFI_WARNINGLN(x, ...) \
    { EFI_WARNING(x, ##__VA_ARGS__); PRINTLN("\r\n"); }

/* Danger wrapper macros. */
#define EFI_DANGER(x, ...) \
    { EFI_COLOR(MFTAH_COLOR_DANGER); PRINT(x, ##__VA_ARGS__); EFI_COLOR(MFTAH_COLOR_DEFAULT); }
#define EFI_DANGERLN(x, ...) \
    { EFI_DANGER(x, ##__VA_ARGS__); PRINTLN("\r\n"); }


#if EFI_DEBUG==1
/* Debug-only. Prints debugging information when enabled by compiler flag. */
#   define DPRINT(x, ...) \
    { \
        EFI_COLOR(MFTAH_COLOR_DEBUG); \
        Print(L"DEBUG:  " L##x, ##__VA_ARGS__); \
        EFI_COLOR(MFTAH_COLOR_DEFAULT); \
    }
#   define DPRINTLN(x, ...) \
    { \
        EFI_COLOR(MFTAH_COLOR_DEBUG); \
        Print(L"DEBUG:  " L##x L"\r\n", ##__VA_ARGS__); \
        EFI_COLOR(MFTAH_COLOR_DEFAULT); \
    }
/* Debug-only. Dumps raw memory details when enabled by compiler flag. */
#   define MEMDUMP(ptr, len) \
    { \
        EFI_COLOR(MFTAH_COLOR_DEBUG); \
        for (int i = 0; i < (len); ++i) { \
        Print(L"%02x%c", *((UINT8 *)(ptr)+i), !((i+1) % 16) ? '\n' : ' '); \
        } \
        if (!(len % 16)) Print(L"\r\n"); \
        EFI_COLOR(MFTAH_COLOR_DEFAULT); \
    }
#else
#   define DPRINT(x, ...)
#   define DPRINTLN(x, ...)
#   define MEMDUMP(ptr, len)
#endif   /* #if EFI_DEBUG==1 */


/* pls don't change panic vars ok thx */
STATIC CHAR16 *GlobalPanicString = NULL;
STATIC CHAR16 GlobalPanicBuffer[512] = {'P', 'A', 'N', 'I', 'C', '!', ' ', 0};
STATIC UINT16 GlobalPanicCursor = 7;

#define HALT \
    { while (TRUE) BS->Stall(EFI_SECONDS_TO_MICROSECONDS(60)); }

#define PANIC(x) \
    { \
        GlobalPanicString = L##x; \
        EFI_COLOR(MFTAH_COLOR_PANIC); \
        do { GlobalPanicBuffer[GlobalPanicCursor] = GlobalPanicString[GlobalPanicCursor - 7]; } \
            while (++GlobalPanicCursor < 511 && '\0' != GlobalPanicString[GlobalPanicCursor - 7]); \
        GlobalPanicBuffer[GlobalPanicCursor] = '\0'; \
        Print(GlobalPanicBuffer); \
        Print(L"  Exit Code: '%d'", Status); \
        HALT; \
    }

#define ABORT(x) PANIC(x)


/** 
 * A more friendly alias for the MFTAH progress hook type.
 */
typedef
mftah_fp__progress_hook_t
PROGRESS_UPDATE_HOOK;


/* A required configuration file that specifies how to load or select a
    target MFTAH payload to chainload. The config file format is described
    in the Config driver. */
STATIC CONST CHAR16 *DefaultConfigFileName = L"\\EFI\\BOOT\\MFTAH.CFG";


/* The Image Handle from EFI_MAIN, in case it's ever used in other modules (hint: it is). */
EXTERN EFI_HANDLE ENTRY_HANDLE;

/* The vendor GUID for XMIT XYZ. */
EXTERN EFI_GUID gXmitVendorGuid;


/* Lovely ASCII art banner. */
STATIC CONST
CHAR16 *
MftahAsciiArt =
    L"\r\n"
    L"        M. F. T. A. H.\r\n"
    L"                                           ████████\r\n"
    L"        Media For Tamper-Averse Humans  ███        ████\r\n"
    L"     ███████████████████████████████████               ██\r\n"
    L"    ██                                                   █\r\n"
    L"   ██              █                                      █\r\n"
    L"  ██               █  █ █   █                              █\r\n"
    L"   ██              █                               ███     █\r\n"
    L"     █████████████ █   █   ███   ███               ███     █\r\n"
    L"            █████  █   █  █   █ █   █                      █\r\n"
    L"         █████     ████████████████████                  ██\r\n"
    L"       ██                              ███             ██\r\n"
    L"        ██                   ████         ████      ███\r\n"
    L"          ███████████████████                 ██████\r\n"
    L"\r\n"
;



#endif   /* MFTAH_UEFI_H */
