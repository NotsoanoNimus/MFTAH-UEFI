#include "../include/drivers/config.h"
#include "../include/core/util.h"



#define MAX_ELEMENT_LENGTH      32
#define MAX_DEFINITION_LENGTH   4096

/* Define this locally so all static methods can just reference it on ABORT. */
STATIC EFI_STATUS Status = EFI_INVALID_PARAMETER;


/* Generic error message string capture. */
STATIC CONST CHAR16 *ErrorMsg = L"x";
STATIC CONST CHAR16 *HackeneyedChainOnlyStr =
    L"Invalid Chain-only command outside of a Chain";


/* Whether the parser is reading inside a 'chain' block.
    Accessible from all methods. */
STATIC BOOLEAN IsWithinChain = FALSE;

/* Whether the parser is currently within a 'banner' definition. */
STATIC BOOLEAN IsReadingBanner = FALSE;


/* Configuration block structure. */
STATIC CONFIGURATION Configuration = {0};

/* Tuple structure to hold a color name (lowercase) corresponding
    to EFI color IDs and UINT64s representing graphical colors. */
typedef
struct {
    CONST CHAR  *Name;
    UINTN       TextColor;
    UINT32      GraphicalColor;
} _PACKED CONFIG_COMMON_COLOR_TUPLE;

/* Foreground color mappings - Names to some common colors useable
    in both modes at any time. */
CONST CONFIG_COMMON_COLOR_TUPLE CommonForegroundColors[] = {
    { "black",          EFI_BLACK,                  0x00000000 },
    { "blue",           EFI_BLUE,                   0xff000088 },
    { "green",          EFI_GREEN,                  0xff008800 },
    { "cyan",           EFI_CYAN,                   0xff008888 },
    { "red",            EFI_RED,                    0xff880000 },
    { "magenta",        EFI_MAGENTA,                0xff880088 },
    { "brown",          EFI_BROWN,                  0xff888800 },
    { "lightgray",      EFI_LIGHTGRAY,              0xff888888 },
    { "lightblue",      EFI_LIGHTBLUE,              0xff0000ff },
    { "lightgreen",     EFI_LIGHTGREEN,             0xff00ff00 },
    { "lightcyan",      EFI_LIGHTCYAN,              0xff00ffff },
    { "lightred",       EFI_LIGHTRED,               0xffff0000 },
    { "lightmagenta",   EFI_LIGHTMAGENTA,           0xffff00ff },
    { "yellow",         EFI_YELLOW,                 0xffffff00 },
    { "white",          EFI_WHITE,                  0xffffffff },
    { NULL, 0, 0 }
};

CONST CONFIG_COMMON_COLOR_TUPLE CommonBackgroundColors[] = {
    { "black",          EFI_BACKGROUND_BLACK,       0x00000000 },
    { "blue",           EFI_BACKGROUND_BLUE,        0xff000088 },
    { "green",          EFI_BACKGROUND_GREEN,       0xff008800 },
    { "cyan",           EFI_BACKGROUND_CYAN,        0xff008888 },
    { "red",            EFI_BACKGROUND_RED,         0xff880000 },
    { "magenta",        EFI_BACKGROUND_MAGENTA,     0xff880088 },
    { "brown",          EFI_BACKGROUND_BROWN,       0xff888800 },
    { "lightgray",      EFI_BACKGROUND_LIGHTGRAY,   0xff888888 },
    { NULL, 0, 0 }
};

/* Tuple structure to hold a command string (lowercase) corresponding
    to its generic handler method. */
typedef
struct {
    CONST CHAR  *Element;
    EFI_STATUS  (EFIAPI *Handler)(CHAR *Data);
} _PACKED CONFIG_HANDLER_TUPLE;

/* Construct a list of config handler methods. */
#define DECL_HANDLER(name) \
    STATIC EFIAPI EFI_STATUS ConfigHandler_##name(CHAR *Data)
