#ifndef MFTAH_ELF_LOADER_H
#define MFTAH_ELF_LOADER_H

#include "loader.h"


/*************************************************************/
typedef UINT32 ELF_STATUS;
#define ELF_SUCCESS                         0

#define ELFERR(x)                           (0x8000000 | (x))
#define ELF_ERROR(x)                        ((ELFERR(0) & (x)) != ELF_SUCCESS)

/* "Status" codes to use when loading and validating an ELF. */
#define ELF_INVALID_MAGIC                   ELFERR(1)
#define ELF_INVALID_CLASS                   ELFERR(2)
#define ELF_INVALID_ENCODING                ELFERR(3)
#define ELF_INVALID_VERSION                 ELFERR(4)
#define ELF_INVALID_TYPE                    ELFERR(5)
#define ELF_INVALID_MACHINE                 ELFERR(6)
#define ELF_INVALID_ENTRYPOINT              ELFERR(7)
#define ELF_INVALID_PROGRAM_HEADER_OFFSET   ELFERR(8)
#define ELF_INVALID_SECTION_HEADER_OFFSET   ELFERR(9)
#define ELF_INVALID_ELF_HEADER_LENGTH       ELFERR(10)
#define ELF_INVALID_PH_ENTRY_SIZE           ELFERR(11)
#define ELF_INVALID_SH_ENTRY_SIZE           ELFERR(12)
#define ELF_NO_SECTIONS                     ELFERR(13)
#define ELF_MISSING_SHSTRNDX                ELFERR(14)
#define ELF_PH_INVALID_SEGMENT_OFFSET       ELFERR(15)
#define ELF_PH_INVALID_VIRTUAL_ADDRESS      ELFERR(16)
#define ELF_PH_INVALID_PHYSICAL_ADDRESS     ELFERR(17)
#define ELF_PH_INVALID_ALIGNMENT            ELFERR(18)
#define ELF_PH_MISALIGNED_VIRTUAL_ADDRESS   ELFERR(19)
#define ELF_SH_INVALID_SHSTRTAB_OFFSET      ELFERR(20)
#define ELF_SH_INVALID_LOAD_ADDRESS         ELFERR(21)
#define ELF_SH_INVALID_OFFSET               ELFERR(22)
#define ELF_SH_INVALID_ALIGNMENT            ELFERR(23)
#define ELF_TOO_LARGE                       ELFERR(24)
#define ELF_UNSUPPORTED_CLASS               ELFERR(100)
#define ELF_UNSUPPORTED_ENCODING            ELFERR(101)
#define ELF_UNSUPPORTED_TYPE                ELFERR(102)
#define ELF_FAILURE_LOAD_SEGMENT            ELFERR(200)
#define ELF_GENERIC_ERROR                   ELFERR(999)
/*************************************************************/

/* ELF "magic" value. */
#define ELF_MAGIC_SIGNATURE     { 0x7F, 'E', 'L', 'F' }
#define ELF_MAGIC_DWORD         0x464C457F   /* the above but reversed */

/* Header 'identity' block size. */
#define EI_NDENT        16


/* ET = ELF Type designation */
typedef
enum {
    ET_NONE     = 0,
    ET_REL,
    ET_EXEC,
    ET_DYN,
    ET_CORE,
    ET_LOPROC   = 0xFF00,
    ET_HIPROC   = 0xFFFF,
} ElfType;

/* EM = ELF Machine designation */
typedef
enum {
    EM_NONE         = 0,
    EM_M32,                 /* AT&T WE 32100 */
    EM_SPARC,               /* SPARC */
    EM_386,                 /* Intel architecture */
    EM_68K,                 /* Motorola 68000 */
    EM_88K,                 /* Motorola 88000 */
    EM_860          = 7,    /* Intel 80860 */
    EM_MIPS,                /* MIPS RS3000 Big-Endian */
    EM_MIPS_RS4_RE  = 10,   /* MIPS RS4000 Big-Endian */
} ElfMachine;

/* EV = ELF Version designation */
typedef
enum {
    EV_NONE     = 0,
    EV_CURRENT,
} ElfVersion;

/* EC = ELF Class designation */
typedef
enum {
    EC_NONE     = 0,
    EC_32,
    EC_64,
} ElfClass;

/* ED = ELF Data designation */
typedef
enum {
    ED_NONE     = 0,
    ED_LSB,             /* Little-endian */
    ED_MSB,             /* Big-endian */
} ElfDataEncoding;


#pragma pack(push, 1)
typedef
struct {
    UINT32      Magic;
    UINT8       Class;
    UINT8       Data;
    UINT8       Version;
    UINT8       Pad[EI_NDENT - 7];
} ElfIdent;

