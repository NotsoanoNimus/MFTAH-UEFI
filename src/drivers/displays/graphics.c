#include "../../include/drivers/displays/graphics.h"

#include "../../include/core/util.h"
#include "../../include/loaders/loader.h"



STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL *GOP = NULL;
STATIC GRAPHICS_CONTEXT *GraphicsContext = NULL;


#define LAYOUT_G_MARGIN_LEFT                16
#define LAYOUT_G_MARGIN_RIGHT               16
#define LAYOUT_G_MARGIN_TOP                 16
#define LAYOUT_G_MARGIN_BOTTOM              16

#define LAYOUT_G_CENTER_HORIZ               (FB->Resolution.Width / 2)
#define LAYOUT_G_CENTER_VERT                (FB->Resolution.Height / 2)

#define LAYOUT_G_PANEL_PADDING_LEFT         8
#define LAYOUT_G_PANEL_PADDING_RIGHT        8
#define LAYOUT_G_PANEL_PADDING_TOP          16
#define LAYOUT_G_PANEL_PADDING_BOTTOM       32

#define LAYOUT_G_GLYPH_HEIGHT               (GraphicsContext->Zoom * FB->BaseGlyphSize.Height)
#define LAYOUT_G_GLYPH_WIDTH                (GraphicsContext->Zoom * FB->BaseGlyphSize.Width)

#define LAYOUT_MENU_ITEM_HEIGHT             (LAYOUT_G_GLYPH_HEIGHT + 2)
#define LAYOUT_G_TEXT_LEFT_PADDING          5
#define LAYOUT_G_TEXT_TOP_PADDING           1

#define LAYOUT_G_TITLE_HEIGHT               (2 * LAYOUT_G_GLYPH_HEIGHT)
#define LAYOUT_G_HEADER_HEIGHT              (LAYOUT_G_TITLE_HEIGHT + (2 * LAYOUT_G_PANEL_PADDING_TOP))
#define LAYOUT_G_PANEL_LEFT_MIN_HEIGHT      (LAYOUT_G_HEADER_HEIGHT + LAYOUT_G_PANEL_PADDING_BOTTOM + (CONFIG_MAX_CHAINS * LAYOUT_MENU_ITEM_HEIGHT))

#define LAYOUT_G_PANEL_RIGHT_MAX_HEIGHT     (12 * LAYOUT_G_GLYPH_HEIGHT)

#define LAYOUT_G_BANNER_MARGIN_TOP          12
#define LAYOUT_G_BANNER_HEIGHT              (8 * LAYOUT_G_GLYPH_HEIGHT)

#define LAYOUT_G_TIMEOUTS_MARGIN_TOP        12
#define LAYOUT_G_TIMEOUTS_HEIGHT            (3 * LAYOUT_G_GLYPH_HEIGHT)

#define LAYOUT_G_OVERLAY_WIDTH              (MAX(FB->Resolution.Width / 4, 250))
#define LAYOUT_G_OVERLAY_HEIGHT             (5 * LAYOUT_G_GLYPH_HEIGHT)
#define LAYOUT_G_OVERLAY_X                  (LAYOUT_G_CENTER_HORIZ - (LAYOUT_G_OVERLAY_WIDTH / 2))
#define LAYOUT_G_OVERLAY_Y                  (MAX(FB->Resolution.Height / 4, 128))


#define PANIC_GUI(Message) \
    GPrint(Message, FB->BLT, 20, 20, 0xFFFF0000, 0, TRUE, 2); \
    FB->Flush(FB);


#define GRAPHICS_MAX_RENDERABLES 32
STATIC BOUNDED_SHAPE *Renderables[GRAPHICS_MAX_RENDERABLES] = {0};
STATIC UINTN RenderablesLength = 0;


#define DECL_DRAW_FUNC(Name) \
    STATIC EFIAPI VOID \
    GraphicsDraw__##Name( \
        BOUNDED_SHAPE *This, CONFIGURATION *c, MENU_STATE *m, VOID *e)

DECL_DRAW_FUNC(Underlay);
DECL_DRAW_FUNC(LeftPanel);
DECL_DRAW_FUNC(RightPanel);
DECL_DRAW_FUNC(Banner);
DECL_DRAW_FUNC(Timeouts);
DECL_DRAW_FUNC(MenuItem);



STATIC
EFIAPI
VOID
ClearScreen(IN UINT32 Color)
{
    FB->ClearScreen(FB, Color);
    FB->Flush(FB);
}


STATIC
EFIAPI
CHAR8 *
InputPopup(IN CHAR8 *Prompt,
           IN UINTN MaxLength,
           IN BOOLEAN IsHidden)
{
    // TODO
    return NULL;
}


STATIC
EFIAPI
BOOLEAN
Confirmation(IN CHAR8 *Prompt)
{
    // TODO
    return FALSE;
}


