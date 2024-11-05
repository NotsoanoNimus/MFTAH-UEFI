/**
 * Operations to integrate a working RamDisk driver into the application.
 * 
 * A lot of this code is adapted from the EDK2 implementation of RamDiskDxe:
 * https://github.com/tianocore/edk2/tree/master/MdeModulePkg/Universal/Disk/RamDiskDxe
 */

#ifndef MFTAH_RAMDISK_H
#define MFTAH_RAMDISK_H

#include "../mftah_uefi.h"


#ifndef RAM_DISK_BLOCK_SIZE
#   define RAM_DISK_BLOCK_SIZE 512
#endif

#define MEDIA_RAM_DISK_DP 0x09

/* Taken from UEFI spec: https://uefi.org/specs/UEFI/2.10/13_Protocols_Media_Access.html#ram-disk-protocol */
#define EFI_RAM_DISK_PROTOCOL_GUID \
    { 0xab38a0df, 0x6873, 0x44a9, \
    { 0x87, 0xe6, 0xd4, 0xeb, 0x56, 0x14, 0x84, 0x49 }}

#define EFI_VIRTUAL_DISK_GUID \
    { 0x77AB535A, 0x45FC, 0x624B, \
    { 0x55, 0x60, 0xF7, 0xB2, 0x81, 0xD1, 0xF9, 0x6E }}

#define EFI_VIRTUAL_CD_GUID \
    { 0x3D5ABD30, 0x4175, 0x87CE, \
    { 0x6D, 0x64, 0xD2, 0xAD, 0xE5, 0x23, 0xC4, 0xBB }}

#define EFI_PERSISTENT_VIRTUAL_DISK_GUID \
    { 0x5CEA02C9, 0x4D07, 0x69D3, \
    { 0x26, 0x9F, 0x44, 0x96, 0xFB, 0xE0, 0x96, 0xF9 }}

#define EFI_PERSISTENT_VIRTUAL_CD_GUID \
    { 0x08018188, 0x42CD, 0xBB48, \
    { 0x10, 0x0F, 0x53, 0x87, 0xD5, 0x3D, 0xED, 0x3D }}


#define RAMDISK_PRIVATE_DATA_SIGNATURE \
    EFI_SIGNATURE_32 ('R', 'D', 'S', 'K')

#define RAM_DISK_PRIVATE_FROM_BLKIO(a) \
    CR(a, RAMDISK_PRIVATE_DATA, BlockIo, RAMDISK_PRIVATE_DATA_SIGNATURE)
#define RAM_DISK_PRIVATE_FROM_BLKIO2(a) \
    CR(a, RAMDISK_PRIVATE_DATA, BlockIo2, RAMDISK_PRIVATE_DATA_SIGNATURE)


typedef
EFI_STATUS
(EFIAPI *EFI_RAM_DISK_REGISTER_RAMDISK) (
    IN UINT64                               RamDiskBase,
    IN UINT64                               RamDiskSize,
    IN EFI_GUID                             *RamDiskType,
    IN EFI_DEVICE_PATH                      *ParentDevicePath OPTIONAL,
    OUT EFI_DEVICE_PATH_PROTOCOL            **DevicePath
);

typedef
EFI_STATUS
(EFIAPI *EFI_RAM_DISK_UNREGISTER_RAMDISK) (
    IN EFI_DEVICE_PATH_PROTOCOL             *DevicePath
);

typedef
struct {
    EFI_RAM_DISK_REGISTER_RAMDISK           Register;
    EFI_RAM_DISK_UNREGISTER_RAMDISK         Unregister;
} EFI_RAM_DISK_PROTOCOL;

typedef
struct {
    EFI_DEVICE_PATH_PROTOCOL    Header;
    UINT32                      StartingAddr[2];
    UINT32                      EndingAddr[2];
    EFI_GUID                    TypeGuid;
    UINT16                      Instance;
} _PACKED MEDIA_RAMDISK_DEVICE_PATH;

typedef
struct {
    UINTN                           Signature;

    EFI_HANDLE                      Handle;

    EFI_BLOCK_IO_PROTOCOL           BlockIo;
    EFI_BLOCK_IO2_PROTOCOL          BlockIo2;
    EFI_BLOCK_IO_MEDIA              Media;
    EFI_DEVICE_PATH_PROTOCOL        *DevicePath;

