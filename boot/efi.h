#ifndef MINOS_BOOT_EFI_H
#define MINOS_BOOT_EFI_H

#include <stdint.h>
#include <stddef.h>

#define EFIAPI __attribute__((ms_abi))

typedef uint64_t UINTN;
typedef int64_t  INTN;
typedef uint8_t  UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef uint16_t CHAR16;
typedef uint8_t  BOOLEAN;
typedef void*    EFI_HANDLE;
typedef UINTN    EFI_STATUS;
typedef UINT64   EFI_PHYSICAL_ADDRESS;
typedef UINT64   EFI_VIRTUAL_ADDRESS;

#define TRUE  1
#define FALSE 0

#define EFI_SUCCESS            0
#define EFI_ERROR_MASK         ((UINTN)1 << 63)
#define EFI_ERROR(status)      (((UINTN)(status)) & EFI_ERROR_MASK)

#define EFI_BUFFER_TOO_SMALL   (EFI_ERROR_MASK | 5)
#define EFI_OUT_OF_RESOURCES   (EFI_ERROR_MASK | 12)
#define EFI_INVALID_PARAMETER  (EFI_ERROR_MASK | 2)
#define EFI_NOT_FOUND          (EFI_ERROR_MASK | 14)

typedef struct {
    UINT32 Data1;
    UINT16 Data2;
    UINT16 Data3;
    UINT8  Data4[8];
} EFI_GUID;

#define EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID \
    { 0x9042a9de, 0x23dc, 0x4a38, { 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a } }

typedef struct {
    UINT64 Signature;
    UINT32 Revision;
    UINT32 HeaderSize;
    UINT32 CRC32;
    UINT32 Reserved;
} EFI_TABLE_HEADER;

/* Simple Text Output */
struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_TEXT_RESET)(
    struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
    BOOLEAN ExtendedVerification
);

typedef EFI_STATUS (EFIAPI *EFI_TEXT_STRING)(
    struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
    const CHAR16 *String
);

typedef struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    EFI_TEXT_RESET  Reset;
    EFI_TEXT_STRING OutputString;
    void           *TestString;
    void           *QueryMode;
    void           *SetMode;
    void           *SetAttribute;
    void           *ClearScreen;
    void           *SetCursorPosition;
    void           *EnableCursor;
    void           *Mode;
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

/* Memory Types */
typedef enum {
    EfiReservedMemoryType,
    EfiLoaderCode,
    EfiLoaderData,
    EfiBootServicesCode,
    EfiBootServicesData,
    EfiRuntimeServicesCode,
    EfiRuntimeServicesData,
    EfiConventionalMemory,
    EfiUnusableMemory,
    EfiACPIReclaimMemory,
    EfiACPIMemoryNVS,
    EfiMemoryMappedIO,
    EfiMemoryMappedIOPortSpace,
    EfiPalCode,
    EfiPersistentMemory,
    EfiMaxMemoryType
} EFI_MEMORY_TYPE;

typedef struct {
    UINT32               Type;
    EFI_PHYSICAL_ADDRESS PhysicalStart;
    EFI_VIRTUAL_ADDRESS  VirtualStart;
    UINT64               NumberOfPages;
    UINT64               Attribute;
} EFI_MEMORY_DESCRIPTOR;

/* Graphics Output Protocol */
typedef enum {
    PixelRedGreenBlueReserved8BitPerColor,
    PixelBlueGreenRedReserved8BitPerColor,
    PixelBitMask,
    PixelBltOnly,
    PixelFormatMax
} EFI_GRAPHICS_PIXEL_FORMAT;

typedef struct {
    UINT32 RedMask;
    UINT32 GreenMask;
    UINT32 BlueMask;
    UINT32 ReservedMask;
} EFI_PIXEL_BITMASK;