STATIC
EFIAPI
VOID
InnerPopup(IN CHAR8 *Message,
           IN COLOR_PAIR Colors,
           IN BOOLEAN WaitsForEnter)
{
    /* Display a notification. The TEXT mode can dynamically determine
        where and how to place it based on the "resolution" of LCB. This
        must pause for the ENTER key for the user to 'press OK'. */
}


STATIC
EFIAPI
VOID
Popup(IN CHAR8 *Message)
{
    /* Proxy to inner method with a specific coloring. */
    // TODO: create color settings for these??
    COLOR_PAIR c = { .Background = 0xFF0033AA, .Foreground = 0xFFFFFFFF };

    return InnerPopup(Message, c, TRUE);
}


STATIC
EFIAPI
VOID
WarningPopup(IN CHAR8 *Message)
{
    /* Just proxy this to 'popup' with a different color. */
    COLOR_PAIR c = { .Background = 0xFFDD0033, .Foreground = 0xFF00AAAA };

    return InnerPopup(Message, c, TRUE);
}


STATIC
EFIAPI
VOID
DrawMenu(IN CONFIGURATION *c,
         IN MENU_STATE *m)
{
    FB_VERTEX Origin = {0};

    FB->ClearScreen(FB, c->Colors.Background);

    /* The Z-index value walks upwards from 0 to its max value (inclusive). */
    for (UINTN z = 0; z <= FB_Z_INDEX_MAX; ++z) {
        for (UINTN i = 0; i < RenderablesLength; ++i) {
            if (NULL == Renderables[i] || Renderables[i]->ZIndex != z) continue;

            if (
                (Renderables[i]->Position.X + Renderables[i]->Dimensions.Width) > FB->BLT->Dimensions.Width
                || (Renderables[i]->Position.Y + Renderables[i]->Dimensions.Height) > FB->BLT->Dimensions.Height
            ) continue;   /* do not draw invalid BLTs (out-of-bounds) */

            /* If this is the overlay buffer, do not draw it if its state is 'Hidden'. */
            if (GraphicsContext->OverlayLayer == Renderables[i]) {
                if (GraphicsContext->OverlayState == OverlayHidden) continue;
            }

            /* Invoke the renderable's Draw method. */
            if (NULL != Renderables[i]->Draw) {
                Renderables[i]->Draw(Renderables[i], c, m, (VOID *)&i);
            }

            /* Draw the BLT buffer into the higher/main BLT buffer, row by row. */
            FB->BltToBlt(FB,
                         FB->BLT,
                         Renderables[i],
                         Renderables[i]->Position,
                         Origin,
                         Renderables[i]->Dimensions);
        }
    }

    /* Flush the entire display to video. Doing this only once is highly efficient. */
    FB->Flush(FB);
}


