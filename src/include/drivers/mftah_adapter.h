#ifndef MFTAH_ADAPTER_H
#define MFTAH_ADAPTER_H

#include "../mftah_uefi.h"



/**
 * Initialize the MFTAH protocol singleton for the boot runtime.
 * 
 * @retval EFI_SUCCESS      The driver was successfully initialized.
 * @retval EFI_NOT_READY    The driver failed to initialize.
 */
EFI_STATUS
EFIAPI
MftahInit(VOID);


/**
 * Return the handle to the loaded MFTAH singleton instance.
 * 
 * @returns A handle to the protocol instance.
 */
mftah_protocol_t *
EFIAPI
MftahGetInstance(VOID);



#endif   /* MFTAH_ADAPTER_H */