    UINT64                          StartingAddr;
    UINT64                          Size;
    EFI_GUID                        TypeGuid;
    UINT16                          InstanceNumber;
} RAMDISK_PRIVATE_DATA;


EXTERN EFI_GUID gEfiRamdiskGuid;
EXTERN EFI_GUID gEfiRamdiskVirtualDiskGuid;
EXTERN EFI_GUID gEfiRamdiskVirtualCdGuid;
EXTERN EFI_GUID gEfiRamdiskPersistentVirtualDiskGuid;
EXTERN EFI_GUID gEfiRamdiskPersistentVirtualCdGuid;

EXTERN EFI_RAM_DISK_PROTOCOL RAMDISK;


/**
 * The entry point for the Ramdisk driver.
 *
 * @param[in] ImageHandle     The image handle of the driver.
 *
 * @retval EFI_SUCCESS            All the related protocols were installed.
 * @retval EFI_INVALID_PARAMETER  The protocol was unable to be installed.
 */
EFI_STATUS
EFIAPI
RamdiskDriverInit(
    IN EFI_HANDLE           ImageHandle
);


/**
 * Register a ramdisk with specified address, size, and type.
 *
 * @param[in]  RamDiskBase    The base address of registered RAM disk.
 * @param[in]  RamDiskSize    The size of registered RAM disk.
 * @param[in]  RamDiskType    The type of registered RAM disk. The GUID can be
 *                            any of the values defined in section 9.3.6.9, or a
 *                            vendor defined GUID.
 * @param[in]  ParentDevicePath
 *                            Pointer to the parent device path. If there is no
 *                            parent device path then ParentDevicePath is NULL.
 * @param[out] DevicePath     On return, points to a pointer to the device path
 *                            of the RAM disk device.
 *                            If ParentDevicePath is not NULL, the returned
 *                            DevicePath is created by appending a RAM disk node
 *                            to the parent device path. If ParentDevicePath is
 *                            NULL, the returned DevicePath is a RAM disk device
 *                            path without appending. This function is
 *                            responsible for allocating the buffer DevicePath
 *                            with the boot service AllocatePool().
 *
 * @retval EFI_SUCCESS             The RAM disk is registered successfully.
 * @retval EFI_INVALID_PARAMETER   DevicePath or RamDiskType is NULL.
 *                                 RamDiskSize is 0.
 * @retval EFI_ALREADY_STARTED     A Device Path Protocol instance to be created
 *                                 is already present in the handle database.
 * @retval EFI_OUT_OF_RESOURCES    The RAM disk register operation fails due to
 *                                 resource limitation.
 *
 */
EFI_STATUS
EFIAPI
RamDiskRegister(
    IN UINT64                       RamDiskBase,
    IN UINT64                       RamDiskSize,
    IN EFI_GUID                     *RamDiskType,
    IN EFI_DEVICE_PATH              *ParentDevicePath OPTIONAL,
    OUT EFI_DEVICE_PATH_PROTOCOL    **DevicePath
);


/**
 * This functionality is left incomplete because it's not currently used.
 */
EFI_STATUS
EFIAPI
RamDiskUnregister(
    IN EFI_DEVICE_PATH_PROTOCOL *DevicePath
);


/**
 * Initialize the BlockIO protocol of a RAM disk device.
 *
 * @param[in] PrivateData     Points to RAM disk private data.
 *
 */
VOID
EFIAPI
RamDiskInitBlockIo(
    IN RAMDISK_PRIVATE_DATA     *PrivateData
);


/**
 * Reset the Block Device.
 *
 * @param[in] This            Indicates a pointer to the calling context.
 * @param[in] ExtendedVerification
 *                            Driver may perform diagnostics on reset.
 *
 * @retval EFI_SUCCESS             The device was reset.
 * @retval EFI_DEVICE_ERROR        The device is not functioning properly and
 *                                 could not be reset.
 *
 */
EFI_STATUS
EFIAPI
RamDiskBlkIoReset(
    IN EFI_BLOCK_IO_PROTOCOL    *This,
    IN BOOLEAN                  ExtendedVerification
);


