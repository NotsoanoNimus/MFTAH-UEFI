#include "../include/drivers/mftah_adapter.h"


STATIC mftah_protocol_t *MFTAH = NULL;


STATIC
VOID *
MftahUefi__wrapper__AllocateZeroPool(__SIZE_TYPE__ Count,
                                    __SIZE_TYPE__ Length)
{
    return AllocateZeroPool(Count * Length);
}


STATIC
VOID *
MftahUefi__wrapper__AllocatePool(__SIZE_TYPE__ Length)
{
    return AllocatePool(Length);
}


STATIC
VOID
MftahUefi__wrapper__FreePool(VOID *Pointer)
{
    FreePool(Pointer);
}


STATIC
VOID *
MftahUefi__wrapper__MemMove(VOID *Destination,
                           CONST VOID *Source,
                           __SIZE_TYPE__ Length)
{
    UINT8 SourceCopy = 0;

    /* Not the most performant operation but it works OK. */
    for (UINTN i = 0; i < Length; ++i) {
        SourceCopy = *((UINT8 *)Source++);
        *((UINT8 *)Destination++) = SourceCopy;
    }

    return Destination;
}


STATIC
VOID *
MftahUefi__wrapper__SetMem(VOID *Destination,
                          INT32 Value,
                          __SIZE_TYPE__ Size)
{
    SetMem(Destination, Size, (UINT8)Value);

    return NULL;
}


STATIC
VOID *
MftahUefi__wrapper__CopyMem(VOID *Destination,
                           CONST VOID *Source,
                           __SIZE_TYPE__ Size)
{
    CopyMem(Destination, (VOID *)Source, Size);

    return NULL;
}


STATIC
INT32
MftahUefi__wrapper__CompareMem(CONST VOID *Left,
                              CONST VOID *Right,
                              __SIZE_TYPE__ Length)
{
    return CompareMem(Left, Right, Length);
}


STATIC
VOID *
MftahUefi__wrapper__ReallocatePool(VOID *At,
                                  __SIZE_TYPE__ ToSize)
{
    /* UEFI isn't going to be bothered by simply READING (most) memory locations. */
    /* It's up to the caller to be aware that any INFLATION of a memory pool 'At' will,
        while preserving the previous data, also introduce GARBAGE DATA (not 0s) at its end. */
    VOID *NewPool = AllocatePool(ToSize);
    if (NULL == NewPool) {
        return NULL;
    }

    CopyMem(NewPool, At, ToSize);
    FreePool(At);

    return NewPool;
}


STATIC
VOID
MftahUefi__wrapper__Print(mftah_log_level_t Level,
                         CONST CHAR *Format,
                         ...)
{
#ifndef EFI_DEBUG
    if (MFTAH_LEVEL_DEBUG == Level) return;
#endif

    /* We actually don't want to output anything from the library that isn't important. */
    if (MFTAH_LEVEL_ERROR != Level) return;

    /* For prints, we assume that all strings and other details will be safely 
        and dynamically pre-converted to wide characters. */
    va_list c;
    va_start(c, Format);
    VPrint(Format, c);
    va_end(c);
}


EFI_STATUS
EFIAPI
MftahInit(VOID)
{
    mftah_status_t MftahStatus = MFTAH_SUCCESS;

    if (NULL != MFTAH) {
        return EFI_SUCCESS;
    }

    DPRINTLN("Loading and registering a new MFTAH protocol instance.");
    MFTAH = (mftah_protocol_t *)AllocateZeroPool(sizeof(mftah_protocol_t));
    MftahStatus = mftah_protocol_factory__create(MFTAH);
    if (MFTAH_ERROR(MftahStatus)) {
        return EFI_NOT_STARTED;
    }

    mftah_registration_details_t *HooksRegistration = (mftah_registration_details_t *)
        AllocateZeroPool(sizeof(mftah_registration_details_t));

    HooksRegistration->calloc      = MftahUefi__wrapper__AllocateZeroPool;
    HooksRegistration->malloc      = MftahUefi__wrapper__AllocatePool;
    HooksRegistration->realloc     = MftahUefi__wrapper__ReallocatePool;
    HooksRegistration->free        = MftahUefi__wrapper__FreePool;
    HooksRegistration->memcmp      = MftahUefi__wrapper__CompareMem;
    HooksRegistration->memcpy      = MftahUefi__wrapper__CopyMem;
    HooksRegistration->memset      = MftahUefi__wrapper__SetMem;
    HooksRegistration->memmove     = MftahUefi__wrapper__MemMove;
    HooksRegistration->printf      = MftahUefi__wrapper__Print;

    MftahStatus = MFTAH->register_hooks(MFTAH, HooksRegistration);
    FreePool(HooksRegistration);

    if (MFTAH_ERROR((MftahStatus))) {
        return EFI_NOT_READY;
    }

    return EFI_SUCCESS;
}


mftah_protocol_t *
EFIAPI
MftahGetInstance(VOID)
{
    return MFTAH;
}


VOID
EFIAPI
MftahDestroy(VOID)
{
    if (NULL != MFTAH) {
        FreePool(MFTAH);
        MFTAH = NULL;
    }
}
