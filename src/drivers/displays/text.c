#include "../../include/drivers/displays/text.h"



EFI_STATUS
TextModeInit(IN CONFIGURATION *c)
{
    return EFI_SUCCESS;
}


EFI_STATUS
TextModeDestroy(VOID)
{
}


EFI_STATUS
TextModePopulateMenu(OUT EFI_MENU_RENDERER_PROTOCOL *Renderer)
{
    return EFI_SUCCESS;
}