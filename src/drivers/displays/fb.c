#include "../../include/drivers/displays/fb.h"

#include "../../include/fonts/orchid.h"
#include "../../include/core/util.h"



CONST EFI_SIMPLE_FRAMEBUFFER_PROTOCOL *FB = NULL;
CONST double PI = 3.14159265358979323846;



STATIC
EFIAPI
VOID
FlushFramebufferPartial(EFI_SIMPLE_FRAMEBUFFER_PROTOCOL *This,
                        UINTN X,
                        UINTN Y,
                        UINTN ToX,
                        UINTN ToY,
                        UINTN Width,
                        UINTN Height)
{
    if (NULL == This) return;

    uefi_call_wrapper(This->GOP->Blt, 10,
                      This->GOP,
                      (VOID *)This->BLT->Buffer,
                      EfiBltBufferToVideo,
                      X, Y,
                      ToX, ToY,
                      Width, Height,
                      This->BLT->Pitch);
}


STATIC
EFIAPI
VOID
FlushFramebuffer(EFI_SIMPLE_FRAMEBUFFER_PROTOCOL *This)
{
    FlushFramebufferPartial(
        This,
        0, 0, 0, 0,
        This->Resolution.Width,
        This->Resolution.Height
    );
}


STATIC
EFIAPI
EFI_PHYSICAL_ADDRESS
GetPixel(EFI_SIMPLE_FRAMEBUFFER_PROTOCOL *This,
         BOUNDED_SHAPE *ObjectBltBuffer,
         UINTN X,
         UINTN Y)
{
    if (
        NULL == ObjectBltBuffer
        || NULL == This
        || (((Y * ObjectBltBuffer->Pitch) + (X * ObjectBltBuffer->PixelSize)) > ObjectBltBuffer->BufferSize)
    ) return (EFI_PHYSICAL_ADDRESS)NULL;

    return (
        ObjectBltBuffer->Buffer
        + (Y * ObjectBltBuffer->Pitch)
        + (X * ObjectBltBuffer->PixelSize)
    );
}


STATIC
EFIAPI
VOID
SetPixel(EFI_SIMPLE_FRAMEBUFFER_PROTOCOL *This,
         BOUNDED_SHAPE *ObjectBltBuffer,
         UINTN X,
         UINTN Y,
         UINT32 ARGB)
{
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL p = {0};

    p.Red   = (ARGB & 0x00FF0000) >> 16;
    p.Green = (ARGB & 0x0000FF00) >> 8;
    p.Blue  = (ARGB & 0x000000FF) >> 0;
    p.Reserved = 0x00;

    EFI_PHYSICAL_ADDRESS PhysAddr = This->GetPixel(This, ObjectBltBuffer, X, Y);
    if (NULL == PhysAddr) return;

    CopyMem((VOID *)PhysAddr, &p, sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL));
}


STATIC
EFIAPI
EFI_STATUS
FastBltToBlt(EFI_SIMPLE_FRAMEBUFFER_PROTOCOL *This,
             BOUNDED_SHAPE *DestinationBlt,
             BOUNDED_SHAPE *SourceBlt,
             FB_VERTEX IntoPosition,
             FB_VERTEX FromPosition,
             FB_DIMENSION Dimension)
{
    if (
        NULL == This
        || NULL == DestinationBlt
        || NULL == DestinationBlt->Buffer
        || NULL == SourceBlt
        || NULL == SourceBlt->Buffer
        || 0 == Dimension.Width
        || 0 == Dimension.Height
        || (IntoPosition.X + Dimension.Width) > DestinationBlt->Dimensions.Width
        || (IntoPosition.Y + Dimension.Height) > DestinationBlt->Dimensions.Height
        || (FromPosition.X + Dimension.Width) > SourceBlt->Dimensions.Width
        || (FromPosition.Y + Dimension.Height) > SourceBlt->Dimensions.Height
    ) return EFI_INVALID_PARAMETER;

    EFI_PHYSICAL_ADDRESS ToPointer
        = This->GetPixel(This, DestinationBlt, IntoPosition.X, IntoPosition.Y);

    EFI_PHYSICAL_ADDRESS FromPointer
        = This->GetPixel(This, SourceBlt, FromPosition.X, FromPosition.Y);

    UINTN Length = SourceBlt->BufferSize;

    /* Make sure we don't overrun the destination buffer's size. */
    if (
        (ToPointer + (Dimension.Height * DestinationBlt->Pitch))
            > (DestinationBlt->Buffer + DestinationBlt->BufferSize)
    ) {
        return EFI_NOT_STARTED;
    }

    /* Copy/BLT row-by-row */
    for (UINTN row = 0; row < Dimension.Height; ++row) {
        /* Don't need to account for X here since the 'pointer' values already
            incorporate the offset into their starting positions. */
        CopyMem((VOID *)(ToPointer + (row * DestinationBlt->Pitch)),
                (VOID *)(FromPointer + (row * SourceBlt->Pitch)),
                (Dimension.Width * SourceBlt->PixelSize));
    }

    return EFI_SUCCESS;
}


