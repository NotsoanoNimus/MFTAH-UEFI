#ifndef MFTAH_DISCOVER_H
#define MFTAH_DISCOVER_H

#include "../mftah_uefi.h"



/**
 * Discover the set of payloads located relative to the given
 *  Image Handle. This is usually from the UEFI entry point, and
 *  thus payloads are searched on the boot drive itself. All valid
 *  payloads are suffixed by the .GDEI blob extension.
 * 
 * @param[in]  BaseImageHandle      The relative image handle to use for discovery.
 * @param[out] LoadedPayloadHandles Returned pointer to an allocated array of open payload handles.
 *                                  NULL on error or EFI_NO_PAYLOAD_FOUND.
 * @param[out] LoadedHandlesCount   Returned handle count in the allocated pool. 0 on error or none.
 * @param[out] LoadedLoaderHash     An optional input buffer where the hash of the MFTAH loader is stored.
 * 
 * @retval EFI_NO_PAYLOAD_FOUND     There was no payload discovered. Error.
 * @retval EFI_SINGLE_PAYLOAD_FOUND There was only one payload discovered.
 * @retval EFI_MULTI_PAYLOAD_FOUND  Multiple payloads were found. Choose.
 * @retval Other                    Some other EFI error occurred.
 */
EFI_STATUS
EFIAPI
DiscoverPayloads(
    IN  EFI_HANDLE              BaseImageHandle,
    OUT EFI_FILE_PROTOCOL       ***LoadedPayloadHandles,
    OUT UINTN                   *LoadedHandlesCount,
    OUT UINT8                   *LoadedLoaderHash               OPTIONAL
);

VOID
EFIAPI
SelectPayload(
    IN OUT EFI_FILE_PROTOCOL    **PayloadFileHandle,
    OUT UINT64                  *PayloadFileStartingPosition,
    OUT UINT8                   *LoadedLoaderHash               OPTIONAL
);



#endif   /* MFTAH_DISCOVER_H */
