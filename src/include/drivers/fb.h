#ifndef MFTAH_FRAMEBUFFER_H
#define MFTAH_FRAMEBUFFER_H

#include "../mftah_uefi.h"
#include "config.h"
#include "displays.h"


// TODO! Clean up the (x,y) vs. FB_VERTEX args listings.
// TODO! Also simplify methods which just need a BLT instead of (x,y)-style parameters.
#define FB_Z_INDEX_MAX  5


typedef
struct S_FB_PROTO
EFI_SIMPLE_FRAMEBUFFER_PROTOCOL;

// TODO! Rename this object type to something more BLT-related and fix neatnesses
typedef
struct S_BOUNDED_SHAPE
BOUNDED_SHAPE;


#pragma pack(push, 1)
typedef
struct __attribute__((packed)) {
    UINTN   X;
    UINTN   Y;
} FB_VERTEX;

typedef
struct __attribute__((packed)) {
    UINTN   Width;
    UINTN   Height;
} FB_DIMENSION;
#pragma pack(pop)

typedef
enum {
    FbShapeLine = 1,
    FbShapeRectangle,
    // TODO! Is it worth implementing these unused shapes?
    FbShapeEquilateralTriangle,
    FbShapeCircle,
    FbShapeMax
} FB_SHAPE_TYPE;


typedef
VOID
(EFIAPI *HOOK_DRAW)(
    IN BOUNDED_SHAPE    *This,
    IN CONFIGURATION    *c,
    IN MENU_STATE       *m,
    IN VOID             *e
);


struct S_BOUNDED_SHAPE {
    FB_DIMENSION            Dimensions; /* Intended object dimensions (W x H) */
    FB_VERTEX               Position;   /* Intended position when rendered */
    UINTN                   PixelSize;  /* Size in bytes of a single pixel */
    UINTN                   Pitch;      /* The size of a single scan line of the buffer */
    UINTN                   BufferSize; /* Easy access to the buffer length */
    EFI_PHYSICAL_ADDRESS    Buffer;     /* Actual pixel/FB data location in physical memory */
    HOOK_DRAW               Draw;       /* Optional child method used to render the shape to its buffer. */
    UINT8                   ZIndex;     /* Top-level precedence; higher = more prominent */
};


typedef
EFI_PHYSICAL_ADDRESS
(EFIAPI *FB_HOOK__GET_PIXEL)(
    IN CONST    EFI_SIMPLE_FRAMEBUFFER_PROTOCOL *This,
    IN          BOUNDED_SHAPE                   *ObjectBltBuffer,
    IN          UINTN                           X,
    IN          UINTN                           Y
);

typedef
VOID
(EFIAPI *FB_HOOK__SET_PIXEL)(
    IN CONST    EFI_SIMPLE_FRAMEBUFFER_PROTOCOL *This,
    IN          BOUNDED_SHAPE                   *ObjectBltBuffer,
    IN          UINTN                           X,
    IN          UINTN                           Y,
    IN          UINT32                          Color
);

typedef
VOID
(EFIAPI *FB_HOOK__FLUSH)(
    IN CONST    EFI_SIMPLE_FRAMEBUFFER_PROTOCOL *This
);

typedef
VOID
(EFIAPI *FB_HOOK__FLUSH_PARTIAL)(
    IN CONST    EFI_SIMPLE_FRAMEBUFFER_PROTOCOL *This,
    IN          UINTN                           X,
    IN          UINTN                           Y,
    IN          UINTN                           ToX,
    IN          UINTN                           ToY,
    IN          UINTN                           Width,
    IN          UINTN                           Height
);

typedef
VOID
(EFIAPI *FB_HOOK__CLEAR_BLT)(
    IN CONST    EFI_SIMPLE_FRAMEBUFFER_PROTOCOL *This,
    IN          BOUNDED_SHAPE                   *ObjectBltBuffer,
    IN          EFI_GRAPHICS_OUTPUT_BLT_PIXEL   *Pixel
);

typedef
EFI_STATUS
(EFIAPI *FB_HOOK__FAST_BLT_TO_BLT)(
    IN CONST    EFI_SIMPLE_FRAMEBUFFER_PROTOCOL *This,
    IN          BOUNDED_SHAPE                   *DestinationBlt,
    IN          BOUNDED_SHAPE                   *SourceBlt,
    IN          FB_VERTEX                       IntoPosition,
    IN          FB_VERTEX                       FromPosition,
    IN          FB_DIMENSION                    Dimension
);

typedef
VOID
(EFIAPI *FB_HOOK__CLEAR_SCREEN)(
    IN CONST    EFI_SIMPLE_FRAMEBUFFER_PROTOCOL *This,
    IN          UINT32                          Color
);