STATIC
EFIAPI
VOID
ClearBlt(EFI_SIMPLE_FRAMEBUFFER_PROTOCOL *This,
         BOUNDED_SHAPE *ObjectBltBuffer,
         EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Pixel)
{
    if (
        NULL == This
        || NULL == ObjectBltBuffer
        || NULL == Pixel
    ) return;

    for (
        UINTN i = 0;
        i < ObjectBltBuffer->BufferSize;
        i += sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL)
    ) {
        CopyMem((VOID *)(ObjectBltBuffer->Buffer + i),
                (VOID *)Pixel,
                sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL));
    }
}


STATIC
EFIAPI
VOID
ClearScreen(EFI_SIMPLE_FRAMEBUFFER_PROTOCOL *This,
            UINT32 ARGB)
{
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL p = {0};

    p.Red   = (ARGB & 0x00FF0000) >> 16;
    p.Green = (ARGB & 0x0000FF00) >> 8;
    p.Blue  = (ARGB & 0x000000FF) >> 0;
    p.Reserved = 0x00;

    /* Update the primary BLT. */
    This->ClearBlt(This, This->BLT, &p);
}


STATIC
EFIAPI
EFI_STATUS
RenderGlyph(EFI_SIMPLE_FRAMEBUFFER_PROTOCOL *This,
            BOUNDED_SHAPE *ObjectBltBuffer,
            CHAR8 Glyph,
            UINTN X,
            UINTN Y,
            UINT32 Foreground,
            UINT32 Background,
            BOOLEAN HasShadow,
            UINTN Zoom)
{
    if (
        NULL == This
        || NULL == ObjectBltBuffer
        || 0 == Zoom
    ) return EFI_INVALID_PARAMETER;

    // TODO: The glyphs from the ORCHID font bitmap start at ' ' (space) and go through '~'.
    //  This needs to be changed in the future to represent all characters like a normal map.
    if (Glyph < ' ' || Glyph > '~') Glyph = '?';   /* char out of range: use question mark */
    else Glyph -= ' ';

    for (UINTN i = 0; i < (This->BaseGlyphSize.Height * Zoom); ++i) {   /* Rows */

        CHAR8 z = ORCHID_FONT_8x16[(Glyph * This->BaseGlyphSize.Height) + (i / Zoom)];

        for (UINTN j = (This->BaseGlyphSize.Width * Zoom); j > 0; --j) {
            if (!((j-1) % Zoom)) z >>= 1;

            /* NOTE: We are filling pixels from RIGHT TO LEFT so the logic here is reversed! */
            BOOLEAN IsShadowPixel = (!(z & 0x1) && ((z >> 1) & 0x1));

            This->SetPixel(This,
                           ObjectBltBuffer,
                           (X + (j - 1)),
                           (Y + i),
                           (IsShadowPixel && HasShadow)
                               ? (0)
                               : ((0x1 & z) ? Foreground : Background));
        }
    }

    return EFI_SUCCESS;
}


