#ifndef MULTIBOOT2_H
#define MULTIBOOT2_H

#include "../mftah_uefi.h"



#define MULTIBOOT_MAGIC_HEADER          0xe85250d6
#define MULTIBOOT_INFO_MAGIC            0x36d76289

#define MULTIBOOT_SEARCH_LIMIT          (1 << 15)   /* 32,768 bytes */
#define MULTIBOOT_SEARCH_ALIGNMENT      (1 << 3)   /* 8 bytes */

#define MULTIBOOT_EMPTY_TAG             { 0, 0, 8 }
#define MULTIBOOT_INFO_EMPTY_TAG        { 0, 8 }

#define MULTIBOOT_ARCHITECTURE_I386     0
#define MULTIBOOT_ARCHITECTURE_MIPS32   4

#define MULTIBOOT_RELOC_PREFERENCE_NONE 0
#define MULTIBOOT_RELOC_PREFERENCE_LOW  1
#define MULTIBOOT_RELOC_PREFERENCE_HIGH 2

#define MULTIBOOT_FLAG_TAG_OPTIONAL     1

#define MULTIBOOT_MEM_AVAILABLE         1
#define MULTIBOOT_MEM_RESERVED          2
#define MULTIBOOT_MEM_ACPI_RECLAIMABLE  3
#define MULTIBOOT_MEM_NVS               4
#define MULTIBOOT_MEM_BADRAM            5

#define MULTIBOOT_FB_TYPE_INDEXED       0
#define MULTIBOOT_FB_TYPE_RGB           1
#define MULTIBOOT_FB_TYPE_EGA_TEXT      2


// TODO: Convert typedef names to capitalized snake_case
typedef
enum {
    MB_END                  = 0,
    MB_INFO_REQ,
    MB_ADDR,
    MB_ENTRY,
    MB_FLAGS,
    MB_FRAMEBUFFER,
    MB_MOD_ALIGN,
    MB_EFI_BS,
    MB_I386_ENTRY,
    MB_AMD64_ENTRY,
    MB_RELOCATABLE,
} MultibootTagType;

typedef
enum {
    MBI_END                 = 0,
    MBI_CMD_LINE,
    MBI_LOADER_NAME,
    MBI_MODULES,
    MBI_BASIC_MEM,
    MBI_BIOS_BOOT_DEV,
    MBI_MEMORY_MAP,
    MBI_VBE_INFO,
    MBI_FRAMEBUFFER,
    MBI_ELF_SYMBOLS,
    MBI_APM,
    MBI_EFI_ST_32,
    MBI_EFI_ST_64,
    MBI_SMBIOS,
    MBI_ACPI_10,
    MBI_ACPI_20,
    MBI_NET,
    MBI_EFI_MEMORY_MAP,
    MBI_NO_EXIT_BOOT_SVCS,
    MBI_IMG_HND_32,
    MBI_IMG_HND_64,
    MBI_IMG_LOAD_BASE,
} MultibootInfoTagType;


/* Multiboot Headers & Tags (within the target kernel). */
typedef
struct {
    UINT32      Magic;
    UINT32      Architecture;
    UINT32      HeaderLength;
    UINT32      Checksum;
} MultibootMagicHeader;

typedef
struct {
    UINT16      Type;
    UINT16      Flags;
    UINT32      Size;
} MultibootTagHeader;

/* Serves as a tag whose existence alone within Multiboot2 headers
    signals a boot loader to take a certain action or provide extra info. */
typedef
struct {
    MultibootTagHeader      Header;
} MultibootTagBoolean;

/* Multiboot tag types. */
typedef
struct {
    MultibootTagHeader      Header;
    UINT32                  RequestedInfoTagTypes[];
} MultibootTagInfoRequest;

typedef
struct {
    MultibootTagHeader      Header;
    UINT32                  HeaderPhysAddr;
    UINT32                  TextLoadPhysAddr;
    UINT32                  LoadEndPhysAddr;
    UINT32                  BssEndPhysAddr;
} MultibootTagAddress;

/* This type works for MB_ENTRY, as well as arch-specific EFI ones. */
typedef
struct {
    MultibootTagHeader      Header;
    UINT32                  EntryPhysAddr;
} MultibootTagEntry;

typedef
struct {
    MultibootTagHeader      Header;
    UINT32                  Flags;
} MultibootTagFlags;

typedef
struct {
    MultibootTagHeader      Header;
    UINT32                  Width;
    UINT32                  Height;
    UINT32                  Depth;
} MultibootTagFramebuffer;

typedef
struct {
    MultibootTagHeader      Header;
    UINT32                  MinPhyAddr;
    UINT32                  MaxPhysAddr;
    UINT32                  Align;
    UINT32                  Preference;
} MultibootTagRelocatable;


/* Multiboot Information Structure Headers & Tags (provided by loaders). */
typedef
struct {
    UINT32      TotalSize;
    UINT32      Reserved;
} MultibootInfoHeader;

