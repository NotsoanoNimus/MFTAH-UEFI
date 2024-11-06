#ifndef MFTAH_CONFIG_H
#define MFTAH_CONFIG_H

#include "../mftah_uefi.h"



/* The menu can have, at most, 16 different options for booting.
    Change at your own risk: there's not really any scrolling atm. */
#define CONFIG_MAX_CHAINS   16

/* Possible chainload types available with MFTAH. */
typedef
enum {
    /* The payload should be loaded as a ramdisk image. */
    DISK    = (1 << 0),
    /* An EFI binary which can be chainloaded and jumped to. */
    EXE     = (1 << 2),
    /* An ELF binary which can be loaded and executed directly. */
    ELF     = (1 << 3),
    /* A raw binary image to transfer control to directly. */
    BIN     = (1 << 4),
} CHAIN_TYPE;

/* Options representing a config 'chain' block. */
typedef
struct {
    CHAR8       *Name;
    CHAR8       *PayloadPath;
    CHAR8       *TargetPath;   /* Inner EFI to chainload (for MFTAH_DISK types). */
    CHAR8       *MFTAHKey;   /* Prefilled password (or filled by prompt). */
    CHAIN_TYPE  Type;
    CHAIN_TYPE  SubType;
    BOOLEAN     IsMFTAH;
    BOOLEAN     IsCompressed;
    BOOLEAN     IsImmediate;
    BOOLEAN     IsDefault;
} __attribute__((packed)) CONFIG_CHAIN_BLOCK;

typedef
enum {
    TEXT        = (1 << 1),
    GRAPHICAL   = (1 << 2)
} DISPLAY_MODE;

typedef
struct {
    UINT32  Foreground;
    UINT32  Background;
} __attribute__((packed)) COLOR_PAIR;

typedef
struct {
    UINT32      Background;
    UINT32      Border;
    COLOR_PAIR  Text;
    COLOR_PAIR  Banner;
    COLOR_PAIR  Title;
    COLOR_PAIR  Timer;
    COLOR_PAIR  PromptPopup;
    COLOR_PAIR  InfoPopup;
    COLOR_PAIR  WarningPopup;
} CONFIG_COLORS;

/* A meta-structure holding the applications runtime configuration. This
    is initially populated with defaults upon driver initialization, but
    is populated by a call to `ConfigParse`. */
typedef
struct {
    CHAR8               *Title;
    DISPLAY_MODE        Mode;
    BOOLEAN             AutoMode;
    BOOLEAN             Quick;
    CONFIG_COLORS       Colors;
    UINT8               Scale;
    UINTN               Timeout;
    UINTN               MaxTimeout;
    CHAR8               *Banner;
    CONFIG_CHAIN_BLOCK  *Chains[CONFIG_MAX_CHAINS];
    UINTN               ChainsLength;
} __attribute__((packed)) CONFIGURATION;


/**
 * Initialize configuration and driver defaults.
 * 
 * @returns Whether the operation was a success.
 */
EFI_STATUS
EFIAPI
ConfigInit(VOID);


/**
 * De-init the MFTAH-UEFI configuration and all allocations.
 * 
 * @returns Whether the operation was a success.
 */
EFI_STATUS
EFIAPI
ConfigDestroy(VOID);


/**
 * De-init a single configuration chain item.
 * 
 * @returns Nothing.
 */
VOID
ConfigDestroyChain(CONFIG_CHAIN_BLOCK *c);


/**
 * Retrieve a heap copy of the current configuration.
 * 
 * @returns A pointer to a newly-allocated copy of the current runtime configuration. \
 *          The caller is responsible for freeing this. NULL on error or out of resources.
 */
CONFIGURATION *
ConfigGet(VOID);


/**
 * Dump debugging information about the current configuration.
 * 
 * @returns Nothing.
 */
VOID
ConfigDump(VOID);


/**
 * Output information about a selected menu chain to a character array.
 * Useful for menus/drivers querying a specific chain's details.
 * 
 * @param[in]   Chain       The chain to dump and describe.
 * @param[out]  ToBuffer    A callee-allocated buffer into which details will be printed.
 * 
 * @returns Nothing.
 */
VOID
ConfigDumpChain(
    IN CONFIG_CHAIN_BLOCK   *Chain,
    OUT CHAR8               **ToBuffer
);


/**
 * Parse a configuration from the given path relative to the boot drive.
 * 
 * @param[in]   RelativeImageHandle The image handle which can identify the device that the filename is relative to.
 * @param[in]   ConfigFilename      A full path to a configuration file on the device.
 * 
 * @retval  EFI_SUCCESS             The configuration was parsed successfully.
 * @retval  EFI_NOT_FOUND           The configuration file was not found.
 * @retval  EFI_OUT_OF_RESOURCES    There was an issue allocating the resources necessary to parse.
 * @retval  EFI_LOAD_ERROR          The configuration is formatted incorrectly or contains invalid options.
 */
EFI_STATUS
EFIAPI
ConfigParse(
    IN EFI_HANDLE   RelativeImageHandle,
    IN CONST CHAR16 *ConfigFilename
);



#endif   /* MFTAH_CONFIG_H */