STATIC
EFIAPI
VOID
DrawLine(EFI_SIMPLE_FRAMEBUFFER_PROTOCOL *This,
         BOUNDED_SHAPE *ObjectBltBuffer,
         FB_VERTEX From,
         FB_VERTEX To,
         UINT32 Color)
{
    if (
        NULL == This
        || NULL == ObjectBltBuffer
    ) return;

    /* Check equality but also the subtraction because of overflow/underflow issues. */
    if (To.X == From.X || 0 == (To.X - From.X)) {
        /* Vertical line. No thanks on the DIV BY 0 error. */
        for (
            UINTN i = From.Y;
            (To.Y > From.Y) ? (i <= To.Y) : (i >= To.Y);
            i = (To.Y > From.Y) ? (i + 1) : (i - 1)
        ) This->SetPixel(This, ObjectBltBuffer, From.X, i, Color);

        return;
    }

    /* Convert these items to doubles for more precise computation. */
    double dAtX = (double)(From.X * 1.0), dAtY = (double)(From.Y * 1.0);
    double dToX = (double)(To.X * 1.0), dToY = (double)(To.Y * 1.0);

    /* Ah yes, "y = mx + b" finally comes in handy */
    double slope = (dToY - dAtY) / (dToX - dAtX);

    /* b = y - mx; we know (AtX, AtY) and (ToX, ToY) are points on the line. */
    double intercept = dToY - (slope * dToX);   /* approximate */

    for (
        UINTN x = From.X;
        (To.X > From.X) ? (x < To.X) : (x > To.X);
        x = (To.X > From.X) ? (x + 1) : (x - 1)
    ) {
        double y = (slope * (double)(x * 1.0)) + intercept;   /* classic */

        if (y < 0 || (UINT32)y >= ObjectBltBuffer->Dimensions.Height) continue;

        This->SetPixel(This, ObjectBltBuffer, x, (UINT32)y, Color);
    }
}


STATIC
EFIAPI
EFI_STATUS
DrawPolygon(EFI_SIMPLE_FRAMEBUFFER_PROTOCOL *This,
            BOUNDED_SHAPE *ObjectBltBuffer,
            UINTN VertexCount,
            FB_VERTEX *VertexList,
            UINT32 Color,
            BOOLEAN ConnectLastPoint)
{
    if (
        NULL == This
        || NULL == ObjectBltBuffer
        || VertexCount < 2
        || NULL == VertexList
    ) return EFI_INVALID_PARAMETER;

    UINTN i = 1;
    do {
        This->DrawSimpleShape(This, ObjectBltBuffer, FbShapeLine, VertexList[i - 1], VertexList[i], 0, FALSE, 1, Color);
        ++i;
    } while (i < VertexCount);

    if (ConnectLastPoint && VertexCount > 2) {
        This->DrawSimpleShape(This, ObjectBltBuffer, FbShapeLine, VertexList[VertexCount - 1], VertexList[0], 0, FALSE, 1, Color);
        // DrawLine(This, ObjectBltBuffer, VertexList[VertexCount - 1], VertexList[0], Color);
    }

    return EFI_SUCCESS;
}


STATIC
EFIAPI
EFI_STATUS
DrawBoxBorder(EFI_SIMPLE_FRAMEBUFFER_PROTOCOL *This,
              BOUNDED_SHAPE *ObjectBltBuffer,
              UINTN Thickness,
              BOOLEAN Gradient,
              UINT32 OuterColor,
              UINT32 InnerColor)
{
    return EFI_SUCCESS;
}