STATIC
EFIAPI
EFI_STATUS
InitMenu(IN CONFIGURATION *c,
         IN MENU_STATE *State)
{
    /* Initialize all primary menu shapes according to LCB dimensions. */
    if (NULL == c || NULL == State) return EFI_INVALID_PARAMETER;

    EFI_STATUS Status = EFI_SUCCESS;
    RenderablesLength = 0;

    /* Get some initial measurements relative to the current framebuffer, in order
        to determine a good layout. */
    GraphicsContext->CompressedView =
        (
            LAYOUT_G_MARGIN_TOP + LAYOUT_G_PANEL_LEFT_MIN_HEIGHT + LAYOUT_G_BANNER_MARGIN_TOP
                + LAYOUT_G_BANNER_HEIGHT + LAYOUT_G_TIMEOUTS_MARGIN_TOP + LAYOUT_G_TIMEOUTS_HEIGHT
                + LAYOUT_G_MARGIN_BOTTOM
        ) > FB->Resolution.Height;

    GraphicsContext->LeftPanelHeight = GraphicsContext->CompressedView
        ? (FB->Resolution.Height - LAYOUT_G_MARGIN_TOP - LAYOUT_G_MARGIN_BOTTOM)
        : LAYOUT_G_PANEL_LEFT_MIN_HEIGHT;

    GraphicsContext->RightPanelHeight = GraphicsContext->CompressedView
        ? LAYOUT_G_PANEL_RIGHT_MAX_HEIGHT
        : (FB->Resolution.Height - LAYOUT_G_MARGIN_TOP - LAYOUT_G_MARGIN_BOTTOM);

    GraphicsContext->TimeoutsPosition.X =   /* same X as banner */
    GraphicsContext->BannerPosition.X = GraphicsContext->CompressedView
        ? (LAYOUT_G_CENTER_HORIZ + LAYOUT_G_MARGIN_LEFT)
        : (LAYOUT_G_MARGIN_LEFT);

    GraphicsContext->BannerPosition.Y = GraphicsContext->CompressedView
        ? (LAYOUT_G_MARGIN_TOP + LAYOUT_G_PANEL_RIGHT_MAX_HEIGHT + LAYOUT_G_BANNER_MARGIN_TOP)
        : (LAYOUT_G_MARGIN_TOP + LAYOUT_G_PANEL_LEFT_MIN_HEIGHT + LAYOUT_G_BANNER_MARGIN_TOP);

    /* The timeouts panel always glues itself to the bottom of the banner panel. */
    GraphicsContext->TimeoutsPosition.Y
        = (GraphicsContext->BannerPosition.Y + LAYOUT_G_BANNER_HEIGHT + LAYOUT_G_TIMEOUTS_MARGIN_TOP);

    /* Allocate each selectable item BLT for the list. */
    for (UINTN i = 1; i < (CONFIG_MAX_CHAINS + 1); ++i) {
        ERRCHECK(
            NewObjectBlt(LAYOUT_G_MARGIN_LEFT + LAYOUT_G_PANEL_PADDING_LEFT,
                         LAYOUT_G_MARGIN_TOP + LAYOUT_G_HEADER_HEIGHT + (i * LAYOUT_MENU_ITEM_HEIGHT),
                         (LAYOUT_G_CENTER_HORIZ - LAYOUT_G_MARGIN_LEFT - LAYOUT_G_MARGIN_RIGHT
                            - LAYOUT_G_PANEL_PADDING_LEFT - LAYOUT_G_PANEL_PADDING_RIGHT),
                         LAYOUT_MENU_ITEM_HEIGHT,
                         2,
                         &(Renderables[RenderablesLength]))
        );
        Renderables[RenderablesLength]->Draw = GraphicsDraw__MenuItem;
        ++RenderablesLength;
    }

    /* Allocate all child object BLT buffers. */
    /* Underlay */
    ERRCHECK(NewObjectBlt(0, 0, FB->Resolution.Width, FB->Resolution.Height, 1, &(Renderables[RenderablesLength])));
    Renderables[RenderablesLength]->Draw = GraphicsDraw__Underlay;
    ++RenderablesLength;

    /* Selection menu, left-hand side. */
    ERRCHECK(
        NewObjectBlt(LAYOUT_G_MARGIN_LEFT,
                     LAYOUT_G_MARGIN_TOP,
                     LAYOUT_G_CENTER_HORIZ - LAYOUT_G_MARGIN_LEFT - LAYOUT_G_MARGIN_RIGHT,
                     GraphicsContext->LeftPanelHeight,
                     1,
                     &(Renderables[RenderablesLength]))
    );
    Renderables[RenderablesLength]->Draw = GraphicsDraw__LeftPanel;
    ++RenderablesLength;

    /* Information panel, right-hand side. */
    ERRCHECK(
        NewObjectBlt(LAYOUT_G_CENTER_HORIZ + LAYOUT_G_MARGIN_LEFT,
                     LAYOUT_G_MARGIN_TOP,
                     LAYOUT_G_CENTER_HORIZ - LAYOUT_G_MARGIN_LEFT - LAYOUT_G_MARGIN_RIGHT,
                     GraphicsContext->RightPanelHeight,
                     1,
                     &(Renderables[RenderablesLength]))
    );
    Renderables[RenderablesLength]->Draw = GraphicsDraw__RightPanel;
    ++RenderablesLength;

    /* Banner panel. */
    ERRCHECK(
        NewObjectBlt(GraphicsContext->BannerPosition.X,
                     GraphicsContext->BannerPosition.Y,
                     LAYOUT_G_CENTER_HORIZ - LAYOUT_G_MARGIN_LEFT - LAYOUT_G_MARGIN_RIGHT,
                     LAYOUT_G_BANNER_HEIGHT,
                     2,
                     &(Renderables[RenderablesLength]))
    );
    Renderables[RenderablesLength]->Draw = GraphicsDraw__Banner;
    ++RenderablesLength;

    /* Timeouts panel. */
    ERRCHECK(
        NewObjectBlt(GraphicsContext->TimeoutsPosition.X,
                     GraphicsContext->TimeoutsPosition.Y,
                     LAYOUT_G_CENTER_HORIZ - LAYOUT_G_MARGIN_LEFT - LAYOUT_G_MARGIN_RIGHT,
                     LAYOUT_G_TIMEOUTS_HEIGHT,
                     2,
                     &(Renderables[RenderablesLength]))
    );
    Renderables[RenderablesLength]->Draw = GraphicsDraw__Timeouts;
    GraphicsContext->TimeoutsLayer = Renderables[RenderablesLength];
    ++RenderablesLength;

    /* Overlay */
    ERRCHECK(
        NewObjectBlt(LAYOUT_G_OVERLAY_X,
                     LAYOUT_G_OVERLAY_Y,
                     LAYOUT_G_OVERLAY_WIDTH,
                     LAYOUT_G_OVERLAY_HEIGHT,
                     FB_Z_INDEX_MAX,
                     &(Renderables[RenderablesLength]))
    );
    /* The overlay uses a special technique for popups. */
    GraphicsContext->OverlayLayer = Renderables[RenderablesLength];
    GraphicsContext->OverlayState = OverlayHidden;   /* hidden by default */
    ++RenderablesLength;

    /* Set the length of the components list manually. */
    for (UINTN i = 0; i < RenderablesLength; ++i)
        if (NULL == Renderables[i]) return EFI_OUT_OF_RESOURCES;

    return EFI_SUCCESS;
}


