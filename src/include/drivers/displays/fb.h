#ifndef MFTAH_FRAMEBUFFER_H
#define MFTAH_FRAMEBUFFER_H

#include "../../mftah_uefi.h"
#include "../config.h"
#include "menu_structs.h"



#define FB_Z_INDEX_MAX  5



typedef
struct S_FB_PROTO __attribute__((packed))
EFI_SIMPLE_FRAMEBUFFER_PROTOCOL;

typedef
struct S_BOUNDED_SHAPE __attribute__((packed))
BOUNDED_SHAPE;


typedef
struct {
    UINTN   X;
    UINTN   Y;
} __attribute__((packed)) FB_VERTEX;

typedef
struct {
    UINTN   Width;
    UINTN   Height;
} __attribute__((packed)) FB_DIMENSION;

typedef
enum {
    FbShapeLine = 1,
    FbShapeRectangle,
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
    UINT8                   ZIndex;     /* Top-level precedence; higher = more prominent */
    UINTN                   BufferSize; /* Easy access to the buffer length */
    EFI_PHYSICAL_ADDRESS    Buffer;     /* Actual pixel/FB data location in physical memory */
    HOOK_DRAW               Draw;       /* Optional child method used to render the shape to its buffer. */
} __attribute__((packed));


// TODO! Add 'IN', 'OUT', 'CONST' etc qualifiers to these prototypes.
typedef
EFI_PHYSICAL_ADDRESS
(EFIAPI *FB_HOOK__GET_PIXEL)(
    EFI_SIMPLE_FRAMEBUFFER_PROTOCOL *This,
    BOUNDED_SHAPE                   *ObjectBltBuffer,
    UINTN                           X,
    UINTN                           Y
);

typedef
VOID
(EFIAPI *FB_HOOK__SET_PIXEL)(
    EFI_SIMPLE_FRAMEBUFFER_PROTOCOL *This,
    BOUNDED_SHAPE                   *ObjectBltBuffer,
    UINTN                           X,
    UINTN                           Y,
    UINT32                          Color
);

typedef
VOID
(EFIAPI *FB_HOOK__FLUSH)(
    EFI_SIMPLE_FRAMEBUFFER_PROTOCOL *This
);

typedef
VOID
(EFIAPI *FB_HOOK__FLUSH_PARTIAL)(
    EFI_SIMPLE_FRAMEBUFFER_PROTOCOL *This,
    UINTN                           X,
    UINTN                           Y,
    UINTN                           ToX,
    UINTN                           ToY,
    UINTN                           Width,
    UINTN                           Height
);

typedef
VOID
(EFIAPI *FB_HOOK__CLEAR_BLT)(
    EFI_SIMPLE_FRAMEBUFFER_PROTOCOL *This,
    BOUNDED_SHAPE                   *ObjectBltBuffer,
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL   *Pixel
);

typedef
EFI_STATUS
(EFIAPI *FB_HOOK__FAST_BLT_TO_BLT)(
    EFI_SIMPLE_FRAMEBUFFER_PROTOCOL *This,
    BOUNDED_SHAPE                   *DestinationBlt,
    BOUNDED_SHAPE                   *SourceBlt,
    FB_VERTEX                       IntoPosition,
    FB_VERTEX                       FromPosition,
    FB_DIMENSION                    Dimension
);

typedef
VOID
(EFIAPI *FB_HOOK__CLEAR_SCREEN)(
    EFI_SIMPLE_FRAMEBUFFER_PROTOCOL *This,
    UINT32                          Color
);

typedef
EFI_STATUS
(EFIAPI *FB_HOOK__RENDER_GLYPH)(
    EFI_SIMPLE_FRAMEBUFFER_PROTOCOL *This,
    BOUNDED_SHAPE                   *ObjectBltBuffer,
    CHAR8                           Glyph,
    UINTN                           X,
    UINTN                           Y,
    UINT32                          Foreground,
    UINT32                          Background,
    BOOLEAN                         HasShadow,
    UINTN                           Zoom
);

typedef
EFI_STATUS
(EFIAPI *FB_HOOK__DRAW_SIMPLE_SHAPE)(
    EFI_SIMPLE_FRAMEBUFFER_PROTOCOL *This,
    BOUNDED_SHAPE                   *ObjectBltBuffer,
    FB_SHAPE_TYPE                   Type,
    FB_VERTEX                       From,
    FB_VERTEX                       To,
    UINTN                           RotationDegrees,
    BOOLEAN                         Fill,
    UINTN                           BorderThickness,
    UINT32                          Color
);

typedef
EFI_STATUS
(EFIAPI *FB_HOOK__DRAW_POLYGON)(
    EFI_SIMPLE_FRAMEBUFFER_PROTOCOL *This,
    BOUNDED_SHAPE                   *ObjectBltBuffer,
    UINTN                           VertexCount,
    FB_VERTEX                       *VertexList,
    UINT32                          Color,
    BOOLEAN                         ConnectLastPoint
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
    FB_HOOK__DRAW_SIMPLE_SHAPE      DrawSimpleShape;
    FB_HOOK__DRAW_POLYGON           DrawPolygon;
    FB_HOOK__RENDER_COMPONENT       RenderComponent;
} __attribute__((packed));


EXTERN CONST EFI_SIMPLE_FRAMEBUFFER_PROTOCOL *FB;

/* Symbols from the pre-compiled PSF font. */
EXTERN CHAR8 font_psf;
EXTERN UINT32 font_psf_len;



EFI_STATUS
FramebufferInit(EFI_GRAPHICS_OUTPUT_PROTOCOL *GOP);


VOID
FramebufferDestroy(VOID);


EFI_STATUS
NewObjectBlt(
    UINTN AtX,
    UINTN AtY,
    UINTN Width,
    UINTN Height,
    UINTN ZIndex,
    BOUNDED_SHAPE **Out
);


VOID
BltDestroy(BOUNDED_SHAPE *Blt);


VOID
BltPixelFromARGB(
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL   *p,                 
    UINT32                          Color
);

VOID
BltPixelInvert(EFI_GRAPHICS_OUTPUT_BLT_PIXEL *p);

UINT32
ARGBFromBltPixel(EFI_GRAPHICS_OUTPUT_BLT_PIXEL *p);



#endif  /* MFTAH_FRAMEBUFFER_H */