/**
 * Read BufferSize bytes from an LBA into a Buffer.
 *
 * @param[in]  This           Indicates a pointer to the calling context.
 * @param[in]  MediaId        Id of the media, changes every time the media is replaced.
 * @param[in]  Lba            The starting Logical Block Address to read from.
 * @param[in]  BufferSize     Size of Buffer, must be a multiple of device block size.
 * @param[out] Buffer         A pointer to the destination buffer for the data.
 *                            The caller is responsible for either having
 *                            implicit or explicit ownership of the buffer.
 *
 * @retval EFI_SUCCESS             The data was read correctly from the device.
 * @retval EFI_DEVICE_ERROR        The device reported an error while performing the read.
 * @retval EFI_NO_MEDIA            There is no media in the device.
 * @retval EFI_MEDIA_CHANGED       The MediaId does not matched the current device.
 * @retval EFI_BAD_BUFFER_SIZE     The Buffer was not a multiple of the block
 *                                 size of the device.
 * @retval EFI_INVALID_PARAMETER   The read request contains LBAs that are not
 *                                 valid, or the buffer is not on proper alignment.
 *
 */
EFI_STATUS
EFIAPI
RamDiskBlkIoReadBlocks(
    IN EFI_BLOCK_IO_PROTOCOL    *This,
    IN UINT32                   MediaId,
    IN EFI_LBA                  Lba,
    IN UINTN                    BufferSize,
    OUT VOID                    *Buffer
);


/**
 * Write BufferSize bytes from a Buffer into an LBA.
 *
 * @param[in] This            Indicates a pointer to the calling context.
 * @param[in] MediaId         The media ID that the write request is for.
 * @param[in] Lba             The starting logical block address to be written.
 *                            The caller is responsible for writing to only
 *                            legitimate locations.
 * @param[in] BufferSize      Size of Buffer, must be a multiple of device block size.
 * @param[in] Buffer          A pointer to the source buffer for the data.
 *
 * @retval EFI_SUCCESS             The data was written correctly to the device.
 * @retval EFI_WRITE_PROTECTED     The device can not be written to.
 * @retval EFI_DEVICE_ERROR        The device reported an error while performing the write.
 * @retval EFI_NO_MEDIA            There is no media in the device.
 * @retval EFI_MEDIA_CHNAGED       The MediaId does not matched the current device.
 * @retval EFI_BAD_BUFFER_SIZE     The Buffer was not a multiple of the block
 *                                 size of the device.
 * @retval EFI_INVALID_PARAMETER   The write request contains LBAs that are not
 *                                 valid, or the buffer is not on proper alignment.
 *
 */
EFI_STATUS
EFIAPI
RamDiskBlkIoWriteBlocks(
    IN EFI_BLOCK_IO_PROTOCOL    *This,
    IN UINT32                   MediaId,
    IN EFI_LBA                  Lba,
    IN UINTN                    BufferSize,
    IN VOID                     *Buffer
);


/**
 * Flush the Block Device.
 *
 * @param[in] This            Indicates a pointer to the calling context.
 *
 * @retval EFI_SUCCESS             All outstanding data was written to the device.
 * @retval EFI_DEVICE_ERROR        The device reported an error while writing back the data.
 * @retval EFI_NO_MEDIA            There is no media in the device.
 *
 */
EFI_STATUS
EFIAPI
RamDiskBlkIoFlushBlocks(
    IN EFI_BLOCK_IO_PROTOCOL    *This
);


/**
 * Resets the block device hardware.
 *
 * @param[in] This                 The pointer of EFI_BLOCK_IO2_PROTOCOL.
 * @param[in] ExtendedVerification The flag about if extend verificate.
 *
 * @retval EFI_SUCCESS             The device was reset.
 * @retval EFI_DEVICE_ERROR        The block device is not functioning correctly
 *                                 and could not be reset.
 *
 */
EFI_STATUS
EFIAPI
RamDiskBlkIo2Reset(
    IN EFI_BLOCK_IO2_PROTOCOL   *This,
    IN BOOLEAN                  ExtendedVerification
);