STATIC
EFIAPI
VOID
TimerTick(EFI_EVENT Event,
          VOID *Context)
{
    MENU_STATE *m = (MENU_STATE *)(((TIMER_CTX *)Context)->m);
    CONFIGURATION *c = (CONFIGURATION *)(((TIMER_CTX *)Context)->c);

    if (
        FALSE == m->KeyPressReceived
        && GraphicsContext->NormalTimeout > 0
        && m->MillisecondsElapsed >= GraphicsContext->NormalTimeout
    ) {
        /* A normal timeout has occurred. Signal to the menu handler. */
        uefi_call_wrapper(BS->CloseEvent, 1, Event);

        GraphicsContext->TimeoutsLayer->Draw(GraphicsContext->TimeoutsLayer, c, m, NULL);
        FB->RenderComponent(FB, GraphicsContext->TimeoutsLayer, TRUE);
        uefi_call_wrapper(BS->Stall, 1, 2000000);

        m->TimeoutOccurred = TRUE;
        return;
    }

    if (
        m->MillisecondsElapsed >= GraphicsContext->MaxTimeout
        && GraphicsContext->MaxTimeout > 0
    ) {
        /* THE MAX TIMEOUT IS REACHED. REDRAW THE BLT (it will now have a red message)
            AND SHUT DOWN THE DEVICE.*/
        /* First, cancel the event. */
        uefi_call_wrapper(BS->CloseEvent, 1, Event);

        GraphicsContext->TimeoutsLayer->Draw(GraphicsContext->TimeoutsLayer, c, m, NULL);
        FB->RenderComponent(FB, GraphicsContext->TimeoutsLayer, TRUE);
        uefi_call_wrapper(BS->Stall, 1, 2000000);

        Shutdown(EFI_TIMEOUT);
    }

    if (!(m->MillisecondsElapsed % 1000) && FALSE == m->PauseTickRenders) {
        CHAR8 *NormalTimeoutStr = "Choosing default selection in %u seconds.";
        CHAR8 *MaxTimeoutStr = "Global timeout in %u seconds.";

        /* I know I can get rid of the >0 stuff here, I just don't want to. */
        BOOLEAN NormalTimeoutActive = (
            m->MillisecondsElapsed < GraphicsContext->NormalTimeout
            && FALSE == m->KeyPressReceived
            && GraphicsContext->NormalTimeout > 0
        );
        BOOLEAN MaxTimeoutActive = (
            GraphicsContext->MaxTimeout > 0
            && m->MillisecondsElapsed < GraphicsContext->MaxTimeout
        );

        if (NormalTimeoutActive) {
            AsciiSPrint(GraphicsContext->NormalTimeoutText,
                        128,
                        NormalTimeoutStr,
                        (GraphicsContext->NormalTimeout - m->MillisecondsElapsed) / 1000);
        }

        if (MaxTimeoutActive) {
                AsciiSPrint(GraphicsContext->MaxTimeoutText,
                            128,
                            MaxTimeoutStr,
                            (GraphicsContext->MaxTimeout - m->MillisecondsElapsed) / 1000);
        }

        /* Draw the base state of the timeouts panel. */
        GraphicsContext->TimeoutsLayer->Draw(GraphicsContext->TimeoutsLayer, c, m, NULL);
        FB->RenderComponent(FB, GraphicsContext->TimeoutsLayer, TRUE);
    }

    /* Keep track of how many seconds have elapsed. */
    m->MillisecondsElapsed += 100;
}



