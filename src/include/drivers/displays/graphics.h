#ifndef MFTAH_GRAPHICS_H
#define MFTAH_GRAPHICS_H

#include "menu_structs.h"
#include "fb.h"



typedef
enum {
    OverlayHidden = 0,
    OverlayPrompt,
    OverlayInfo,
    OverlayWarning,
    OverlayMax
} OVERLAY_STATE;

typedef
struct {
    UINT8           Zoom;
    BOOLEAN         CompressedView;
    UINTN           LeftPanelHeight;
    UINTN           RightPanelHeight;
    FB_VERTEX       BannerPosition;
    FB_VERTEX       TimeoutsPosition;
    UINTN           NormalTimeout;
    UINTN           MaxTimeout;
    OVERLAY_STATE   OverlayState;
    BOUNDED_SHAPE   *OverlayLayer;
    BOUNDED_SHAPE   *TimeoutsLayer;
    CHAR8           NormalTimeoutText[128];
    CHAR8           MaxTimeoutText[128];
} _PACKED
GRAPHICS_CONTEXT;


/**
 * Initializes an appropriate graphics mode for the application, if configured.
 *  Attempts to pre-select the highest resolution video mode available, since many
 *  rendering methods use ratios.
 * 
 * @returns Whether initialization succeeded or encountered a fatal error.
 */
EFI_STATUS
GraphicsInit(CONFIGURATION *c);


/**
 * Destruct any dynamic GRAPHICAL mode objects.
 */
EFI_STATUS
GraphicsDestroy(BOOLEAN PreserveFramebuffer);


/**
 * Populate the given menu renderer with graphics-mode hooks.
 * 
 * @param[out]  Renderer    A menu renderer object which can call into private GRAPHICAL mode functions.
 * 
 * @retval  EFI_SUCCESS             The `Renderer` was hooked successfully.
 * @retval  EFI_INVALID_PARAMETER   The `Renderer` parameter is NULL.
 */
EFI_STATUS
GraphicsModePopulateMenu(OUT EFI_MENU_RENDERER_PROTOCOL *Renderer);


/**
 * Destruct all 'Renderable' menu components. This allows the graphical mode to still be used,
 *  while also not consuming a ton of memory with its compositing windows.
 */
VOID
GraphicsDestroyMenu(VOID);


// TODO: Shouldn't these just be private/static?
VOID
GPrint(
    CHAR *restrict  Str,
    BOUNDED_SHAPE   *ObjectBltBuffer,
    UINTN           X,
    UINTN           Y,
    UINT32          Foreground,
    UINT32          Background,
    BOOLEAN         Wrap,
    UINT8           FontScale
);


EFI_STATUS
LoadingAnimationLoop(
    BOUNDED_SHAPE       *Underlay,
    UINT32              ColorARGB,
    VOLATILE BOOLEAN    *Stall
);


#endif   /* MFTAH_GRAPHICS_H */