typedef
struct {
    UINT32      Type;
    UINT32      Size;
} MultibootInfoTagHeader;

/* Generic structure used for types consisting of just a pointer. */
typedef
struct {
    MultibootInfoTagHeader  Header;
    UINT32                  PhysicalAddress;
} MultibootInfoTagPointer32;
typedef
struct {
    MultibootInfoTagHeader  Header;
    UINT64                  PhysicalAddress;
} MultibootInfoTagPointer64;

/* Generic structure used for boolean tags. */
typedef
struct {
    MultibootInfoTagHeader  Header;
} MultibootInfoTagBoolean;

/* Multiboot Info structure tag types. */
typedef
struct {
    MultibootInfoTagHeader  Header;
    UINT8                   CmdLineStringData[];
} MultibootInfoTagCmdLine;

typedef
struct {
    MultibootInfoTagHeader  Header;
    CHAR                    *BootLoaderNameStringData;
} MultibootInfoTagLoaderName;

typedef
struct {
    MultibootInfoTagHeader  Header;
    UINT32                  ModulePhysAddrStart;
    UINT32                  ModulePhysAddrEnd;
    UINT8                   ModuleStringData[];
} MultibootInfoTagModule;

typedef
struct {
    MultibootInfoTagHeader  Header;
    UINT32                  MemoryLower;
    UINT32                  MemoryUpper;
} MultibootInfoTagBasicMemoryInfo;

typedef
struct {
    MultibootInfoTagHeader  Header;
    UINT32                  BiosDeviceNumber;
    UINT32                  Parition;
    UINT32                  SubPartition;
} MultibootInfoTagBiosBootDevice;

typedef
struct {
    UINT64      Base;
    UINT64      Length;
    UINT32      Type;
    UINT32      Reserved;
} MultibootMemoryMapEntry;
typedef
struct {
    MultibootInfoTagHeader  Header;
    UINT32                  EntrySize;
    UINT32                  EntryVersion;
    // MultibootMemoryMapEntry Entries[];
} MultibootInfoTagMemoryMap;

typedef
struct {
    MultibootInfoTagHeader  Header;
    UINT16                  VbeMode;
    UINT16                  VbeIfaceSegment;
    UINT16                  VbeIfaceOffset;
    UINT16                  VbeIfaceLength;
    UINT8                   VbeControlInfo[512];
    UINT8                   VbeModeInfo[256];
} MultibootInfoTagVbeInfo;

typedef
struct {
    UINT8       Red;
    UINT8       Green;
    UINT8       Blue;
} MultibootColor;
typedef
struct {
    MultibootInfoTagHeader  Header;
    UINT64                  FramebufferPhysAddr;
    UINT32                  Pitch;
    UINT32                  Width;
    UINT32                  Height;
    UINT8                   BitsPerPixel;
    UINT8                   Type;
    UINT16                  Reserved;
    union {
        struct {
            UINT32          NumberOfColors;
            MultibootColor  Colors[16];
        };
        struct {
            UINT8           RedFieldPosition;
            UINT8           RedMaskSize;
            UINT8           GreenFieldPosition;
            UINT8           GreenMaskSize;
            UINT8           BlueFieldPosition;
            UINT8           BlueMaskSize;
        };
    };
} MultibootInfoTagFramebuffer;

typedef
struct {
    MultibootInfoTagHeader  Header;
    UINT16                  NumberOfSections;
    UINT16                  EntrySize;
    UINT16                  SectionHeaderIndex;
    UINT16                  Reserved;
    UINT8                   SectionHeaders[];
} MultibootInfoTagElfSymbols;

typedef
struct {
    MultibootInfoTagHeader  Header;
    UINT16                  Version;
    UINT16                  CodeSegment;
    UINT32                  Offset;
    UINT16                  CodeSegment16;
    UINT16                  DataSegment16;
    UINT16                  Flags;
    UINT16                  CodeSegmentLength;
    UINT16                  CodeSegment16Length;
    UINT16                  DataSegment16Length;
} MultibootInfoTagApm;

typedef
struct {
    MultibootInfoTagHeader  Header;
    UINT8                   Major;
    UINT8                   Minor;
    UINT8                   Reserved[6];
    UINT8                   Tables[];
} MultibootInfoTagSmbios;

typedef
struct {
    MultibootInfoTagHeader  Header;
    UINT8                   Rsdp10[];
} MultibootInfoTagAcpi10;

typedef
struct {
    MultibootInfoTagHeader  Header;
    UINT8                   Rsdp20[];
} MultibootInfoTagAcpi20;

typedef
struct {
    MultibootInfoTagHeader  Header;
    UINT8                   DhcpAck[];
} MultibootInfoTagNetwork;

typedef
struct {
    MultibootInfoTagHeader  Header;
    UINT32                  DescriptorSize;
    UINT32                  DescriptorVersion;
    // UINT8                   EfiMemoryMap[];
} MultibootInfoTagEfiMemoryMap;



#endif   /* MULTIBOOT2_H */