EFI_STATUS
GraphicsInit(CONFIGURATION *c)
{
    if (NULL == c) return EFI_INVALID_PARAMETER;

    EFI_STATUS Status = EFI_SUCCESS;

    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *GraphicsInfo = NULL;
    UINTN GraphicsInfoSize = 0, NumberOfModes, NativeMode,
        BestMode = 0, LargestBufferSize = 0;

    ERRCHECK_UEFI(BS->LocateProtocol, 3,
                  &gEfiGraphicsOutputProtocolGuid,
                  NULL,
                  (VOID **)&GOP);

    /* Skip video mode auto-selection when set by config. */
    if (FALSE == c->AutoMode) goto GraphicsInit__SkipVideoMode;

Init__QueryMode:
    Status = uefi_call_wrapper(GOP->QueryMode, 4,
                               GOP,
                               ((NULL == GOP->Mode) ? 0 : GOP->Mode->Mode),
                               &GraphicsInfoSize,
                               &GraphicsInfo);
    if (EFI_NOT_STARTED == Status) {
        /* Need to run a 'SetMode' first. */
        ERRCHECK_UEFI(GOP->SetMode, 2, GOP, 0);
        goto Init__QueryMode;
    } else if (EFI_ERROR(Status)) {
        EFI_DANGERLN("Failure querying GOP video modes.");
        return Status;
    } else if (NULL == GOP->Mode) {
        EFI_DANGERLN("Empty GOP native mode struct.");
        return EFI_NOT_READY;
    }

    FreePool(GraphicsInfo);

    /* Store some information before changing modes, in case we want it. */
    NativeMode = GOP->Mode->Mode;
    NumberOfModes = GOP->Mode->MaxMode;

    PRINTLN("GOP:  Inspecting %u video modes...", NumberOfModes);

    for (UINTN i = 0; i < NumberOfModes; ++i) {
        Status = uefi_call_wrapper(GOP->QueryMode, 4, GOP, i, &GraphicsInfoSize, &GraphicsInfo);

        if (EFI_ERROR(Status)) {
            EFI_WARNINGLN("NOTICE: GOP:  Error while querying mode #%u (Code %u).", i, Status);
            continue;
        }

        /* Auto-select the video mode with the best resolution. */
        if ((GraphicsInfo->HorizontalResolution * GraphicsInfo->VerticalResolution) > LargestBufferSize) {
            BestMode = i;
            LargestBufferSize = (GraphicsInfo->HorizontalResolution * GraphicsInfo->VerticalResolution);
        }

        EFI_COLOR(MFTAH_COLOR_DEBUG);
        PRINTLN(
            "Detected video mode %03u (W by H -> %u x %u, PPL %u, format %x%a)",
            i,
            GraphicsInfo->HorizontalResolution,
            GraphicsInfo->VerticalResolution,
            GraphicsInfo->PixelsPerScanLine,
            GraphicsInfo->PixelFormat,
            (i == NativeMode) ? "  (current)" : ""
        );
        EFI_COLOR(MFTAH_COLOR_DEFAULT);

        FreePool(GraphicsInfo);
    }

    PRINTLN("Finished enumerating video modes.");

    /* This should be done even in spite of the modes being the same, just to
        make sure the mode is changeable, that there are no problems, and that we
        actually get everything initialized that we need. */
    PRINTLN("Setting video mode #%u...", BestMode);
    ERRCHECK_UEFI(GOP->SetMode, 2, GOP, BestMode);

GraphicsInit__SkipVideoMode:
    /* Initialize the graphics context. IF the video is above a certain resolution,
        set the zoom and other display context items appropriately. */
    GraphicsContext = (GRAPHICS_CONTEXT *)AllocateZeroPool(sizeof(GRAPHICS_CONTEXT));

    /* No tricks here... */
    GraphicsContext->Zoom = c->Scale;
    GraphicsContext->NormalTimeout = c->Timeout;
    GraphicsContext->MaxTimeout = c->MaxTimeout;

    /* Initialize the framebuffer object and clear the screen to the bg color. */
    ERRCHECK(FramebufferInit(GOP));

    if (NULL == FB->BLT) {
        Status = EFI_OUT_OF_RESOURCES;
        PANIC("FATAL: GOP:  No BLT shadow buffer allocated: out of resources.");
    }

    /* TESTING SPINNING LOADER ICON. */
    // EFI_GRAPHICS_OUTPUT_BLT_PIXEL px = {0};
    // BOUNDED_SHAPE *blt = NULL; ERRCHECK(NewObjectBlt(0, 0, FB->Resolution.Width, FB->Resolution.Height, FB_Z_INDEX_MAX, &blt));
    // CopyMem((VOID *)blt->Buffer, (VOID *)FB->BLT->Buffer, FB->BLT->BufferSize);
    // VOLATILE BOOLEAN b = TRUE; FB_DIMENSION d = {150, 150}; f.X = 100; f.Y = 100;
    // LoadingAnimationLoop(blt, f, d, &b);
    // uefi_call_wrapper(BS->Stall, 1, 10000000);

    /* Once the new mode initializes (if it was changed), the screen SHOULD turn black. */
    return EFI_SUCCESS;
}


EFI_STATUS
GraphicsDestroy(BOOLEAN PreserveFramebuffer)
{
    GraphicsDestroyMenu();
    FreePool(GraphicsContext);

    if (FALSE == PreserveFramebuffer) {
        FramebufferDestroy();
    }

    return EFI_SUCCESS;
}


VOID
GraphicsDestroyMenu(VOID)
{
    for (UINTN i = 0; i < RenderablesLength; ++i) {
        if (NULL == Renderables[i]) continue;

        FreePool((VOID *)(Renderables[i]->Buffer));
        FreePool(Renderables[i]);

        Renderables[i] = NULL;
    }

    RenderablesLength = 0;
}


EFI_STATUS
GraphicsModePopulateMenu(OUT EFI_MENU_RENDERER_PROTOCOL *Renderer)
{
    if (NULL == Renderer) return EFI_INVALID_PARAMETER;

    Renderer->Initialize    = InitMenu;
    Renderer->InputPopup    = InputPopup;
    Renderer->Confirmation  = Confirmation;
    Renderer->Popup         = Popup;
    Renderer->WarningPopup  = WarningPopup;
    Renderer->Redraw        = DrawMenu;
    Renderer->ClearScreen   = ClearScreen;
    Renderer->Tick          = TimerTick;

    return EFI_SUCCESS;
}


