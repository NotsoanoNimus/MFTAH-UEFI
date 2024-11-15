#include "../../include/drivers/displays.h"
#include "../../include/drivers/fb.h"

#include "../../include/core/util.h"



CHAR8 *NormalTimeoutStr = "Choosing default in %u seconds.";
CHAR8 *MaxTimeoutStr = "Global timeout in %u seconds.";
CHAR8 *NoTimeoutStr = "No timeout values are set.";
CHAR8 *DefaultTimeoutStr = "Choosing default menu option...";
CHAR8 *MaxTimeoutExceededtStr = "Maximum timeout exceeded! Shutting down!";


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
} GRAPHICS_CONTEXT;



STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL *GOP = NULL;

STATIC GRAPHICS_CONTEXT *GraphicsContext = NULL;

STATIC BOUNDED_SHAPE *LoadingIconUnderlayBlt,
                     *ProgressBlt,
                     *MftahKeyPromptBlt;



/* Forward declarations. */
STATIC EFI_STATUS GraphicsInit(
    IN SIMPLE_DISPLAY *This,
    IN CONFIGURATION *Configuration
);

STATIC EFI_STATUS GraphicsDestroy(
    IN CONST SIMPLE_DISPLAY *This
);

STATIC VOID GraphicsClearScreen(
    IN CONST SIMPLE_DISPLAY *This,
    IN UINT32 Color
);

STATIC VOID GraphicsPanic(
    IN CONST SIMPLE_DISPLAY *This,
    IN CHAR8 *Message,
    IN BOOLEAN IsShutdown,
    IN UINTN ShutdownTimer
);

STATIC VOID GraphicsPrintProgress(
    IN CONST SIMPLE_DISPLAY *This,                      
    IN CONST CHAR8 *Message,                      
    IN UINTN Current,                      
    IN UINTN OutOfTotal
);

STATIC VOID GraphicsStall(
    IN CONST SIMPLE_DISPLAY *This,
    IN UINTN TimeInMilliseconds
);

STATIC VOID GraphicsInputPopup(
    IN CONST SIMPLE_DISPLAY *This,
    IN CHAR8 *Prompt,
    IN CHAR8 *CurrentInput,
    IN BOOLEAN IsHidden,
    IN CHAR8 *ErrorMessage OPTIONAL
);

/* Construct the static display protocol instance. */
SIMPLE_DISPLAY GUI = {
    .Initialize     = GraphicsInit,
    .Destroy        = GraphicsDestroy,
    .ClearScreen    = GraphicsClearScreen,
    .Panic          = GraphicsPanic,
    .Progress       = GraphicsPrintProgress,
    .Stall          = GraphicsStall,
    .InputPopup     = GraphicsInputPopup,

    .MENU           = NULL
};


STATIC VOID GPrint(
    CHAR8 *Str,
    BOUNDED_SHAPE *ObjectBltBuffer,
    UINTN X,
    UINTN Y,
    UINT32 Foreground,
    UINT32 Background,
    BOOLEAN Wrap,
    UINT8 FontScale
);

STATIC EFI_STATUS LoadingAnimationLoop(
    BOUNDED_SHAPE *Underlay,
    UINT32 ColorARGB,
    VOLATILE BOOLEAN *Stall
);


/* LAYOUT CONSTANTS. */
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


#define GRAPHICS_MAX_RENDERABLES 32
STATIC BOUNDED_SHAPE *Renderables[GRAPHICS_MAX_RENDERABLES] = {0};
STATIC UINTN RenderablesLength = 0;


#define DECL_DRAW_FUNC(Name) \
    STATIC EFIAPI VOID \
    GraphicsDraw__##Name(BOUNDED_SHAPE *This, CONFIGURATION *c, MENU_STATE *m, VOID *e)

DECL_DRAW_FUNC(Underlay);
DECL_DRAW_FUNC(LeftPanel);
DECL_DRAW_FUNC(RightPanel);
DECL_DRAW_FUNC(Banner);
DECL_DRAW_FUNC(Timeouts);
DECL_DRAW_FUNC(MenuItem);