STATIC
EFIAPI
EFI_STATUS
DrawSimpleShape(EFI_SIMPLE_FRAMEBUFFER_PROTOCOL *This,
                BOUNDED_SHAPE *ObjectBltBuffer,
                FB_SHAPE_TYPE Type,
                FB_VERTEX From,
                FB_VERTEX To,
                UINTN RotationDegrees,
                BOOLEAN Fill,
                UINTN BorderThickness,
                UINT32 Color)
{
    if (
        NULL == This
        || NULL == ObjectBltBuffer
        || !Type
        || Type >= FbShapeMax
    ) return EFI_INVALID_PARAMETER;

    FB_VERTEX CurrentFrom = { .X = From.X, .Y = From.Y };
    FB_VERTEX CurrentTo   = { .X = To.X,   .Y = To.Y   };
    FB_VERTEX Least       = { .X = MIN(From.X, To.X), .Y = MIN(From.Y, To.Y) };   /* top left vertex */
    FB_VERTEX Most        = { .X = MAX(From.X, To.X), .Y = MAX(From.Y, To.Y) };   /* bottom right vertex */

    /* If the vertices are the same point, only put a dot regardless of shape. */
    if (From.X == To.X && From.Y == To.Y) {
        This->SetPixel(This, ObjectBltBuffer, From.X, To.X, Color);
        return EFI_SUCCESS;
    }

    /* Thickness should always be at least 1 regardless of fill. */
    BorderThickness = MAX(1, BorderThickness);

    switch (Type) {
        case FbShapeLine:
            for (UINTN i = 0; i < BorderThickness; ++i) {
                CurrentFrom.Y = From.Y + i;
                CurrentTo.Y = To.Y + i;
                DrawLine(This, ObjectBltBuffer, CurrentFrom, CurrentTo, Color);
            }
            break;

        case FbShapeRectangle:
            if (Fill) {
                /* Draw a single shape. */
                for (UINTN i = Least.Y; i <= Most.Y; ++i)
                    for (UINTN j = Least.X; j <= Most.X; ++j)
                        This->SetPixel(This, ObjectBltBuffer, j, i, Color);
            } else {
                CurrentFrom = Least;
                CurrentTo = Most;

                FB_VERTEX CurrentBottomLeft = { .X = CurrentFrom.X, .Y = CurrentTo.Y   };
                FB_VERTEX CurrentTopRight   = { .X = CurrentTo.X,   .Y = CurrentFrom.Y };

                /* Iterate each border with a box. */
                for (UINTN i = 0; i < BorderThickness; ++i) {
                    /* Exit the loop early if the rectangle has converged. */
                    if (CurrentFrom.X > CurrentTo.X || CurrentFrom.Y > CurrentTo.Y) break;

                    DrawLine(This, ObjectBltBuffer, CurrentFrom,        CurrentBottomLeft,  Color);   /* left line */
                    DrawLine(This, ObjectBltBuffer, CurrentTopRight,    CurrentTo,          Color);   /* right line */
                    DrawLine(This, ObjectBltBuffer, CurrentFrom,        CurrentTopRight,    Color);   /* top line */
                    DrawLine(This, ObjectBltBuffer, CurrentBottomLeft,  CurrentTo,          Color);   /* bottom line */

                    CurrentFrom.X = Least.X + i; CurrentFrom.Y = Least.Y + i;
                    CurrentTo.X = Most.X - i; CurrentTo.Y = Most.Y - i;

                    CurrentBottomLeft.X = CurrentFrom.X; CurrentBottomLeft.Y = CurrentTo.Y;
                    CurrentTopRight.X = CurrentTo.X;     CurrentTopRight.Y = CurrentFrom.Y;
                }
            }
            break;

        case FbShapeEquilateralTriangle:
                CurrentFrom = Least;
                CurrentTo = Most;

                FB_VERTEX CurrentBottomLeft = { .X = Least.X, .Y = Most.Y   };
                FB_VERTEX CurrentTopRight   = { .X = CurrentTo.X,   .Y = CurrentFrom.Y };
            break;

        case FbShapeCircle:
            break;

        default: return EFI_INVALID_PARAMETER;
    }

    return EFI_SUCCESS;
}