typedef
EFI_STATUS
(EFIAPI *FB_HOOK__RENDER_GLYPH)(
    IN CONST    EFI_SIMPLE_FRAMEBUFFER_PROTOCOL *This,
    IN          BOUNDED_SHAPE                   *ObjectBltBuffer,
    IN          CHAR8                           Glyph,
    IN          UINTN                           X,
    IN          UINTN                           Y,
    IN          UINT32                          Foreground,
    IN          UINT32                          Background,
    IN          BOOLEAN                         HasShadow,
    IN          UINTN                           Zoom
);

typedef
EFI_STATUS
(EFIAPI *FB_HOOK__PRINT_STRING)(
    IN  CONST   EFI_SIMPLE_FRAMEBUFFER_PROTOCOL *This,
    IN          CHAR8                           *Str,
    IN          BOUNDED_SHAPE                   *ObjectBltBuffer,
    IN OUT      FB_VERTEX                       *Start,
    IN          COLOR_PAIR                      *Colors,
    IN          BOOLEAN                         Wrap,
    IN          UINT8                           FontScale
);

typedef
EFI_STATUS
(EFIAPI *FB_HOOK__DRAW_SIMPLE_SHAPE)(
    IN CONST    EFI_SIMPLE_FRAMEBUFFER_PROTOCOL *This,
    IN          BOUNDED_SHAPE                   *ObjectBltBuffer,
    IN          FB_SHAPE_TYPE                   Type,
    IN          FB_VERTEX                       From,
    IN          FB_VERTEX                       To,
    IN          UINTN                           RotationDegrees,
    IN          BOOLEAN                         Fill,
    IN          UINTN                           BorderThickness,
    IN          UINT32                          Color
);

typedef
EFI_STATUS
(EFIAPI *FB_HOOK__DRAW_POLYGON)(
    IN CONST    EFI_SIMPLE_FRAMEBUFFER_PROTOCOL *This,
    IN          BOUNDED_SHAPE                   *ObjectBltBuffer,
    IN          UINTN                           VertexCount,
    IN          FB_VERTEX                       *VertexList,
    IN          UINT32                          Color,
    IN          BOOLEAN                         ConnectLastPoint
);

typedef
EFI_STATUS
(EFIAPI *FB_HOOK__RENDER_COMPONENT)(
    IN CONST EFI_SIMPLE_FRAMEBUFFER_PROTOCOL    *This,
    IN BOUNDED_SHAPE                            *ObjectBltBuffer,
    IN BOOLEAN                                  Flush
);


struct S_FB_PROTO {
    EFI_GRAPHICS_OUTPUT_PROTOCOL    *GOP;
    BOUNDED_SHAPE                   *BLT;
    UINTN                           PixelSize;
    UINTN                           PixelsPerScanLine;
    UINT32                          Pitch;
    EFI_PHYSICAL_ADDRESS            LFB;
    FB_DIMENSION                    Resolution;
    FB_DIMENSION                    BaseGlyphSize;
    FB_HOOK__FLUSH                  Flush;
    FB_HOOK__FLUSH_PARTIAL          FlushPartial;
    FB_HOOK__GET_PIXEL              GetPixel;
    FB_HOOK__SET_PIXEL              SetPixel;
    FB_HOOK__CLEAR_BLT              ClearBlt;
    FB_HOOK__FAST_BLT_TO_BLT        BltToBlt;
    FB_HOOK__CLEAR_SCREEN           ClearScreen;
    FB_HOOK__RENDER_GLYPH           RenderGlyph;
    FB_HOOK__PRINT_STRING           PrintString;
    FB_HOOK__DRAW_SIMPLE_SHAPE      DrawSimpleShape;
    FB_HOOK__DRAW_POLYGON           DrawPolygon;
    FB_HOOK__RENDER_COMPONENT       RenderComponent;
};


/* Export of the protocol instance. */
EXTERN EFI_SIMPLE_FRAMEBUFFER_PROTOCOL *FB;


/* Symbols from the pre-compiled PSF font. */
EXTERN CHAR8 font_psf;
EXTERN UINT32 font_psf_len;



EFI_STATUS
FramebufferInit(IN EFI_GRAPHICS_OUTPUT_PROTOCOL *GOP);


VOID
FramebufferDestroy(VOID);


EFI_STATUS
NewObjectBlt(
    IN  UINTN           AtX,
    IN  UINTN           AtY,
    IN  UINTN           Width,
    IN  UINTN           Height,
    IN  UINTN           ZIndex,
    OUT BOUNDED_SHAPE   **Out
);


VOID
BltDestroy(IN BOUNDED_SHAPE *Blt);


VOID
BltDrawOutline(
    IN BOUNDED_SHAPE *Blt,
    IN UINT32 Color
);


VOID
BltPixelFromARGB(
    IN  EFI_GRAPHICS_OUTPUT_BLT_PIXEL   *p,                 
    IN  UINT32                          Color
);

VOID
BltPixelInvert(IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL *p);

UINT32
ARGBFromBltPixel(IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL *p);



#endif  /* MFTAH_FRAMEBUFFER_H */