typedef struct {
    UINT32                    Version;
    UINT32                    HorizontalResolution;
    UINT32                    VerticalResolution;
    EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
    EFI_PIXEL_BITMASK         PixelInformation;
    UINT32                    PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
    UINT32                               MaxMode;
    UINT32                               Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
    UINTN                                SizeOfInfo;
    EFI_PHYSICAL_ADDRESS                 FrameBufferBase;
    UINTN                                FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct _EFI_GRAPHICS_OUTPUT_PROTOCOL {
    void                              *QueryMode;
    void                              *SetMode;
    void                              *Blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;

/* Boot Services */
typedef EFI_STATUS (EFIAPI *EFI_GET_MEMORY_MAP)(
    UINTN                 *MemoryMapSize,
    EFI_MEMORY_DESCRIPTOR *MemoryMap,
    UINTN                 *MapKey,
    UINTN                 *DescriptorSize,
    UINT32                *DescriptorVersion
);

typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_POOL)(
    EFI_MEMORY_TYPE PoolType,
    UINTN           Size,
    void          **Buffer
);

typedef EFI_STATUS (EFIAPI *EFI_FREE_POOL)(
    void *Buffer
);

typedef EFI_STATUS (EFIAPI *EFI_LOCATE_PROTOCOL)(
    EFI_GUID *Protocol,
    void     *Registration,
    void    **Interface
);

typedef EFI_STATUS (EFIAPI *EFI_EXIT_BOOT_SERVICES)(
    EFI_HANDLE ImageHandle,
    UINTN      MapKey
);

typedef struct {
    EFI_TABLE_HEADER        Hdr;

    /* Task Priority Services */
    void                   *RaiseTPL;
    void                   *RestoreTPL;

    /* Memory Services */
    void                   *AllocatePages;
    void                   *FreePages;
    EFI_GET_MEMORY_MAP      GetMemoryMap;
    EFI_ALLOCATE_POOL       AllocatePool;
    EFI_FREE_POOL           FreePool;

    /* Event & Timer Services */
    void                   *CreateEvent;
    void                   *SetTimer;
    void                   *WaitForEvent;
    void                   *SignalEvent;
    void                   *CloseEvent;
    void                   *CheckEvent;

    /* Protocol Handler Services */
    void                   *InstallProtocolInterface;
    void                   *ReinstallProtocolInterface;
    void                   *UninstallProtocolInterface;
    void                   *HandleProtocol;
    void                   *Void;
    void                   *RegisterProtocolNotify;
    void                   *LocateHandle;
    void                   *LocateDevicePath;
    void                   *InstallConfigurationTable;

    /* Image Services */
    void                   *LoadImage;
    void                   *StartImage;
    void                   *Exit;
    void                   *UnloadImage;
    EFI_EXIT_BOOT_SERVICES  ExitBootServices;

    /* Miscellaneous Services */
    void                   *GetNextMonotonicCount;
    void                   *Stall;
    void                   *SetWatchdogTimer;

    /* DriverSupport Services */
    void                   *ConnectController;
    void                   *DisconnectController;

    /* Open and Close Protocol Services */
    void                   *OpenProtocol;
    void                   *CloseProtocol;
    void                   *OpenProtocolInformation;

    /* Library Services */
    void                   *ProtocolsPerHandle;
    void                   *LocateHandleBuffer;
    EFI_LOCATE_PROTOCOL     LocateProtocol;
} EFI_BOOT_SERVICES;

/* System Table */
typedef struct {
    EFI_TABLE_HEADER                 Hdr;
    CHAR16                          *FirmwareVendor;
    UINT32                           FirmwareRevision;
    EFI_HANDLE                       ConsoleInHandle;
    void                            *ConIn;
    EFI_HANDLE                       ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
    EFI_HANDLE                       StandardErrorHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *StdErr;
    void                            *RuntimeServices;
    EFI_BOOT_SERVICES               *BootServices;
    UINTN                            NumberOfTableEntries;
    void                            *ConfigurationTable;
} EFI_SYSTEM_TABLE;

#endif /* MINOS_BOOT_EFI_H */