STATIC
EFIAPI
EFI_STATUS
RenderComponent(IN CONST EFI_SIMPLE_FRAMEBUFFER_PROTOCOL *This,
                IN BOUNDED_SHAPE *ObjectBltBuffer,
                IN BOOLEAN Flush)
{
    EFI_STATUS Status = EFI_SUCCESS;

    /* Copy the BLT up to the root BLT and publish it. */
    FB_VERTEX Origin = {0};
    FB->BltToBlt(FB,
                 FB->BLT,
                 ObjectBltBuffer,
                 ObjectBltBuffer->Position,
                 Origin,
                 ObjectBltBuffer->Dimensions);

    if (TRUE == Flush) {
        FB->FlushPartial(FB,
                         ObjectBltBuffer->Position.X, ObjectBltBuffer->Position.Y,
                         ObjectBltBuffer->Position.X, ObjectBltBuffer->Position.Y,
                         ObjectBltBuffer->Dimensions.Width, ObjectBltBuffer->Dimensions.Height);
    }

    return Status;
}


EFI_STATUS
FramebufferInit(EFI_GRAPHICS_OUTPUT_PROTOCOL *GOP)
{
    EFI_STATUS Status = EFI_SUCCESS;

    EFI_SIMPLE_FRAMEBUFFER_PROTOCOL *FrameBuff = (EFI_SIMPLE_FRAMEBUFFER_PROTOCOL *)
        AllocateZeroPool(sizeof(EFI_SIMPLE_FRAMEBUFFER_PROTOCOL));
    if (NULL == FrameBuff) {
        return EFI_OUT_OF_RESOURCES;
    }

    FrameBuff->GOP = GOP;
    FrameBuff->Flush            = FlushFramebuffer;
    FrameBuff->FlushPartial     = FlushFramebufferPartial;
    FrameBuff->GetPixel         = GetPixel;
    FrameBuff->SetPixel         = SetPixel;
    FrameBuff->ClearBlt         = ClearBlt;
    FrameBuff->BltToBlt         = FastBltToBlt;
    FrameBuff->ClearScreen      = ClearScreen;
    FrameBuff->RenderGlyph      = RenderGlyph;
    FrameBuff->DrawSimpleShape  = DrawSimpleShape;
    FrameBuff->DrawPolygon      = DrawPolygon;
    FrameBuff->RenderComponent  = RenderComponent;

    // TODO: Detect these with a dynamic font
    FrameBuff->BaseGlyphSize.Width  = 8;   /* default */
    FrameBuff->BaseGlyphSize.Height = 16;   /* default */
    
    FrameBuff->Resolution.Width  = GOP->Mode->Info->HorizontalResolution;
    FrameBuff->Resolution.Height = GOP->Mode->Info->VerticalResolution;
    FrameBuff->PixelsPerScanLine = GOP->Mode->Info->PixelsPerScanLine;

    /* Capture the physical address of the LFB in case we want it. */
    if (GOP->Mode->Info->PixelFormat != PixelBltOnly) {
        FrameBuff->LFB = GOP->Mode->FrameBufferBase;
    } else {
        FrameBuff->LFB = (EFI_PHYSICAL_ADDRESS)NULL;   /* explicit */
    }

    /* This is the pixel size and pitch of the LFB. We don't use it here, but we could
        optionally hand it down to a loaded kernel or system. */
    switch (GOP->Mode->Info->PixelFormat) {
        case PixelBitMask:
            EFI_PIXEL_BITMASK *p = &(GOP->Mode->Info->PixelInformation);
            UINT32 x = (p->RedMask | p->GreenMask | p->BlueMask | p->ReservedMask);

            UINTN HighestBit = 0;
            while (x >>= 1) ++HighestBit;

            FrameBuff->PixelSize = (HighestBit >> 3);
            break;
        default:
        case PixelBlueGreenRedReserved8BitPerColor:
        case PixelRedGreenBlueReserved8BitPerColor:
            FrameBuff->PixelSize = sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL); break;
    }
    FrameBuff->Pitch = (FrameBuff->PixelSize * FrameBuff->PixelsPerScanLine);

    /* Allocate the shadow buffer. X, Y, & ZIndex as 0 are all good. */
    ERRCHECK(NewObjectBlt(0, 0, FrameBuff->Resolution.Width,
        FrameBuff->Resolution.Height, 0, &(FrameBuff->BLT)));

    /* Ensure the precompiled font is available at runtime. */
    // TODO: Make this font system better
    if (
        NULL == font_psf
        || NULL == font_psf_len
    ) {
        EFI_DANGERLN("Missing incorporated font");
        return EFI_NOT_STARTED;
    }

    /* Cheat with CONST because we are thunderchads and do not care B) */
    CopyMem((VOID **)&FB, &FrameBuff, sizeof(EFI_SIMPLE_FRAMEBUFFER_PROTOCOL *));

    return EFI_SUCCESS;
}