STATIC
VOID
DrawMenu(IN MENU_STATE *m)
{
    FB_VERTEX Origin = {0};

    FB->ClearScreen(FB, CONFIG->Colors.Background);

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
                Renderables[i]->Draw(Renderables[i], CONFIG, m, (VOID *)&i);
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
EFI_STATUS
InitMenu(IN EFI_SIMPLE_FRAMEBUFFER_PROTOCOL *FB)
{
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
VOID
TimerTick(EFI_EVENT Event,
          VOID *Context)
{
    MENU_STATE *m = (MENU_STATE *)Context;

    if (
        FALSE == m->KeyPressReceived
        && GraphicsContext->NormalTimeout > 0
        && m->MillisecondsElapsed >= GraphicsContext->NormalTimeout
    ) {
        /* A normal timeout has occurred. Signal to the menu handler. */
        BS->CloseEvent(Event);

        GraphicsContext->TimeoutsLayer->Draw(GraphicsContext->TimeoutsLayer, CONFIG, m, NULL);
        FB->RenderComponent(FB, GraphicsContext->TimeoutsLayer, TRUE);
        BS->Stall(EFI_SECONDS_TO_MICROSECONDS(2));

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
        BS->CloseEvent(Event);

        GraphicsContext->TimeoutsLayer->Draw(GraphicsContext->TimeoutsLayer, CONFIG, m, NULL);
        FB->RenderComponent(FB, GraphicsContext->TimeoutsLayer, TRUE);
        BS->Stall(EFI_SECONDS_TO_MICROSECONDS(2));

        Shutdown(EFI_TIMEOUT);
    }

    if (
        0 == (m->MillisecondsElapsed % 1000)
        && FALSE == m->PauseTickRenders
    ) {
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
        GraphicsContext->TimeoutsLayer->Draw(GraphicsContext->TimeoutsLayer, CONFIG, m, NULL);
        FB->RenderComponent(FB, GraphicsContext->TimeoutsLayer, TRUE);
    }

    /* Keep track of how many seconds have elapsed. */
    m->MillisecondsElapsed += 100;
}


STATIC
EFI_STATUS
GraphicsInit(IN SIMPLE_DISPLAY *This,
             IN CONFIGURATION *Configuration)
{
    if (NULL == This || NULL == Configuration) {
        return EFI_INVALID_PARAMETER;
    }

    EFI_STATUS Status = EFI_SUCCESS;

    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *GraphicsInfo = NULL;
    UINTN GraphicsInfoSize = 0, NumberOfModes, NativeMode,
        BestMode = 0, LargestBufferSize = 0;

    ERRCHECK(
        BS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid,
                           NULL,
                           (VOID **)&GOP)
    );

    /* Sanity check. */
    if (NULL == GOP) {
        return EFI_NOT_STARTED;
    }

    /* Skip video mode auto-selection when set by config. */
    if (FALSE == Configuration->AutoMode) goto GraphicsInit__SkipVideoMode;

Init__QueryMode:
    Status = GOP->QueryMode(GOP,
                            ((NULL == GOP->Mode) ? 0 : GOP->Mode->Mode),
                            &GraphicsInfoSize,
                            &GraphicsInfo);
    if (EFI_NOT_STARTED == Status) {
        /* Need to run a 'SetMode' first. */
        ERRCHECK(GOP->SetMode(GOP, 0));
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
        Status = GOP->QueryMode(GOP, i, &GraphicsInfoSize, &GraphicsInfo);

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
    ERRCHECK(GOP->SetMode(GOP, BestMode));

GraphicsInit__SkipVideoMode:
    /* Initialize the graphics context. IF the video is above a certain resolution,
        set the zoom and other display context items appropriately. */
    GraphicsContext = (GRAPHICS_CONTEXT *)AllocateZeroPool(sizeof(GRAPHICS_CONTEXT));
    if (NULL == GraphicsContext) {
        EFI_DANGERLN("ERROR: GOP:  Out of resources while allocating context.");
        return EFI_OUT_OF_RESOURCES;
    }

    /* No tricks here... */
    GraphicsContext->Zoom = Configuration->Scale;
    GraphicsContext->NormalTimeout = Configuration->Timeout;
    GraphicsContext->MaxTimeout = Configuration->MaxTimeout;

    /* Initialize the framebuffer object and clear the screen to the bg color. */
    ERRCHECK(FramebufferInit(GOP));

    if (NULL == FB || NULL == FB->BLT) {
        Status = EFI_OUT_OF_RESOURCES;
        PANIC("FATAL: GOP:  No BLT shadow buffer allocated: out of resources.");
    }

    /* Initial screen clearing. */
    This->ClearScreen(This, Configuration->Colors.Background);

    /* Ready the menu's objects. */
    EFI_MENU_RENDERER_PROTOCOL *Renderer = (EFI_MENU_RENDERER_PROTOCOL *)
        AllocateZeroPool(sizeof(EFI_MENU_RENDERER_PROTOCOL));
    if (NULL == Renderer) {
        EFI_DANGERLN("FATAL: GOP:  No space for a menu renderer: out of resources.");
        return EFI_OUT_OF_RESOURCES;
    }

    Renderer->Redraw        = DrawMenu;
    Renderer->Tick          = TimerTick;
    This->MENU = Renderer;

    InitMenu(FB);

    /* Prepare some other independent display components and artifacts. */
    Status = NewObjectBlt((FB->Resolution.Width / 2) - MIN(250, (FB->Resolution.Width / 4)),
                          (FB->Resolution.Height / 5),
                          MIN(500, (FB->Resolution.Width / 2)),
                          MIN(250, (FB->Resolution.Height / 3)),
                          1,
                          &ProgressBlt);
    if (EFI_ERROR(Status)) {
        This->Panic(This, "Failed to allocate a BLT for the progress notifier.", FALSE, 0);
        return EFI_NOT_STARTED;
    }

    EFI_GRAPHICS_OUTPUT_BLT_PIXEL p = {0};
    BltPixelFromARGB(&p, CONFIG->Colors.Text.Background);
    FB->ClearBlt(FB, ProgressBlt, &p);

    Status = NewObjectBlt(ProgressBlt->Position.X,
                          ProgressBlt->Position.Y,
                          ProgressBlt->Dimensions.Width,
                          ProgressBlt->Dimensions.Height,
                          1,
                          &MftahKeyPromptBlt);
    if (EFI_ERROR(Status)) {
        This->Panic(This, "Failed to allocate a BLT for the MFTAH key prompt.", FALSE, 0);
        return EFI_NOT_STARTED;
    }

    FB->ClearBlt(FB, MftahKeyPromptBlt, &p);

    Status = NewObjectBlt((FB->Resolution.Width / 2) - 100,
                          ProgressBlt->Position.Y + ProgressBlt->Dimensions.Height + 30,
                          200,
                          200,
                          2,
                          &LoadingIconUnderlayBlt);
    if (EFI_ERROR(Status)) {
        This->Panic(This, "Failed to allocate the loading animation underlay BLT.", FALSE, 0);
        return EFI_NOT_STARTED;
    }

    BltPixelFromARGB(&p, CONFIG->Colors.Background);
    FB->ClearBlt(FB, LoadingIconUnderlayBlt, &p);

    /* Once the new mode initializes (if it was changed), the screen SHOULD turn black. */
    return EFI_SUCCESS;
}


STATIC
EFI_STATUS
GraphicsDestroy(IN CONST SIMPLE_DISPLAY *This)
{
    /* Destroy all menu renderable BLTs. */
    for (UINTN i = 0; i < RenderablesLength; ++i) {
        if (NULL == Renderables[i]) continue;

        FreePool((VOID *)(Renderables[i]->Buffer));
        FreePool(Renderables[i]);

        Renderables[i] = NULL;
    }
    RenderablesLength = 0;

    /* Delete any independently tracked BLT buffers. */
    BltDestroy(ProgressBlt);
    BltDestroy(LoadingIconUnderlayBlt);
    BltDestroy(MftahKeyPromptBlt);

    /* Free the GUI context. */
    FreePool(GraphicsContext);

    /* Free protocol pointers. */
    FreePool((VOID *)FB->BLT->Buffer);
    FreePool(FB->BLT);
    FreePool(FB);
    FB = NULL;

    FreePool(This->MENU);

    return EFI_SUCCESS;
}


STATIC
VOID
GraphicsClearScreen(IN CONST SIMPLE_DISPLAY *This,
                    IN UINT32 Color)
{
    FB->ClearScreen(FB, Color);
    FB->Flush(FB);
}


STATIC
VOID
GraphicsPanic(IN CONST SIMPLE_DISPLAY *This,
              IN CHAR8 *Message,
              IN BOOLEAN IsShutdown,
              IN UINTN ShutdownTimer)
{
    CHAR8 *Shadow = (CHAR8 *)
        AllocateZeroPool(sizeof(CHAR8) + (AsciiStrLen(Message) + 7 + 1));

    if (NULL == Shadow) {
        GPrint(Message, FB->BLT, 20, 20, 0xFFFF0000, 0, TRUE, 2);
    } else {
        AsciiSPrint(Shadow, (AsciiStrLen(Message) + 7), "PANIC: %a", Message);
        GPrint(Shadow, FB->BLT, 20, 20, 0xFFFF0000, 0, TRUE, 2);
        FreePool(Shadow);
    }

    FB->Flush(FB);

    if (0 == ShutdownTimer) {
        /* Default to 3 seconds. */
        BS->Stall(EFI_SECONDS_TO_MICROSECONDS(3));
    } else {
        BS->Stall(ShutdownTimer);
    }

    if (TRUE == IsShutdown) {
        Shutdown(EFI_SUCCESS);
    }
}


STATIC
VOID
GraphicsPrintProgress(IN CONST SIMPLE_DISPLAY *This,
                      IN CONST CHAR8 *Message,
                      IN UINTN Current,
                      IN UINTN OutOfTotal)
{
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL p = {0};
    BltPixelFromARGB(&p, CONFIG->Colors.Text.Background);
    FB->ClearBlt(FB, ProgressBlt, &p);

    FB_VERTEX ProgressRect = {
        .X = ProgressBlt->Dimensions.Width / 8,
        .Y = ProgressBlt->Dimensions.Height - 90
    };
    FB_VERTEX ProgressRectTo = {
        .X = 7 * (ProgressBlt->Dimensions.Width / 8),
        .Y = ProgressBlt->Dimensions.Height - 40
    };
    FB_VERTEX ProgressRectForegroundTo = {
        .X = ProgressRect.X + ((Current * (ProgressRectTo.X - ProgressRect.X)) / OutOfTotal),
        .Y = ProgressRectTo.Y
    };

    FB_VERTEX MessageAt = {
        .X = (ProgressBlt->Dimensions.Width / 2) - ((AsciiStrLen(Message) * 2 * FB->BaseGlyphSize.Width) / 2),
        .Y = 30
    };

    if (NULL != Message) {
        FB->PrintString(FB,
                        Message,
                        ProgressBlt,
                        &MessageAt,
                        &(CONFIG->Colors.Text),
                        FALSE,
                        2);
    }

    /* Progress bar background, foreground, then box border. */
    FB->DrawSimpleShape(
        FB, ProgressBlt, FbShapeRectangle,
        ProgressRect, ProgressRectTo, 0, TRUE, 1, CONFIG->Colors.Title.Background
    );
    FB->DrawSimpleShape(
        FB, ProgressBlt, FbShapeRectangle,
        ProgressRect, ProgressRectForegroundTo, 0, TRUE, 1, CONFIG->Colors.Title.Foreground
    );
    FB->DrawSimpleShape(
        FB, ProgressBlt, FbShapeRectangle,
        ProgressRect, ProgressRectTo, 0, FALSE, 1, CONFIG->Colors.Title.Foreground
    );

    /* Full progress BLT box border. */
    BltDrawOutline(ProgressBlt, CONFIG->Colors.Text.Foreground);

    FB->RenderComponent(FB, ProgressBlt, TRUE);
}


STATIC
VOID
FlipToFalse(EFI_EVENT Event,
            VOID *Context)
{ BOOLEAN *b = (BOOLEAN *)Context; if (NULL != b) *b = FALSE; }

STATIC
VOID
GraphicsStall(IN CONST SIMPLE_DISPLAY *This,
              IN UINTN TimeInMilliseconds)
{
    VOLATILE BOOLEAN Stall = TRUE;
    EFI_EVENT StallEvent = {0};

    if (TRUE == CONFIG->Quick) return;

    BS->CreateEvent((EVT_TIMER | EVT_NOTIFY_SIGNAL), TPL_NOTIFY, FlipToFalse, (VOID *)&Stall, &StallEvent);
    BS->SetTimer(StallEvent, TimerPeriodic, 10 * 1000 * TimeInMilliseconds);

    if (EFI_ERROR(LoadingAnimationLoop(LoadingIconUnderlayBlt, ~(CONFIG->Colors.Background), &Stall))) {
        /* Stall normally if the animation can't be rendered. */
        BS->Stall(TimeInMilliseconds * 1000);
    }

    BS->CloseEvent(StallEvent);
}


STATIC
VOID
GraphicsInputPopup(IN CONST SIMPLE_DISPLAY *This,
                   IN CHAR8 *Prompt,
                   IN CHAR8 *CurrentInput,
                   IN BOOLEAN IsHidden,
                   IN CHAR8 *ErrorMessage OPTIONAL)
{
    if (
        NULL == This
        || NULL == Prompt
        || NULL == CurrentInput
    ) return;

    EFI_GRAPHICS_OUTPUT_BLT_PIXEL p = {0};
    BltPixelFromARGB(&p, CONFIG->Colors.Text.Background);
    FB->ClearBlt(FB, MftahKeyPromptBlt, &p);

    FB_VERTEX PromptRect = {
            .X = MftahKeyPromptBlt->Dimensions.Width / 8,
            .Y = MftahKeyPromptBlt->Dimensions.Height - 30 - (3 * FB->BaseGlyphSize.Height)
    };
    FB_VERTEX PromptRectTo = {
            .X = 7 * (MftahKeyPromptBlt->Dimensions.Width / 8),
            .Y = MftahKeyPromptBlt->Dimensions.Height - 30
    };

    GPrint(Prompt,
           MftahKeyPromptBlt,
           (MftahKeyPromptBlt->Dimensions.Width / 2)
               - ((AsciiStrLen(Prompt) * 2 * FB->BaseGlyphSize.Width) / 2),
           20,
           CONFIG->Colors.Text.Foreground,
           CONFIG->Colors.Text.Background,
           FALSE,
           2);

    if (NULL != ErrorMessage) {
        GPrint(ErrorMessage,
               MftahKeyPromptBlt,
               (MftahKeyPromptBlt->Dimensions.Width / 2)
                   - ((AsciiStrLen(ErrorMessage) * 1 * FB->BaseGlyphSize.Width) / 2),
               20 + (2 * FB->BaseGlyphSize.Height) + 30,
               0xFFFF0000,
               0x00000000,
               FALSE,
               1);
    }

    /* Input bar background and border. */
    FB->DrawSimpleShape(FB, MftahKeyPromptBlt, FbShapeRectangle, PromptRect, PromptRectTo, 0, TRUE, 1, CONFIG->Colors.Text.Foreground);
    FB->DrawSimpleShape(FB, MftahKeyPromptBlt, FbShapeRectangle, PromptRect, PromptRectTo, 0, FALSE, 1, CONFIG->Colors.Text.Background);

    /* Full BLT box border. */
    BltDrawOutline(MftahKeyPromptBlt, CONFIG->Colors.Title.Foreground);

    /* Never exceed the width of the box when GPrint'ing. */
    UINTN StarsToPrintInBox = MIN(
        ((NULL == CurrentInput) ? 0 : AsciiStrLen(CurrentInput)),
        ((PromptRectTo.X - PromptRect.X) / (2 * FB->BaseGlyphSize.Width))
    );

    /* Print the '*' characters to match the current length of the password or the max width. */
    if (TRUE == IsHidden) {
        for (UINTN i = 0; i < StarsToPrintInBox; ++i) {
            GPrint("*",
                   MftahKeyPromptBlt,
                   PromptRect.X + 5 + (i * 2 * FB->BaseGlyphSize.Width),
                   PromptRect.Y + (FB->BaseGlyphSize.Height / 2),
                   CONFIG->Colors.Text.Background,
                   CONFIG->Colors.Text.Foreground,
                   FALSE,
                   2);
        }
    } else if (NULL != CurrentInput) {
        // TODO Long strings will bleed off the edge of the textbox.
        GPrint(CurrentInput,
               MftahKeyPromptBlt,
               PromptRect.X,
               PromptRect.Y,
               CONFIG->Colors.Text.Background,
               CONFIG->Colors.Text.Foreground,
               FALSE,
               2);
    }

    /* Finally, render it all. */
    FB->RenderComponent(FB, MftahKeyPromptBlt, TRUE);
}


STATIC
VOID
GPrint(CHAR8 *Str,
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

    FB_VERTEX At = { .X = X, .Y = Y };
    COLOR_PAIR Color = { .Foreground = Foreground, .Background = Background };

    FB->PrintString(FB, Str, ObjectBltBuffer, &At, &Color, Wrap, FontScale);
}


STATIC
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
    BS->LocateProtocol(&gEfiRngProtocolGuid, NULL, (VOID **)&RNG);

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
        BS->Stall(25000);
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

    BltDrawOutline(This, CONFIG->Colors.Text.Foreground);
}


DECL_DRAW_FUNC(RightPanel)
{
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL border = {0};
    BltPixelFromARGB(&border, c->Colors.Border);

    FB->ClearBlt(FB, This, &border);

    BltDrawOutline(This, CONFIG->Colors.Text.Foreground);

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

    BltDrawOutline(This, CONFIG->Colors.Text.Foreground);

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

    BltPixelFromARGB(&timeout, c->Colors.Timer.Background);
    FB->ClearBlt(FB, This, &timeout);

    BltDrawOutline(This, CONFIG->Colors.Text.Foreground);

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
            GPrint(MaxTimeoutExceededtStr,
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
        BltDrawOutline(This, CONFIG->Colors.Text.Foreground);
    }
}
