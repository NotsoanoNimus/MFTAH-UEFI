#ifndef MFTAH_TEXT_H
#define MFTAH_TEXT_H

#include "menu_structs.h"



/**
 * Initializes an appropriate text mode for the application, if configured or by
 *  default. Attempts to pre-select the highest resolution console mode available.
 * 
 * @returns Whether initialization succeeded or encountered a fatal error.
 */
EFI_STATUS
TextModeInit(IN CONFIGURATION *c);


/**
 * Destruct any dynamic TEXT mode constructs.
 */
EFI_STATUS
TextModeDestroy(VOID);


/**
 * Populate the given menu renderer with text-mode hooks.
 * 
 * @param[out]  Renderer    A menu renderer object which can call into private TEXT mode functions.
 * 
 * @retval  EFI_SUCCESS             The `Renderer` was hooked successfully.
 * @retval  EFI_INVALID_PARAMETER   The `Renderer` parameter is NULL.
 */
EFI_STATUS
TextModePopulateMenu(OUT EFI_MENU_RENDERER_PROTOCOL *Renderer);



#endif   /* MFTAH_TEXT_H */