VOID
BltDestroy(BOUNDED_SHAPE *Blt)
{
    if (NULL == Blt) return;

    FreePool((VOID *)(Blt->Buffer));
    FreePool(Blt);
}


VOID
FramebufferDestroy(VOID)
{
    if (NULL == FB) return;

    FreePool((VOID *)(FB->BLT->Buffer));
    FreePool(FB->BLT);
    FreePool(FB);
}


EFI_STATUS
NewObjectBlt(UINTN AtX,
             UINTN AtY,
             UINTN Width,
             UINTN Height,
             UINTN ZIndex,
             BOUNDED_SHAPE **Out)
{
    BOUNDED_SHAPE *ret = NULL;

    if (
        0 == Height
        || 0 == Width
        || AtX > FB->Resolution.Width
        || AtY > FB->Resolution.Height
        || NULL == Out
        || ZIndex > FB_Z_INDEX_MAX
    ) return EFI_INVALID_PARAMETER;

    if (NULL == (*Out)) {
        (*Out) = (BOUNDED_SHAPE *)AllocateZeroPool(sizeof(BOUNDED_SHAPE));
    }

    ret = (*Out);

    if (NULL != ret->Buffer) {
        FreePool((VOID *)(ret->Buffer));
    }

    ret->Dimensions.Width = Width;
    ret->Dimensions.Height = Height;
    ret->Position.X = AtX;
    ret->Position.Y = AtY;
    ret->ZIndex = ZIndex;

    /* The UEFI spec says with GOP, the framebuffer's physical pixel sizes and
        scanline offsets can be handled by the BLT abstraction. So here we continue
        to rely on that abstraction for any given/allocated BLT buffer. */
    ret->PixelSize = sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL);
    ret->Pitch = sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL) * Width;
    ret->BufferSize = ret->Pitch * Height;

    ret->Buffer = (EFI_PHYSICAL_ADDRESS)AllocateZeroPool(ret->BufferSize);
    if (NULL == ret->Buffer) return EFI_OUT_OF_RESOURCES;

    return EFI_SUCCESS;
}


VOID
BltPixelFromARGB(EFI_GRAPHICS_OUTPUT_BLT_PIXEL *p,
                 UINT32 Color)
{
    if (NULL == p) return;

    p->Red   = (0x00FF0000 & Color) >> 16;
    p->Green = (0x0000FF00 & Color) >> 8;
    p->Blue  = (0x000000FF & Color) >> 0;
    p->Reserved = 0x00;
}


UINT32
ARGBFromBltPixel(EFI_GRAPHICS_OUTPUT_BLT_PIXEL *p)
{
    if (NULL == p) return 0;

    return (
        ((p->Red & 0xFF) << 16)
        | ((p->Green & 0xFF) << 8)
        | ((p->Blue & 0xFF) << 0)
    );
}


VOID
BltPixelInvert(EFI_GRAPHICS_OUTPUT_BLT_PIXEL *p)
{
    if (NULL == p) return;

    p->Red   = (0xFF - p->Red);
    p->Green = (0xFF - p->Green);
    p->Blue  = (0xFF - p->Blue);
    p->Reserved = 0x00;
}