// TODO: Perhaps this should be moved to the FB file as a method on the 'protocol'
VOID
GPrint(CHAR *restrict Str,
       BOUNDED_SHAPE *ObjectBltBuffer,
       UINTN X,
       UINTN Y,
       UINT32 Foreground,
       UINT32 Background,
       BOOLEAN Wrap,
       UINT8 FontScale)
{
    if (
        NULL == Str
        || '\0' == *Str
        || NULL == ObjectBltBuffer
        || 0 == FontScale
    ) return;

    UINTN i = 0;
    CHAR *p = Str;
    for (; *p; ++p, ++i) {
        if ('\n' == *p && TRUE == Wrap) {
            i = 0;
            Y += (FontScale * FB->BaseGlyphSize.Height);

            continue;
        }

        /* Try to wrap text inside the BLT. */
        if (X + ((i+1) * FontScale * FB->BaseGlyphSize.Width) > ObjectBltBuffer->Dimensions.Width) {
            /* If wrapping is disabled, stop drawing here. */
            if (FALSE == Wrap) return;

            i = 0;
            Y += (FontScale * FB->BaseGlyphSize.Height);
        }

        // if (Y > ObjectBltBuffer->Dimensions.Height) return;

        FB->RenderGlyph(FB,
                        ObjectBltBuffer,
                        *p,
                        X + (i * FontScale * FB->BaseGlyphSize.Width),
                        Y,
                        Foreground,
                        Background,
                        TRUE,
                        FontScale);
    }
}


EFI_STATUS
LoadingAnimationLoop(BOUNDED_SHAPE *Underlay,
                     UINT32 ColorARGB,
                     VOLATILE BOOLEAN *Stall)
{
    if (
        NULL == Stall
        || FALSE == *Stall
        || NULL == Underlay
        || 0 == Underlay->Dimensions.Height
        || 0 == Underlay->Dimensions.Width
        || (Underlay->Position.X + Underlay->Dimensions.Width) > FB->Resolution.Width
        || (Underlay->Position.Y + Underlay->Dimensions.Height) > FB->Resolution.Height
    ) return EFI_INVALID_PARAMETER;

    /* Create a weird squiggly polygon and every few frames make it dither around
        based on some PRNG values. This will create a little "scribble" effect which
        acts as a cool 'animation'. The underlay acts as the buffer to draw the
        loading BLT on top of. It's never converged, just MERGED into the main FB BLT. */
    EFI_STATUS Status = EFI_SUCCESS;
    BOUNDED_SHAPE *s = NULL;
    FB_VERTEX Origin = {0};
    VOLATILE FB_VERTEX *Vertices = NULL;

    /* Short-handing these properties... */
    FB_VERTEX Position = Underlay->Position;
    FB_DIMENSION Size = Underlay->Dimensions;

    EFI_RNG_PROTOCOL *RNG = NULL;
    EFI_GUID gEfiRngProtocolGuid = EFI_RNG_PROTOCOL_GUID;
    uefi_call_wrapper(BS->LocateProtocol, 3,
                      &gEfiRngProtocolGuid,
                      NULL,
                      (VOID **)&RNG);

    if (EFI_SUCCESS != (
        Status = NewObjectBlt(Position.X,
                              Position.Y,
                              Size.Width,
                              Size.Height,
                              FB_Z_INDEX_MAX,
                              &s)
    )) goto LoadingAnimation__ExitError;

    /* Initialize vertices with random locations inside of `Size` (16 <= Count <= 128). */
    UINTN VerticesCount = MAX(16, MIN(128, (Size.Width * Size.Height) / 1000));
    INT8 wiggleX = 0, wiggleY = 0;

    Vertices = (FB_VERTEX *)AllocateZeroPool(sizeof(FB_VERTEX) * VerticesCount);
    if (NULL == Vertices) {
        Status = EFI_OUT_OF_RESOURCES;
        goto LoadingAnimation__ExitError;
    }

    for (UINTN i = 0; i < VerticesCount; ++i) {
        if (NULL == RNG || EFI_SUCCESS != RNG->GetRNG(RNG, NULL, sizeof(UINTN), &(Vertices[i].X))) {
            Vertices[i].X = i;
        } else {
            Vertices[i].X %= Size.Width;
        }

        if (NULL == RNG || EFI_SUCCESS != RNG->GetRNG(RNG, NULL, sizeof(UINTN), &(Vertices[i].Y))) {
            Vertices[i].Y = i;
        } else {
            Vertices[i].Y %= Size.Height;
        }
    }

    /* Render the underlay and then the initial state of the loading animation. */
    FB->Flush(FB);

    while (TRUE == *Stall) {
        /* Flash the underlay into the animation BLT to 'reset' the drawn lines. */
        if (EFI_SUCCESS != FB->BltToBlt(FB, s, Underlay, Origin, Origin, Size)) {
            Status = EFI_LOAD_ERROR;
            goto LoadingAnimation__ExitError;
        }

        /* Use the PRNG to get some slight modifications to each vertex. */
        for (UINTN i = 0; i < VerticesCount; ++i) {
            if (NULL == RNG || EFI_SUCCESS != RNG->GetRNG(RNG, NULL, sizeof(INT8), &wiggleX)) {
                wiggleX = (i % 6) - 3;
            } else {
                wiggleX %= 6;
            }

            if (NULL == RNG || EFI_SUCCESS != RNG->GetRNG(RNG, NULL, sizeof(INT8), &wiggleY)) {
                wiggleY = (i % 6) - 3;
            } else {
                wiggleY %= 6;
            }

            Vertices[i].X = MIN(Size.Width - 1,  MAX(1, (Vertices[i].X + wiggleX) % Size.Width));
            Vertices[i].Y = MIN(Size.Height - 1, MAX(1, (Vertices[i].Y + wiggleY) % Size.Height));
        }

        /* Redraw the polygon inside the animation BLT. */
        FB->DrawPolygon(FB, s, VerticesCount, Vertices, ColorARGB, TRUE);

        /* Partial rendering. */
        if (EFI_SUCCESS != FB->BltToBlt(FB, FB->BLT, s, Position, Origin, Size)) {
            Status = EFI_LOAD_ERROR;
            goto LoadingAnimation__ExitError;
        }

        FB->FlushPartial(FB, Position.X, Position.Y, Position.X, Position.Y, Size.Width, Size.Height);

        /* 25 ms delay between shifts */
        // TODO: Better timing
        uefi_call_wrapper(BS->Stall, 1, 25000);
    }

    Status = EFI_SUCCESS;

LoadingAnimation__ExitError:
    BltDestroy(s);
    FreePool(Vertices);
    return Status;
}