typedef
struct {
    ElfIdent    Ident;
    UINT16      Type;
    UINT16      Machine;
    UINT32      Version;
    UINT32      Entry;
    UINT32      ProgramHeaderTableOffset;
    UINT32      SectionHeaderTableOffset;
    UINT32      Flags;
    UINT16      ElfHeaderSizeBytes;
    UINT16      ProgramHeaderEntrySizeBytes;
    UINT16      ProgramHeaderCount;
    UINT16      SectionHeaderEntrySizeBytes;
    UINT16      SectionHeaderCount;
    UINT16      StringTableSectionHeaderIndex;
} Elf32Header;

typedef
struct {
    ElfIdent    Ident;
    UINT16      Type;
    UINT16      Machine;
    UINT32      Version;
    UINT64      Entry;
    UINT64      ProgramHeaderTableOffset;
    UINT64      SectionHeaderTableOffset;
    UINT32      Flags;
    UINT16      ElfHeaderSizeBytes;
    UINT16      ProgramHeaderEntrySizeBytes;
    UINT16      ProgramHeaderCount;
    UINT16      SectionHeaderEntrySizeBytes;
    UINT16      SectionHeaderCount;
    UINT16      StringTableSectionHeaderIndex;
} Elf64Header;
#pragma pack(pop)


/* Special section header numbers. */
typedef
enum {
    SHN_UNDEFINED       = 0,
    SHN_LORESERVE       = 0xFF00,
    SHN_LOPROC          = 0xFF00,
    SHN_HIPROC          = 0xFF1F,
    SHN_ABS             = 0xFFF1,
    SHN_COMMON          = 0xFFF2,
    SHN_HIRESERVE       = 0xFFFF,
} ElfSpecialSectionIndex;


#pragma pack(push, 1)
typedef
struct {
    UINT32      StringTableOffset;
    UINT32      Type;
    UINT32      Flags;
    UINT32      VirtualAddress;
    UINT32      Offset;
    UINT32      Size;
    UINT32      LinkedSectionIndex;
    UINT32      ExtraInfo;
    UINT32      Alignment;
    UINT32      EntrySize;
} Elf32SectionHeader;

typedef
struct {
    UINT32      StringTableOffset;
    UINT32      Type;
    UINT64      Flags;
    UINT64      VirtualAddress;
    UINT64      Offset;
    UINT64      Size;
    UINT32      LinkedSectionIndex;
    UINT32      ExtraInfo;
    UINT64      Alignment;
    UINT64      EntrySize;
} Elf64SectionHeader;
#pragma pack(pop)


typedef
enum {
    PT_NULL     = 0,
    PT_LOAD,
    PT_DYNAMIC,
    PT_INTERP,
    PT_NOTE,
    PT_SHLIB,
    PT_PHDR,
    PT_TLS,
    PT_LOOS     = 0x60000000,
    PT_HIOS     = (0x70000000 - 1),
    PT_LOPROC   = 0x70000000,
    PT_HIPROC   = (0x80000000 - 1),
} ElfProgramHeaderType;

typedef
enum {
    PF_X        = 1,
    PF_W        = 2,
    PF_R        = 4,
} ElfProgramHeaderFlag;


#pragma pack(push, 1)
typedef
struct {
    UINT32      Type;
    UINT32      Offset;
    UINT32      VirtualAddress;
    UINT32      PhysicalAddress;
    UINT32      FileSize;
    UINT32      MemorySize;
    UINT32      Flags;
    UINT32      Alignment;
} Elf32ProgramHeader;

typedef
struct {
    UINT32      Type;
    UINT32      Flags;
    UINT64      Offset;
    UINT64      VirtualAddress;
    UINT64      PhysicalAddress;
    UINT64      FileSize;
    UINT64      MemorySize;
    UINT64      Alignment;
} Elf64ProgramHeader;
#pragma pack(pop)



typedef
struct {
    EFI_PHYSICAL_ADDRESS        LoadedImageAddr;
    EFI_PHYSICAL_ADDRESS        LoadedImageEntry;
    EFI_PHYSICAL_ADDRESS        ImageBegin;
    EFI_PHYSICAL_ADDRESS        ImageEnd;
    UINTN                       ImageSize;
    UINTN                       ImagePages;
    VOID                        *HeaderStart;
    VOID                        *ProgramHeaderStart;
} LOADED_ELF;   // TODO! Don't really need/use any of this information


EXTERN EFI_EXECUTABLE_LOADER ElfLoader;



#endif   /* MFTAH_ELF_LOADER_H */
