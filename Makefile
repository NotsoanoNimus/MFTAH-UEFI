SUPPARCHS		= x86_64 # aarch64 riscv64 loongarch64 ia64 mips64el

# Must be set to one of the members from SUPPARCHS.
ARCH			:= x86_64

ifeq ($(filter $(ARCH),$(SUPPARCHS)),)
$(error '$(ARCH)' is not a supported architecture)
endif


CXX				= clang

GNUEFI_DIR		= gnu-efi
MFTAH_DIR		= MFTAH
SRC_DIR			= src

EFIINC_DIR		= $(GNUEFI_DIR)/inc
EFIINCS			= -I./ -I$(SRC_DIR)/include -I$(MFTAH_DIR)/src/include -I$(EFIINC_DIR) \
					-I$(EFIINC_DIR)/$(ARCH) -I$(EFIINC_DIR)/protocol

EFILIB			= $(GNUEFI_DIR)/$(ARCH)
EFILIBS			= -L$(EFILIB)/lib -L$(EFILIB)/gnuefi

LIBGNUEFI		= $(EFILIB)/gnuefi/libgnuefi.a
LIBEFI			= $(EFILIB)/lib/libefi.a

MFTAHLIBS		= -L$(MFTAH_DIR)/build
LIBMFTAH		= $(MFTAH_DIR)/build/libmftah.a

OPTIM			= -O3
CFLAGS			= -target $(ARCH)-unknown-windows -ffreestanding -DMFTAH_ARCH=$(ARCH) \
					-mno-stack-arg-probe -Wall $(EFIINCS) $(OPTIM) \
					-DMFTAH_RELEASE_DATE=$(shell printf "0x`date +%Y``date +%m`%d" `date +%02d`)
LDFLAGS			= -target $(ARCH)-unknown-windows -nostdlib -Wl,-entry:efi_main \
					-Wl,-subsystem:efi_application -fuse-ld=lld-link -mno-stack-arg-probe $(EFILIBS) $(MFTAHLIBS)

SRCS_GNU_EFI	= $(shell find $(GNUEFI_DIR)/lib -maxdepth 1 -type f -name "*.c" | grep -Pvi '(entry|lock)\.')
SRCS_ARCH		= $(shell find $(GNUEFI_DIR)/lib/$(ARCH) -maxdepth 1 -type f -name "*.c")
SRCS_RT			= $(shell find $(GNUEFI_DIR)/lib/runtime -maxdepth 1 -type f -name "*.c")
SRCS_MFTAHUEFI	= $(shell find $(SRC_DIR) -type f -name "*.c")

OBJS_GNUEFI		= $(patsubst %.c,%.o,$(SRCS_GNU_EFI) $(SRCS_ARCH) $(SRCS_RT))
OBJS_MFTAHUEFI	= $(patsubst %.c,%.o,$(SRCS_MFTAHUEFI))

OBJS			= $(OBJS_GNUEFI) $(OBJS_MFTAHUEFI)
PSF_SRC			= $(SRC_DIR)/core/font.c

TARGET			= MFTAH.EFI


.PHONY: default
.PHONY: clean
.PHONY: clean-objs
.PHONY: font
.PHONY: debug
.PHONY: all
.PHONY: ramdisk_ssdt

default: all

clean: clean-objs
	-rm $(TARGET)* &>/dev/null

clean-objs:
	-rm $(OBJS) &>/dev/null
	-rm $(PSF_SRC) &>/dev/null
	-rm $(SRC_DIR)/Ramdisk.aml
	-rm $(SRC_DIR)/Ramdisk.aml.c

# We can assume a 'clean' should be run on all .o files
#   after the build completes. This is because compilation
#   of the EFI file is rather expedient anyway, and it
#   helps not to mix up release and debug build artifacts.
debug: CFLAGS += -DEFI_DEBUG=1
debug: TARGET = $(TARGET).DEBUG
debug: $(TARGET) clean-objs

all: $(SRC_DIR)/Ramdisk.aml.c $(TARGET)

%.o: %.c
	$(CXX) $(CFLAGS) -c -o $@ $<


font:
	xxd -i font.psf >$(PSF_SRC)
	$(CXX) $(CFLAGS) -c -o $(SRC_DIR)/core/font.o $(PSF_SRC)


# The resulting C file will contain a byte array definition (u-char)
#   for 'Ramdisk_aml' and a 'Ramdisk_aml_len'. These can then be
#	referenced in other files as 'extern'.
# NOTE: `xxd -i [FILE]` generated dynamic names, hence the complexity here.
ramdisk_ssdt: $(SRC_DIR)/Ramdisk.aml.c
	$(CXX) $(CFLAGS) -c -o $(SRC_DIR)/Ramdisk.aml.o $(SRC_DIR)/Ramdisk.aml.c

$(SRC_DIR)/Ramdisk.aml.c: $(SRC_DIR)/Ramdisk.asl
	iasl $(SRC_DIR)/Ramdisk.asl
	echo "unsigned char NvdimmRootAml[] = {" >$(SRC_DIR)/Ramdisk.aml.c
	cat $(SRC_DIR)/Ramdisk.aml | xxd -i >>$(SRC_DIR)/Ramdisk.aml.c
	echo -ne "};\nunsigned int NvdimmRootAmlLength = " >>$(SRC_DIR)/Ramdisk.aml.c
	wc -c $(SRC_DIR)/Ramdisk.aml | cut -d' ' -f1 | tr -d '\n' >>$(SRC_DIR)/Ramdisk.aml.c
	echo ";" >>$(SRC_DIR)/Ramdisk.aml.c


$(TARGET): font $(LIBGNUEFI) $(LIBEFI) $(LIBMFTAH) $(OBJS)
	$(MAKE) -C . font
	$(MAKE) -C . ramdisk_ssdt
	$(CXX) $(LDFLAGS) -o $(TARGET) $(OBJS) $(SRC_DIR)/core/font.o $(LIBMFTAH)


# README dependency ensures the submodule is cloned 'properly'.
$(LIBMFTAH): $(MFTAH_DIR)/README.md
	$(MAKE) -C $(MFTAH_DIR) efi
	$(MAKE) $(TARGET)


$(LIBEFI): $(LIBGNUEFI)
$(LIBGNUEFI): $(GNUEFI_DIR) $(GNUEFI_DIR)/README.md
	$(MAKE) -C $(GNUEFI_DIR)
	$(MAKE) $(TARGET)
