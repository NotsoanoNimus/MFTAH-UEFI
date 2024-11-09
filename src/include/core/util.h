#ifndef MFTAH_UTIL_H
#define MFTAH_UTIL_H

#include "../mftah_uefi.h"



/**
 * Get the size of the file from the given handle.
 *
 * @param[in] FileHandle    A pointer to a valid system file handle.
 *
 * @returns The length of the file given by the handle; 0 on failure.
 */
UINT64
EFIAPI
FileSize(
    IN EFI_FILE_PROTOCOL *FileHandle
);


/**
 * Attempt to read the contents of a file on the drive relative to
 *  the given image handle.
 * 
 * @param[in]   BaseImageHandle The handle of an image whose drive should be used when loading.
 * @param[in]   Filename        A full path to a file on-disk.
 * @param[in]   Offset          Starting offset into the file to read from.
 * @param[out]  OutputBuffer    Returns a pointer to the read buffer. NULL on error.
 * @param[out]  LoadedFileSize  Returns the length of the read file.
 * @param[in]   HandleIsLoadedImage Whether the provided handle should be parsed as a Loaded Image handle.
 * @param[in]   AllocatedMemoryType The EFI memory type to use when reserving the buffer.
 * @param[in]   RoundToBlockSize    Rounds up the size of the allocated buffer to a nearest multiple of this value, if not 0.
 * @param[in]   ExtraEndAllocation  Any additional allocation to make onto the end of the buffer.
 * @param[in]   ProgressHook    An optional function that can report occasional progress details.
 * 
 * @retval  EFI_SUCCESS     Successfully read the file and returned details.
 * @retval  EFI_NOT_FOUND   The file was not found relative to the handle, or could not be read.
 * @retval  EFI_INVALID_PARMETER    One or more of the provided parameters were incorrect.
 * @retval  EFI_OUT_OF_RESOURCES    No more resources were available to read the file.
 * @retval  EFI_END_OF_FILE The size of the read file is 0 or reading preemptively stopped.
 */
// TODO! Rearrange params ordering
EFI_STATUS
EFIAPI
ReadFile(
    IN EFI_HANDLE           BaseImageHandle,
    IN CONST CHAR16         *Filename,
    IN UINTN                Offset,
    OUT UINT8               **OutputBuffer,
    OUT UINTN               *LoadedFileSize,
    IN BOOLEAN              HandleIsLoadedImage,
    IN EFI_MEMORY_TYPE      AllocatedMemoryType,
    IN UINTN                RoundToBlockSize,
    IN UINTN                ExtraEndAllocation,
    IN PROGRESS_UPDATE_HOOK ProgressHook        OPTIONAL
);


/**
 * Attempt to locate a simple filesystem by its label name.
 * 
 * @param[in]   VolumeName      The exact name of the volume to search for.
 * @param[out]  TargetHandle    On success, set to the first matching handle corresponding with the given volume name.
 * 
 * @retval  EFI_SUCCESS     The handle was found and set in `TargetHandle`.
 * @retval  EFI_NOT_FOUND   No available volume handle could be found with the given name.
 * @retval  EFI_INVALID_PARAMETER   An invalid parameter was supplied to the function.
 * @retval  EFI_OUT_OF_RESOURCES    Ran out of system resources to search with.
 */
EFI_STATUS
GetFileSystemHandleByVolumeName(
    IN CHAR16       *VolumeName,
    OUT EFI_HANDLE  *TargetHandle
);


/**
 * Securely wipe a buffer's data by passing it several times with alternating bit patterns.
 * 
 * @param[in]   Buffer  The buffer to clear.
 * @param[in]   Length  The length of the buffer.
 * 
 * @returns Nothing.
 */
VOID
EFIAPI
__attribute__((optnone))
SecureWipe(
    IN  VOID    *Buffer,
    IN  UINTN   Length
);


/**
 * Generic shutdown function. Panics if a shutdown could not be completed.
 * 
 * @param[in]   Reason  Set to EFI_SUCCESS to indicate a normal shutdown. \
 *                       Otherwise, provide a status code to the ResetSystem call.
 * 
 * @returns Nothing. This is supposed to terminate the application (and stop the system).
 */
VOID
EFIAPI
Shutdown(
    IN CONST EFI_STATUS Reason
);


/**
 * Generic reboot function. Panics if the reboot could not be completed.
 * 
 * @param[in]   Reason  Set to EFI_SUCCESS to indicate a normal shutdown. \
 *                       Otherwise, provide a status code to the ResetSystem call.
 * 
 * @returns Nothing. This is supposed to terminate the application (and stop the system).
 */
VOID
EFIAPI
Reboot(
    IN CONST EFI_STATUS Reason
);


/**
 * Gets whether a string is specifically numeric. Allows negative numbers.
 * 
 * @param[in]   String  The string to check.
 * 
 * @returns Whether the string consists of only numeric characters.
 */
BOOLEAN
EFIAPI
AsciiIsNumeric(IN CONST CHAR8 *String);


/**
 * ATOI implementation for ASCII (CHAR8) strings. Does not
 *  currently work with negative numbers.
 * 
 * @param[in]   String  The numeric string to convert.
 * 
 * @returns A number from the given string. -1 on error.
 */
UINTN
EFIAPI
AsciiAtoi(IN CONST CHAR8 *String);


/**
 * Wrapper function for AsciiVSPrint.
 */
VOID
AsciiSPrint(
    OUT CHAR8 *Into,            
    IN UINTN Length,            
    IN CONST CHAR8 *Format,            
    ...
);


/**
 * Attempt to put all characters from the source buffer into a
 *  pre-allocated destination buffer.
 * 
 * @returns A newly-allocated wide-char string, or NULL on error/empty source string.
 */
CHAR16 *
AsciiStrToUnicode(
    IN CHAR8    *Src
);


#endif   /* MFTAH_UTIL_H */