#define DECL_TUPLE(element) \
    { #element, ConfigHandler_##element }

DECL_HANDLER(display);
DECL_HANDLER(automode);
DECL_HANDLER(timeout);
DECL_HANDLER(max_timeout);
DECL_HANDLER(banner);
DECL_HANDLER(title);
DECL_HANDLER(color_bg);
DECL_HANDLER(color_border);
DECL_HANDLER(color_text);
DECL_HANDLER(color_banner);
DECL_HANDLER(color_title);
DECL_HANDLER(color_timer);
DECL_HANDLER(color_popup_prompt);
DECL_HANDLER(color_popup_info);
DECL_HANDLER(color_popup_warning);
DECL_HANDLER(scale);
DECL_HANDLER(name);
DECL_HANDLER(payload);
DECL_HANDLER(target);
DECL_HANDLER(now);
DECL_HANDLER(type);
DECL_HANDLER(mftah);
DECL_HANDLER(default);


/* A mapping of all possible config elements to each of their handlers.
    There will be a looping function to handle config parsing that can
    gather the generic (void*) argument to the handler and pass it in
    for each encountered command. */
CONST CONFIG_HANDLER_TUPLE ConfigHandlers[] = {
    DECL_TUPLE(display),
    DECL_TUPLE(automode),
    DECL_TUPLE(timeout),
    DECL_TUPLE(max_timeout),
    DECL_TUPLE(banner),
    DECL_TUPLE(title),
    DECL_TUPLE(color_bg),
    DECL_TUPLE(color_border),
    DECL_TUPLE(color_text),
    DECL_TUPLE(color_banner),
    DECL_TUPLE(color_title),
    DECL_TUPLE(color_timer),
    DECL_TUPLE(color_popup_prompt),
    DECL_TUPLE(color_popup_info),
    DECL_TUPLE(color_popup_warning),
    DECL_TUPLE(scale),
    DECL_TUPLE(name),
    DECL_TUPLE(payload),
    DECL_TUPLE(target),
    DECL_TUPLE(now),
    DECL_TUPLE(type),
    DECL_TUPLE(mftah),
    DECL_TUPLE(default),
    { NULL, NULL }   /* Terminal NULL entry marks end of list. */
};


STATIC
EFIAPI
EFI_STATUS
ParseColorHexCode(CHAR *Input,
                  UINT32 *Out)
{
    if (
        NULL == Input
        || NULL == Output
        || AsciiStrLen(Input) < 8
    ) {
        ErrorMsg = L"Invalid `ParseColorHexCode` parameter";
        return EFI_INVALID_PARAMETER;
    }

    UINT32 FinalValue = 0;
    UINT8 NibbleValue = 0;

    for (UINTN i = 8; i > 0; --i) {
        CHAR p = Input[i - 1];

        /* To lower-case */
        if (p >= 'A' && p <= 'Z') p -= ' ';

        /* Convert the character to its actual value. */
        if (p >= 'a' && p <= 'f') {
            NibbleValue = (10 + (p - 'a'));
        } else if (p >= '0' && p <= '9') {
            NibbleValue = (p - '0');
        } else {
            ErrorMsg = L"Invalid hex code: bad character";
            return EFI_INVALID_PARAMETER;
        }

        /* Each parsed character is a nibble. Therefore, left-shift the interpreted nibble
            by 4 (length of a nibble) times the current index into the string. */
        FinalValue += (NibbleValue << (4 * (8 - i)));
    }

    *Out = FinalValue;
    return EFI_SUCCESS;
}


STATIC
EFIAPI
EFI_STATUS
ParseColor(CHAR *Input,
           COLOR_PAIR *Out)
{
    if (
        NULL == Input
        || AsciiStrLen(Input) <= 0
    ) {
        ErrorMsg = L"Invalid `ParseColor` parameter";
        return EFI_INVALID_PARAMETER;
    }

    EFI_STATUS Status = EFI_SUCCESS;
    CHAR *s = Input, *p = Input;
    CHAR Foreground[16] = {0};
    CHAR Background[16] = {0};
    UINTN ForegroundLength = 0, BackgroundLength = 0;

    /* Scroll until the first space or end-of-string. */
    while (*p >= '!' && *p <= '~' && *p) p = (CHAR *)((UINTN)p + 1);

    ForegroundLength = MIN(15, ((UINTN)p - (UINTN)s));
    CopyMem(Foreground, s, ForegroundLength);

    if (' ' == *p) {
        /* There are some spaces that are not just trailing whitespace. */
        while (*p && (*p < '!' || *p > '~')) p = (CHAR *)((UINTN)p + 1);

        /* If the string appears to end here, then jump to stowing colors. */
        if (!*p) goto ParseColor__Store;

        /* 'Background' color starts here. */
        s = p;

        /* Scroll until the first space or end-of-string. */
        while (*p >= '!' && *p <= '~' && *p) p = (CHAR *)((UINTN)p + 1);

        BackgroundLength = MIN(15, (UINTN)p - (UINTN)s);
        CopyMem(Background, s, BackgroundLength);
    }

ParseColor__Store:
    /* Now parse each color to determine the value. We assume the `display` value
        has already been properly parsed/set. If not, this will cause issues. So
        a rule of the config is that `display` MUST be set BEFORE any colors. */
    
    switch (Configuration.Mode) {
        case TEXT:
            /* In TEXT mode, search the list of well-known colors. If none match, error. */
            for (UINTN i = 0; ; ++i) {
                if (NULL == CommonForegroundColors[i].Name) {
                    ErrorMsg = L"That foreground color for TEXT mode was not found";
                    return EFI_INVALID_PARAMETER;
                }

                if (0 != CompareMem(Foreground, CommonForegroundColors[i].Name, ForegroundLength)) continue;

                Out->Foreground = CommonForegroundColors[i].TextColor;
                break;
            }

            if (BackgroundLength > 0) {
                for (UINTN i = 0; ; ++i) {
                    if (NULL == CommonBackgroundColors[i].Name) {
                        ErrorMsg = L"That background color for TEXT mode was not found";
                        return EFI_INVALID_PARAMETER;
                    }

                    if (0 != CompareMem(Background, CommonBackgroundColors[i].Name, BackgroundLength)) continue;

                    Out->Background = CommonBackgroundColors[i].TextColor;
                    break;
                }
            }

            break;

        case GRAPHICAL:
            /* GRAPHICAL mode is a little more complicated. If the string starts with the '%'
                symbol, then it's a raw color. Otherwise, it should be looked up. */
            if ('%' != Foreground[0]) {
                for (UINTN i = 0; ; ++i) {
                    if (NULL == CommonForegroundColors[i].Name) {
                        ErrorMsg = L"That foreground color for GRAPHICAL mode was not found";
                        return EFI_INVALID_PARAMETER;
                    }

                    if (0 != CompareMem(Foreground, CommonForegroundColors[i].Name, ForegroundLength)) continue;

                    Out->Foreground = CommonForegroundColors[i].GraphicalColor;
                    break;
                }
            } else {
                if (AsciiStrLen(Foreground) != 9) {   /* Ex: '%ffaabbcc' <-- note the leading '%' in the length */
                    ErrorMsg = L"That foreground color is not set to a valid hex code.";
                    return EFI_INVALID_PARAMETER;
                }

                ERRCHECK(ParseColorHexCode(&(Foreground[1]), &(Out->Foreground)));
            }

            if (BackgroundLength > 0) {
                if ('%' != Background[0]) {
                    for (UINTN i = 0; ; ++i) {
                        if (NULL == CommonBackgroundColors[i].Name) {
                            ErrorMsg = L"That background color for GRAPHICAL mode was not found";
                            return EFI_INVALID_PARAMETER;
                        }

                        if (0 != CompareMem(Background, CommonBackgroundColors[i].Name, BackgroundLength)) continue;

                        Out->Background = CommonBackgroundColors[i].GraphicalColor;
                        break;
                    }
                } else {
                    if (AsciiStrLen(Background) != 9) {   /* Ex: '%ffaabbcc' <-- note the leading '%' in the length */
                        ErrorMsg = L"That background color is not set to a valid hex code.";
                        return EFI_INVALID_PARAMETER;
                    }

                    ERRCHECK(ParseColorHexCode(&(Background[1]), &(Out->Background)));
                }
            }

            break;

        default:
            ErrorMsg = L"Unknown `display` mode setting";
            return EFI_LOAD_ERROR;
    }

    return EFI_SUCCESS;
}


STATIC
EFIAPI
EFI_STATUS
SanitizeAndHandle(CHAR *End,
                  CHAR *Start,
                  CHAR *Delim)
{
    *End = '\0';  /* Can immediately convert the ';' to a terminator */
    DPRINTLN("FULL LINE: [[[%a]]] | DELIM [%c]\r\n", Start, *Delim);

    /* Cut at any whitespace within the element (they're always one word). */
    CHAR *p = Start, *s = Start;
    UINTN ElementLength = 0;

    CHAR Element[MAX_ELEMENT_LENGTH + 1] = {0};
    CHAR Definition[MAX_DEFINITION_LENGTH + 1] = {0};

SeekElementStart:
    while (*p && p < Delim && (*p < '!' || *p > '~')) p = (CHAR *)((UINTN)p + 1);
    if ('#' == *p) {   /* Always ignore comments until the end of a line. */
        while (*p && '\n' != *p) p = (CHAR *)((UINTN)p + 1);
        goto SeekElementStart;   /* Keep seeking */
    }
    s = p;   /* Capture the location of the first non-comment character. */

    while (*p && p < Delim && *p >= '!' && *p <= '~') p = (CHAR *)((UINTN)p + 1);
    ElementLength = ((UINTN)p - (UINTN)s);

    /* Convert the element name to lower case. */
    for (p = s; p < (CHAR *)((UINTN)s + ElementLength); p = (CHAR *)((UINTN)p + 1)) {
        if (*p <= 'Z' && *p >= 'A') *p += ' ';
    }

    CopyMem(Element, s, MIN(MAX_ELEMENT_LENGTH, ElementLength));

    /* Trim out the leading and trailing whitespaces of the definition value. */
    if (0 != CompareMem(Element, "banner", 6)) {
        p = (CHAR *)((UINTN)Delim + 1);

        while (*p && p < End && (*p < '!' || *p > '~')) p = (CHAR *)((UINTN)p + 1);
        s = p;   /* Record the first printable character. */

        while (*p && p < End && *p >= ' ' && *p <= '~') p = (CHAR *)((UINTN)p + 1);
        *p = '\0';

        /* Now walk backwards from the end until it's a non-whitespace character and mark that. */
        CHAR *x = (CHAR *)((UINTN)p - 1);
        while (*x == ' ') x = (CHAR *)((UINTN)x - 1);
        p = (CHAR *)((UINTN)x + 1);
        *p = '\0';
    } else {
        /* Always copy the banner verbatim. */
        s = (CHAR *)((UINTN)Delim + 1);
        p = End;
    }

    CopyMem(Definition, s, MIN(MAX_DEFINITION_LENGTH, ((UINTN)p - (UINTN)s)));

    /* Because You Never Know... (tm) */
    Element[MAX_ELEMENT_LENGTH] = '\0';
    Definition[MAX_DEFINITION_LENGTH] = '\0';

    // EFI_COLOR(MFTAH_COLOR_DEBUG);
    // PRINT("Parsing configuration element `%a`  [[[%a]]]", Element, Definition);
    // EFI_COLOR(MFTAH_COLOR_DEFAULT);
    // PRINT("\r\n");

    if (0 == Element[0] || 0 == Definition[0]) {
        ErrorMsg = L"Missing element name or definition";
        return EFI_INVALID_PARAMETER;
    }

    for (UINTN i = 0; ; ++i) {
        if (0 == CompareMem(ConfigHandlers[i].Element, Element, ElementLength + 1)) {
            return ConfigHandlers[i].Handler(Definition);
        }

        if (NULL == ConfigHandlers[i].Element) {
            ErrorMsg = L"The element name is not valid or understood.";
            return EFI_NOT_FOUND;
        }
    }
}

STATIC
EFIAPI
EFI_STATUS
PostProcessConfig(VOID)
{
    // TODO! Process the loaded config info to ensure validity
    // This includes sanity checks like null pointers, invalid/munged data, etc.
    return EFI_SUCCESS;
}


EFI_STATUS
EFIAPI
ConfigInit(VOID)
{
    SetMem(&Configuration, sizeof(CONFIGURATION), 0x00);

    /* No chains are specified by default. */
    Configuration.Title             = "MFTAH Chainloader";
    Configuration.Banner            = "Select a payload to chainload.";
    Configuration.Mode              = GRAPHICAL;
    Configuration.AutoMode          = TRUE;
    Configuration.Scale             = 1;
    Configuration.Timeout           = 5 * 1000;
    Configuration.MaxTimeout        = 300 * 1000;

    /* Color defaults are initialized when the Mode is parsed from the configuration. */
    return EFI_SUCCESS;
}


EFI_STATUS
EFIAPI
ConfigDestroy(VOID)
{
    FreePool(Configuration.Title);
    FreePool(Configuration.Banner);

    for (UINTN i = 0; i < Configuration.ChainsLength; ++i) {
        ConfigDestroyChain(Configuration.Chains[i]);
        Configuration.Chains[i] = NULL;
    }

    return EFI_SUCCESS;
}


VOID
ConfigDestroyChain(CONFIG_CHAIN_BLOCK *c)
{
    FreePool(c->Name);
    FreePool(c->PayloadPath);
    FreePool(c->TargetPath);

    FreePool(c);
}


CONFIGURATION *
ConfigGet(VOID)
{
    CONFIGURATION *copy = (CONFIGURATION *)AllocatePool(sizeof(CONFIGURATION));

    if (NULL == copy) return NULL;
    
    CopyMem(copy, &Configuration, sizeof(CONFIGURATION));
    return copy;
}


VOID
ConfigDump(VOID)
{
    PRINTLN("===== CONFIGURATION DUMP =====");
    PRINTLN("Mode(%u) // auto(%u) // Timeout(%u) // MaxTimeout(%u) // Scale(%u)",
        Configuration.Mode, Configuration.AutoMode, Configuration.Timeout,
        Configuration.MaxTimeout, Configuration.Scale);
    PRINTLN("Title:  '%a'", Configuration.Title);
    PRINTLN("Banner:  '%a'", Configuration.Banner);
    PRINTLN("Colors:  bg(%08x), text(%08x / %08x), border(%08x / %08x)",
        Configuration.Colors.Background, Configuration.Colors.Text.Foreground, Configuration.Colors.Text.Background,
        Configuration.Colors.Border, Configuration.Colors.Border);
    PRINTLN("         title(%08x / %08x), banner(%08x / %08x), timer(%08x / %08x)",
        Configuration.Colors.Title.Foreground, Configuration.Colors.Title.Background,
        Configuration.Colors.Banner.Foreground, Configuration.Colors.Banner.Background,
        Configuration.Colors.Timer.Foreground, Configuration.Colors.Timer.Background);

    PRINTLN("Chains(%u):", Configuration.ChainsLength);
    PRINTLN("---");
    for (UINTN i = 0; i < Configuration.ChainsLength; ++i) {
        PRINTLN("   name(%a), payload(%a)",
            Configuration.Chains[i]->Name ? Configuration.Chains[i]->Name : "NULL",
            Configuration.Chains[i]->PayloadPath ? Configuration.Chains[i]->PayloadPath : "NULL");
        PRINTLN("   target(%a), type(%u), mftah(%u), compressed(0)",
            Configuration.Chains[i]->TargetPath ? Configuration.Chains[i]->TargetPath : "NULL",
            Configuration.Chains[i]->Type,
            !!(Configuration.Chains[i]->IsMFTAH));
        PRINTLN("   default(%u), immediate(%u)",
            !!(Configuration.Chains[i]->IsDefault),
            !!(Configuration.Chains[i]->IsImmediate));
        PRINTLN("---");
    }

    PRINTLN("\r\n");
}


VOID
ConfigDumpChain(IN CONFIG_CHAIN_BLOCK *Chain,
                OUT CHAR8 **ToBuffer)
{
    if (NULL == ToBuffer) return;

    *ToBuffer = (CHAR8 *)AllocateZeroPool(sizeof(CHAR8) * (512 + 1));

    AsciiSPrint(
        (*ToBuffer), 512,
        "Chain '%a':\n   payload(%a),\n   target(%a),\n   type(%u),  flags(%1u:%1u:%1u:%1u)",
        Chain->Name, Chain->PayloadPath, Chain->TargetPath,
        Chain->Type, Chain->IsMFTAH, 0, Chain->IsDefault, Chain->IsImmediate
    );
}


EFI_STATUS
EFIAPI
ConfigParse(IN EFI_HANDLE RelativeImageHandle,
            IN CONST CHAR16 *ConfigFilename)
{
    /* Attempt to load the file from the boot disk. */
    EFI_STATUS Status = EFI_SUCCESS;

    UINT8 *ReadFileBuffer = NULL;
    UINTN ReadFileLength = 0;

    UINTN CurrentFilePosition = 0;

    PRINT("\r\nReading configuration file at '%s'...   ", ConfigFilename);

    /* Read the file's data into the given buffer handle. */
    Status = ReadFile(RelativeImageHandle,
                      ConfigFilename,
                      0U,
                      &ReadFileBuffer,
                      &ReadFileLength,
                      TRUE,
                      EfiBootServicesData,
                      NULL);
    if (EFI_ERROR(Status)) {
        EFI_DANGERLN("`ReadFile` returned a fatal error.");
        return Status;
    }
    PRINTLN("ok");

    /* Get each command name and block and pass it off to a handler. */
    /* Got me like.. CAST CAST CAST CAST CAST CAST CAST CAST CAST CAST  */
    for (
        /* 'p' is the scrolling end pos ';', 's' is the starting pos, 'd' is the '=' */
        CHAR *p = (CHAR *)ReadFileBuffer, *s = (CHAR *)ReadFileBuffer, *d = (CHAR *)ReadFileBuffer;
        *p && p < (CHAR *)((UINTN)ReadFileBuffer + ReadFileLength);
        p = (CHAR *)((UINTN)p + 1)   /* pointer arithmetic amirite */
    ) {
        /* Always update the current file position offset to hint at the block 's'. */
        CurrentFilePosition = ((UINTN)s - (UINTN)ReadFileBuffer);

        switch (*p) {
            case '#':   /* Always skip to the end of lines with comments */
                while (*p && '\n' != *p) p = (CHAR *)((UINTN)p + 1);
                break;

            case ';':
                /* We hit a stop for the current element/definition. Parse it. */
                if (s == d || (UINTN)d == ((UINTN)p - 1)) {   /* Start == Delim? There must be no '=' in the statement */
                    ErrorMsg = L"No definition was found for the element";
                    goto ConfigParse__error;
                }

                /* Sanitize and parse the block. */
                Status = SanitizeAndHandle(p, s, d);
                if (EFI_ERROR(Status)) {
                    goto ConfigParse__error;
                }

                d = s = (CHAR *)((UINTN)p + 1);   /* The next start is after the ';' */
                break;

            case '=':
                if (d != s) break;   /* Only move the 'Delim' pointer once per block. */

                /* Update the Delim position. */
                d = p;
                break;

            case '{':
                /* It doesn't matter if this is encountered multiple times. It's idempotent. */
                /*   REVISION: Meh, just let the person know there's a mistake. */
                if (IsWithinChain) {
                    ErrorMsg = L"Already within a Chain. You cannot use another '{' here";
                    goto ConfigParse__error;
                }

                if (Configuration.ChainsLength >= CONFIG_MAX_CHAINS) {
                    ErrorMsg = L"You have defined too many chains. The limit is 16.";
                    goto ConfigParse__error;
                }

                IsWithinChain = TRUE;
                *p = ' ';   /* Convert the indicator to useless whitespace */

                Configuration.Chains[Configuration.ChainsLength] =
                    (CONFIG_CHAIN_BLOCK *)AllocateZeroPool(sizeof(CONFIG_CHAIN_BLOCK));

                if (NULL == Configuration.Chains[Configuration.ChainsLength]) {
                    ErrorMsg = L"Failed to allocate chain: out of resources";
                    return EFI_OUT_OF_RESOURCES;
                }

                break;

            case '}':
                if (!IsWithinChain) {
                    /* But this one _does_ matter where and how we encounter it. */
                    ErrorMsg = L"Not within a Chain. You cannot use a '}' here";
                    goto ConfigParse__error;
                }

                /* Increment the chain indexer and leave Chain mode. */
                ++Configuration.ChainsLength;

                IsWithinChain = FALSE;
                *p = ' ';   /* Convert the indicator to useless whitespace */
                break;
        }
    }

    /* What about Chain mode? */
    if (IsWithinChain) {
        ErrorMsg = L"The last Chain was not properly closed with a '}'";
        goto ConfigParse__error;
    }

    Status = EFI_SUCCESS;
    goto ConfigParse__clean_up_and_exit;

ConfigParse__error:
    EFI_DANGERLN("FATAL: CONFIG( %s:%u ):  %s.", ConfigFilename, CurrentFilePosition, ErrorMsg);
    Status = EFI_LOAD_ERROR;

ConfigParse__clean_up_and_exit:
    FreePool(ReadFileBuffer);

    /* Don't post-process on error. */
    if (EFI_SUCCESS == Status) {
        Status = PostProcessConfig();
    }

    return Status;
}



DECL_HANDLER(display)
{
    /* Convert to lower-case. */
    if (*Data < 'a') *Data += ' ';

    switch (*Data) {
        case 'g':
            Configuration.Mode = GRAPHICAL;

            Configuration.Colors.Background = 0x00000000;
            Configuration.Colors.Border = 0xffeeeeee;
            Configuration.Colors.Text.Foreground = 0xffeeeeee;
            Configuration.Colors.Text.Background = 0x00000000;
            Configuration.Colors.Banner.Foreground = 0xff00ff00;
            Configuration.Colors.Banner.Background = 0x00000000;
            Configuration.Colors.Title.Foreground = 0xff12340b;
            Configuration.Colors.Title.Background = 0x00000000;
            Configuration.Colors.Timer.Foreground = 0xffeeeeee;
            Configuration.Colors.Timer.Background = 0x00000000;
            Configuration.Colors.PromptPopup.Foreground = 0xffeeeeee;
            Configuration.Colors.PromptPopup.Background = 0xffbb2299;
            Configuration.Colors.InfoPopup.Foreground = 0xffeeeeee;
            Configuration.Colors.InfoPopup.Background = 0xff2222bb;
            Configuration.Colors.WarningPopup.Foreground = 0xffeeeeee;
            Configuration.Colors.WarningPopup.Background = 0xffbb2222;

            break;

        case 't':
            Configuration.Mode = TEXT;

            Configuration.Colors.Background = EFI_BACKGROUND_BLACK;
            Configuration.Colors.Border = EFI_BACKGROUND_LIGHTGRAY;
            Configuration.Colors.Text.Foreground = EFI_WHITE;
            Configuration.Colors.Text.Background = EFI_BACKGROUND_BLACK;
            Configuration.Colors.Banner.Foreground = EFI_LIGHTGREEN;
            Configuration.Colors.Banner.Background = EFI_BACKGROUND_BLACK;
            Configuration.Colors.Title.Foreground = EFI_YELLOW;
            Configuration.Colors.Title.Background = EFI_BACKGROUND_BLACK;
            Configuration.Colors.Timer.Foreground = EFI_WHITE;
            Configuration.Colors.Timer.Background = EFI_BACKGROUND_BLACK;
            Configuration.Colors.PromptPopup.Foreground = EFI_WHITE;
            Configuration.Colors.PromptPopup.Background = EFI_BACKGROUND_MAGENTA;
            Configuration.Colors.InfoPopup.Foreground = EFI_WHITE;
            Configuration.Colors.InfoPopup.Background = EFI_BACKGROUND_BLUE;
            Configuration.Colors.WarningPopup.Foreground = EFI_YELLOW;
            Configuration.Colors.WarningPopup.Background = EFI_BACKGROUND_RED;

            break;

        default:
            ErrorMsg = L"Invalid `display` type. Options are 'g' (Graphical) and 't' (Text)";
            return EFI_INVALID_PARAMETER;
    }

    return EFI_SUCCESS;
}


DECL_HANDLER(automode)
{
    Configuration.AutoMode = !!AsciiAtoi((CONST CHAR8 *)Data);
    DPRINTLN("AUTO MODE SET");

    return EFI_SUCCESS;
}


DECL_HANDLER(timeout)
{
    Configuration.Timeout = AsciiAtoi((CONST CHAR8 *)Data);
    if (-1U == Configuration.Timeout) {
        ErrorMsg = L"Definition of `timeout` is not a number";
        return EFI_INVALID_PARAMETER;
    }

    /* The maximum allowable keypress timeout is 180 seconds. */
    Configuration.Timeout = MIN(180 * 1000, Configuration.Timeout);

    DPRINTLN("TIMEOUT SET TO %u MILLISECONDS.", Configuration.Timeout);

    return EFI_SUCCESS;
}


DECL_HANDLER(max_timeout)
{
    Configuration.MaxTimeout = AsciiAtoi((CONST CHAR8 *)Data);
    if (-1U == Configuration.MaxTimeout) {
        ErrorMsg = L"Definition of `max_timeout` is not a number";
        return EFI_INVALID_PARAMETER;
    }

    /* The maximum allowable global timeout is 1 hour (3600 seconds). */
    Configuration.MaxTimeout = MIN(60 * 60 * 1000, Configuration.MaxTimeout);

    DPRINTLN("MAX TIMEOUT SET TO %u MILLISECONDS.", Configuration.MaxTimeout);

    return EFI_SUCCESS;
}


DECL_HANDLER(banner)
{
    UINTN Len = AsciiStrLen((CONST CHAR8 *)Data);

    CHAR8 *banner = (CHAR8 *)AllocateZeroPool(Len + 1);
    if (NULL == banner) {
        ErrorMsg = L"Failed to copy `banner` text: out of resources";
        return EFI_OUT_OF_RESOURCES;
    }

    CopyMem(banner, Data, Len);
    Configuration.Banner = banner;
    DPRINTLN("BANNER ALLOCATED AND SET.");

    return EFI_SUCCESS;
}


DECL_HANDLER(title)
{
    UINTN Len = AsciiStrLen((CONST CHAR8 *)Data);

    CHAR8 *title = (CHAR8 *)AllocateZeroPool(Len + 1);
    if (NULL == title) {
        ErrorMsg = L"Failed to copy `title` text: out of resources";
        return EFI_OUT_OF_RESOURCES;
    }

    CopyMem(title, Data, Len);
    Configuration.Title = title;
    DPRINTLN("TITLE ALLOCATED AND SET.");

    return EFI_SUCCESS;
}


DECL_HANDLER(color_bg)
{
    EFI_STATUS Status = EFI_SUCCESS;

    COLOR_PAIR pair = {0};
    
    /* `ParseColor` takes care of setting error messages. */
    ERRCHECK(ParseColor(Data, &pair));

    /* The parsed foreground in this case is the lone color here, even if the
        user specified a pair of colors. */
    Configuration.Colors.Background = pair.Foreground;
    DPRINTLN("BACKGROUND COLOR SET.");

    return EFI_SUCCESS;
}


DECL_HANDLER(color_border)
{
    EFI_STATUS Status = EFI_SUCCESS;

    COLOR_PAIR pair = {0};
    
    /* `ParseColor` takes care of setting error messages. */
    ERRCHECK(ParseColor(Data, &pair));

    /* The parsed foreground in this case is the lone color here, even if the
        user specified a pair of colors. */
    Configuration.Colors.Border = pair.Foreground;
    DPRINTLN("BORDER COLOR SET.");

    return EFI_SUCCESS;
}


DECL_HANDLER(color_text)
{
    EFI_STATUS Status = EFI_SUCCESS;
    
    /* `ParseColor` takes care of setting error messages. */
    ERRCHECK(ParseColor(Data, &(Configuration.Colors.Text)));
    DPRINTLN("TEXT COLOR SET.");

    return EFI_SUCCESS;
}


DECL_HANDLER(color_banner)
{
    EFI_STATUS Status = EFI_SUCCESS;
    
    /* `ParseColor` takes care of setting error messages. */
    ERRCHECK(ParseColor(Data, &(Configuration.Colors.Banner)));
    DPRINTLN("BANNER COLOR SET.");

    return EFI_SUCCESS;
}


DECL_HANDLER(color_title)
{
    EFI_STATUS Status = EFI_SUCCESS;
    
    /* `ParseColor` takes care of setting error messages. */
    ERRCHECK(ParseColor(Data, &(Configuration.Colors.Title)));
    DPRINTLN("TITLE COLOR SET.");

    return EFI_SUCCESS;
}


DECL_HANDLER(color_timer)
{
    EFI_STATUS Status = EFI_SUCCESS;
    
    /* `ParseColor` takes care of setting error messages. */
    ERRCHECK(ParseColor(Data, &(Configuration.Colors.Timer)));
    DPRINTLN("TIMER COLOR SET.");

    return EFI_SUCCESS;
}


DECL_HANDLER(color_popup_prompt)
{
    EFI_STATUS Status = EFI_SUCCESS;
    
    /* `ParseColor` takes care of setting error messages. */
    ERRCHECK(ParseColor(Data, &(Configuration.Colors.PromptPopup)));
    DPRINTLN("PROMPT POPUP COLOR SET.");

    return EFI_SUCCESS;
}


DECL_HANDLER(color_popup_info)
{
    EFI_STATUS Status = EFI_SUCCESS;
    
    /* `ParseColor` takes care of setting error messages. */
    ERRCHECK(ParseColor(Data, &(Configuration.Colors.InfoPopup)));
    DPRINTLN("INFO POPUP COLOR SET.");

    return EFI_SUCCESS;
}


DECL_HANDLER(color_popup_warning)
{
    EFI_STATUS Status = EFI_SUCCESS;
    
    /* `ParseColor` takes care of setting error messages. */
    ERRCHECK(ParseColor(Data, &(Configuration.Colors.WarningPopup)));
    DPRINTLN("WARNING POPUP COLOR SET.");

    return EFI_SUCCESS;
}


DECL_HANDLER(scale)
{
    Configuration.Scale = AsciiAtoi((CONST CHAR8 *)Data);
    if (-1U == Configuration.Scale) {
        ErrorMsg = L"Definition of `timeout` is not a number";
        return EFI_INVALID_PARAMETER;
    }

    /* Do not allow scaling over 3x, but it also must be at least 1x. */
    Configuration.Scale = MAX(1, MIN(Configuration.Scale, 3));

    DPRINTLN("ZOOM/SCALE SET TO %ux.", Configuration.Scale);

    return EFI_SUCCESS;
}


DECL_HANDLER(name)
{
    if (!IsWithinChain) {
        ErrorMsg = HackeneyedChainOnlyStr;
        return EFI_INVALID_PARAMETER;
    }

    UINTN Len = AsciiStrLen((CONST CHAR8 *)Data);

    CHAR8 *name = (CHAR8 *)AllocateZeroPool(Len + 1);
    if (NULL == name) {
        ErrorMsg = L"Failed to copy `name` text: out of resources";
        return EFI_OUT_OF_RESOURCES;
    }

    CopyMem(name, Data, Len);
    Configuration.Chains[Configuration.ChainsLength]->Name = name;
    DPRINTLN("NAME ALLOCATED AND SET.");

    return EFI_SUCCESS;
}


DECL_HANDLER(payload)
{
    if (!IsWithinChain) {
        ErrorMsg = HackeneyedChainOnlyStr;
        return EFI_INVALID_PARAMETER;
    }

    UINTN Len = AsciiStrLen((CONST CHAR8 *)Data);

    CHAR8 *payload = (CHAR8 *)AllocateZeroPool(Len + 1);
    if (NULL == payload) {
        ErrorMsg = L"Failed to copy `payload` text: out of resources";
        return EFI_OUT_OF_RESOURCES;
    }

    CopyMem(payload, Data, Len);
    Configuration.Chains[Configuration.ChainsLength]->PayloadPath = payload;
    DPRINTLN("PAYLOAD ALLOCATED AND SET.");

    return EFI_SUCCESS;
}


DECL_HANDLER(target)
{
    if (!IsWithinChain) {
        ErrorMsg = HackeneyedChainOnlyStr;
        return EFI_INVALID_PARAMETER;
    }

    UINTN Len = AsciiStrLen((CONST CHAR8 *)Data);

    CHAR8 *target = (CHAR8 *)AllocateZeroPool(Len + 1);
    if (NULL == target) {
        ErrorMsg = L"Failed to copy `target` text: out of resources";
        return EFI_OUT_OF_RESOURCES;
    }

    CopyMem(target, Data, Len);
    Configuration.Chains[Configuration.ChainsLength]->TargetPath = target;
    DPRINTLN("TARGET ALLOCATED AND SET.");

    return EFI_SUCCESS;
}


DECL_HANDLER(now)
{
    if (!IsWithinChain) {
        ErrorMsg = HackeneyedChainOnlyStr;
        return EFI_INVALID_PARAMETER;
    }

    Configuration.Chains[Configuration.ChainsLength]->IsImmediate
        = !!AsciiAtoi((CONST CHAR8 *)Data);
    DPRINTLN("NOW SET FOR CHAIN");

    return EFI_SUCCESS;
}


DECL_HANDLER(type)
{
    if (!IsWithinChain) {
        ErrorMsg = HackeneyedChainOnlyStr;
        return EFI_INVALID_PARAMETER;
    }

    if (AsciiStrLen(Data) >= 4 && 0 == CompareMem(Data, "disk", 4)) {
        Configuration.Chains[Configuration.ChainsLength]->Type = DISK;
    }
    else if (AsciiStrLen(Data) >= 3 && 0 == CompareMem(Data, "exe", 3)) {
        Configuration.Chains[Configuration.ChainsLength]->Type = EXE;
    }
    else if (AsciiStrLen(Data) >= 3 && 0 == CompareMem(Data, "elf", 3)) {
        Configuration.Chains[Configuration.ChainsLength]->Type = ELF;
    }
    else if (AsciiStrLen(Data) >= 3 && 0 == CompareMem(Data, "bin", 3)) {
        Configuration.Chains[Configuration.ChainsLength]->Type = BIN;
    }
    else {
        ErrorMsg = L"Invalid Chain type";
        return EFI_INVALID_PARAMETER;
    }

    DPRINTLN("TYPE SET FOR CHAIN");

    return EFI_SUCCESS;
}


DECL_HANDLER(mftah)
{
    if (!IsWithinChain) {
        ErrorMsg = HackeneyedChainOnlyStr;
        return EFI_INVALID_PARAMETER;
    }

    Configuration.Chains[Configuration.ChainsLength]->IsMFTAH
        = !!AsciiAtoi((CONST CHAR8 *)Data);
    DPRINTLN("MFTAH SET FOR CHAIN");

    return EFI_SUCCESS;
}


DECL_HANDLER(default)
{
    if (!IsWithinChain) {
        ErrorMsg = HackeneyedChainOnlyStr;
        return EFI_INVALID_PARAMETER;
    }

    Configuration.Chains[Configuration.ChainsLength]->IsDefault
        = !!AsciiAtoi((CONST CHAR8 *)Data);
    DPRINTLN("DEFAULT SET FOR CHAIN");

    return EFI_SUCCESS;
}