DECL_DRAW_FUNC(Underlay)
{
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL p = {0};
    BltPixelFromARGB(&p, c->Colors.Background);

    FB->ClearBlt(FB, This, &p);

    // TODO: Consider other options like background image processing here.
}


DECL_DRAW_FUNC(LeftPanel)
{
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL border = {0};
    BltPixelFromARGB(&border, c->Colors.Border);
    FB->ClearBlt(FB, This, &border);

    /* Draw the header text. The text is one step up from the current zoom. */
    GraphicsContext->Zoom++;   /* THIS MUST BE HERE BECAUSE OF THE LAYOUT_G_GLYPH_WIDTH REF BELOW */
    UINTN StrPixels = (AsciiStrLen(c->Title) * LAYOUT_G_GLYPH_WIDTH);
    FB_VERTEX HeaderAt = {
        .X = (
            ((This->Dimensions.Width / 2) > (StrPixels / 2))
                ? ((This->Dimensions.Width / 2) - (StrPixels / 2))
                : 10
        ),
        .Y = LAYOUT_G_PANEL_PADDING_TOP + (LAYOUT_G_GLYPH_HEIGHT / 2)
    };

    GPrint(c->Title,
           This,
           HeaderAt.X,
           HeaderAt.Y,
           c->Colors.Title.Foreground,
           c->Colors.Title.Background,
           FALSE,
           GraphicsContext->Zoom);
    GraphicsContext->Zoom--;

    /* Draw a box outline. */
    FB_VERTEX f = {0}, t = { .X = This->Dimensions.Width-1, .Y = This->Dimensions.Height-1 };
    FB->DrawSimpleShape(FB, This, FbShapeRectangle, f, t, 0, FALSE, 1, 0x00000000);
}


DECL_DRAW_FUNC(RightPanel)
{
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL border = {0};
    BltPixelFromARGB(&border, c->Colors.Border);

    FB->ClearBlt(FB, This, &border);

    /* Draw a box outline. */
    FB_VERTEX f = {0}, t = { .X = This->Dimensions.Width-1, .Y = This->Dimensions.Height-1 };
    FB->DrawSimpleShape(FB, This, FbShapeRectangle, f, t, 0, FALSE, 1, 0x00000000);

    // TODO: generate these during config parsing and attach them to the chains themselves as a property
    // This is otherwise annoyingly expensive
    CHAR8 *ChainInfo = NULL;
    ConfigDumpChain(c->Chains[m->CurrentItemIndex], &ChainInfo);

    if (NULL != ChainInfo) {
        GPrint(ChainInfo,
               This,
               LAYOUT_G_TEXT_LEFT_PADDING,
               LAYOUT_G_TEXT_TOP_PADDING,
               c->Colors.Title.Foreground,
               c->Colors.Title.Background,
               TRUE,
               GraphicsContext->Zoom);

        FreePool(ChainInfo);
    }
}