/**
 * Reads the requested number of blocks from the device.
 *
 * @param[in]      This            Indicates a pointer to the calling context.
 * @param[in]      MediaId         The media ID that the read request is for.
 * @param[in]      Lba             The starting logical block address to read
 *                                 from on the device.
 * @param[in, out] Token           A pointer to the token associated with the
 *                                 transaction.
 * @param[in]      BufferSize      The size of the Buffer in bytes. This must be
 *                                 a multiple of the intrinsic block size of the
 *                                 device.
 * @param[out]     Buffer          A pointer to the destination buffer for the
 *                                 data. The caller is responsible for either
 *                                 having implicit or explicit ownership of the
 *                                 buffer.
 *
 * @retval EFI_SUCCESS             The read request was queued if Token->Event
 *                                 is not NULL. The data was read correctly from
 *                                 the device if the Token->Event is NULL.
 * @retval EFI_DEVICE_ERROR        The device reported an error while attempting
 *                                 to perform the read operation.
 * @retval EFI_NO_MEDIA            There is no media in the device.
 * @retval EFI_MEDIA_CHANGED       The MediaId is not for the current media.
 * @retval EFI_BAD_BUFFER_SIZE     The BufferSize parameter is not a multiple of
 *                                 the intrinsic block size of the device.
 * @retval EFI_INVALID_PARAMETER   The read request contains LBAs that are not
 *                                 valid, or the buffer is not on proper alignment.
 * @retval EFI_OUT_OF_RESOURCES    The request could not be completed due to a
 *                                 lack of resources.
 *
 */
EFI_STATUS
EFIAPI
RamDiskBlkIo2ReadBlocksEx(
    IN EFI_BLOCK_IO2_PROTOCOL   *This,
    IN UINT32                   MediaId,
    IN EFI_LBA                  Lba,
    IN OUT EFI_BLOCK_IO2_TOKEN  *Token,
    IN UINTN                    BufferSize,
    OUT VOID                    *Buffer
);


/**
 * Writes a specified number of blocks to the device.
 *
 * @param[in]      This            Indicates a pointer to the calling context.
 * @param[in]      MediaId         The media ID that the write request is for.
 * @param[in]      Lba             The starting logical block address to be
 *                                 written. The caller is responsible for
 *                                 writing to only legitimate locations.
 * @param[in, out] Token           A pointer to the token associated with the transaction.
 * @param[in]      BufferSize      The size in bytes of Buffer. This must be a
 *                                 multiple of the intrinsic block size of the device.
 * @param[in]      Buffer          A pointer to the source buffer for the data.
 *
 * @retval EFI_SUCCESS             The write request was queued if Event is not
 *                                 NULL. The data was written correctly to the
 *                                 device if the Event is NULL.
 * @retval EFI_WRITE_PROTECTED     The device cannot be written to.
 * @retval EFI_NO_MEDIA            There is no media in the device.
 * @retval EFI_MEDIA_CHANGED       The MediaId is not for the current media.
 * @retval EFI_DEVICE_ERROR        The device reported an error while attempting
 *                                 to perform the write operation.
 * @retval EFI_BAD_BUFFER_SIZE     The BufferSize parameter is not a multiple of
 *                                 the intrinsic block size of the device.
 * @retval EFI_INVALID_PARAMETER   The write request contains LBAs that are not
 *                                 valid, or the buffer is not on proper
 *                                 alignment.
 * @retval EFI_OUT_OF_RESOURCES    The request could not be completed due to a
 *                                 lack of resources.
 *
 */
EFI_STATUS
EFIAPI
RamDiskBlkIo2WriteBlocksEx(
    IN EFI_BLOCK_IO2_PROTOCOL   *This,
    IN UINT32                   MediaId,
    IN EFI_LBA                  Lba,
    IN OUT EFI_BLOCK_IO2_TOKEN  *Token,
    IN UINTN                    BufferSize,
    IN VOID                     *Buffer
);


/**
 * Flushes all modified data to a physical block device.
 *
 * @param[in]      This            Indicates a pointer to the calling context.
 * @param[in, out] Token           A pointer to the token associated with the transaction.
 *
 * @retval EFI_SUCCESS             The flush request was queued if Event is not
 *                                 NULL. All outstanding data was written
 *                                 correctly to the device if the Event is NULL.
 * @retval EFI_DEVICE_ERROR        The device reported an error while attempting
 *                                 to write data.
 * @retval EFI_WRITE_PROTECTED     The device cannot be written to.
 * @retval EFI_NO_MEDIA            There is no media in the device.
 * @retval EFI_MEDIA_CHANGED       The MediaId is not for the current media.
 * @retval EFI_OUT_OF_RESOURCES    The request could not be completed due to a
 *                                 lack of resources.
 *
 */
EFI_STATUS
EFIAPI
RamDiskBlkIo2FlushBlocksEx(
    IN EFI_BLOCK_IO2_PROTOCOL   *This,
    IN OUT EFI_BLOCK_IO2_TOKEN  *Token
);



#endif   /* MFTAH_RAMDISK_H */