DECL_DRAW_FUNC(Banner)
{
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL banner = {0};
    BltPixelFromARGB(&banner, c->Colors.Banner.Background);

    FB->ClearBlt(FB, This, &banner);

    /* Draw a box outline. */
    FB_VERTEX f = {0}, t = { .X = This->Dimensions.Width-1, .Y = This->Dimensions.Height-1 };
    FB->DrawSimpleShape(FB, This, FbShapeRectangle, f, t, 0, FALSE, 1, 0x00000000);

    GPrint(
        c->Banner,
        This,
        LAYOUT_G_TEXT_LEFT_PADDING,
        LAYOUT_G_TEXT_TOP_PADDING,
        c->Colors.Banner.Foreground,
        c->Colors.Banner.Background,
        TRUE,
        GraphicsContext->Zoom
    );
}


DECL_DRAW_FUNC(Timeouts)
{
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL timeout = {0};
    CHAR8 *NoTimeoutStr = "No timeout values are set.";
    CHAR8 *DefaultTimeoutStr = "Choosing default menu option...";
    CHAR8 *MaxTimeoutStr = "Maximum timeout exceeded! Shutting down!";

    BltPixelFromARGB(&timeout, c->Colors.Timer.Background);
    FB->ClearBlt(FB, This, &timeout);

    /* Draw a box outline. */
    FB_VERTEX f = {0}, t = { .X = This->Dimensions.Width-1, .Y = This->Dimensions.Height-1 };
    FB->DrawSimpleShape(FB, This, FbShapeRectangle, f, t, 0, FALSE, 1, 0x00000000);

    if (FALSE == m->KeyPressReceived && c->Timeout > 0) {
        if (m->MillisecondsElapsed < c->Timeout) {
            GPrint(GraphicsContext->NormalTimeoutText,
                   This,
                   LAYOUT_G_TEXT_LEFT_PADDING,
                   (LAYOUT_G_GLYPH_HEIGHT / 2),
                   c->Colors.Timer.Foreground,
                   c->Colors.Timer.Background,
                   FALSE,
                   GraphicsContext->Zoom);
        } else {
            GPrint(DefaultTimeoutStr,
                   This,
                   LAYOUT_G_TEXT_LEFT_PADDING,
                   LAYOUT_G_GLYPH_HEIGHT,
                   0xFFFF0000,
                   0x00000000,
                   FALSE,
                   GraphicsContext->Zoom);
            return;
        }
    }

    if (c->MaxTimeout > 0) {
        if (m->MillisecondsElapsed < c->MaxTimeout) {
            GPrint(GraphicsContext->MaxTimeoutText,
                   This,
                   LAYOUT_G_TEXT_LEFT_PADDING,
                   (LAYOUT_G_GLYPH_HEIGHT * 3) / 2,
                   c->Colors.Timer.Foreground,
                   c->Colors.Timer.Background,
                   FALSE,
                   GraphicsContext->Zoom);
        } else {
            GPrint(MaxTimeoutStr,
                   This,
                   LAYOUT_G_TEXT_LEFT_PADDING,
                   LAYOUT_G_GLYPH_HEIGHT,
                   0xFFFF0000,
                   0x00000000,
                   FALSE,
                   GraphicsContext->Zoom);
            return;
        }
    }

    if (0 == c->Timeout && 0 == c->MaxTimeout) {
        GPrint(NoTimeoutStr,
               This,
               LAYOUT_G_TEXT_LEFT_PADDING,
               LAYOUT_G_GLYPH_HEIGHT,
               c->Colors.Timer.Foreground,
               c->Colors.Timer.Background,
               FALSE,
               GraphicsContext->Zoom);
    }
}


DECL_DRAW_FUNC(MenuItem)
{
    /* For menu items, the passed 'Extra' (e) param is the index of the current renderable. */
    UINTN index = *((UINTN *)e);
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL itembg = {0}, itemfg = {0};

    BltPixelFromARGB(&itembg, c->Colors.Text.Background);
    BltPixelFromARGB(&itemfg, c->Colors.Text.Foreground);

    if (index == m->CurrentItemIndex) {
        BltPixelInvert(&itembg);
        BltPixelInvert(&itemfg);
    }

    FB->ClearBlt(FB, This, &itembg);

    if (index < m->ItemsListLength) {
        GPrint(
            c->Chains[index]->Name,
            This,
            LAYOUT_G_TEXT_LEFT_PADDING,
            LAYOUT_G_TEXT_TOP_PADDING,
            ARGBFromBltPixel(&itemfg),
            ARGBFromBltPixel(&itembg),
            FALSE,
            GraphicsContext->Zoom
        );
    }

    if (index == m->CurrentItemIndex) {
        /* Draw a box outline to beter 'highlight' the selection. */
        FB_VERTEX f = {0}, t = { .X = This->Dimensions.Width-1, .Y = This->Dimensions.Height-1 };
        FB->DrawSimpleShape(FB, This, FbShapeRectangle, f, t, 0, FALSE, 1, ARGBFromBltPixel(&itemfg));
    }
}
