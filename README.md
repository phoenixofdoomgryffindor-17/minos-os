# MinOS

MinOS is an independent operating system project combining:
* The polished, modern user experience and visual design philosophy associated with macOS
* The broad hardware/software compatibility and practicality associated with Windows
* Strong security by default
* A clean, fast, beginner-friendly experience
* A distinctive MinOS identity rather than being a clone of another operating system

The project builds incrementally toward a real, bootable bare-metal operating system.

---

## Milestone 1: Minimal Bootable Prototype

Milestone 1 implements the smallest possible real bootable x86_64 UEFI kernel that boots in QEMU and visibly demonstrates full hardware control.

### Features
* **Pure UEFI Boot:** Boots natively as an x86_64 UEFI application (`BOOTX64.EFI`).
* **Serial Diagnostics:** Transmits real-time boot status over COM1 (`0x3F8`) UART.
* **GOP Linear Framebuffer:** Direct 32-bit linear framebuffer output with custom styling and text rendering.
* **CPU Hardware Detection:** Detects CPU vendor string via `CPUID`.
* **UEFI Memory Map Retrieval:** Inspects system physical memory descriptors.
* **Clean Transition:** Calls UEFI `ExitBootServices` to terminate firmware services and hand execution over to the MinOS kernel.
* **Zero-Admin Portable Toolchain:** All compilers, binutils, and emulators reside inside `tools/` with no installation or administrator rights required on Windows.

---

## Development & Build Instructions

### Prerequisites
* Windows 10/11 with Python 3.10+ installed.
* No administrator privileges, WSL, or Visual Studio required.

### Setup Toolchain
Download and extract portable tools (w64devkit GCC, QEMU x86_64, OVMF UEFI firmware, 7-Zip):
```cmd
python tools/setup_toolchain.py
```

### Build MinOS
Compile the bootloader and kernel, and assemble the bootable FAT disk image (`build/minos.img`):
```cmd
python build.py
```

### Run MinOS in QEMU
Launch QEMU with the generated disk image, UEFI firmware, and COM1 serial logging:
```cmd
python run.py
```

---

## Repository Structure
```
minos/
├── boot/             # UEFI bootloader entry and headers
│   ├── bootinfo.h    # Handoff structure passed to kernel
│   ├── bootx64.c     # UEFI entry point, GOP setup, ExitBootServices
│   └── efi.h         # Freestanding UEFI 2.x type definitions
├── kernel/           # MinOS core kernel source code
│   ├── framebuffer.c # GOP linear framebuffer graphics routines
│   ├── framebuffer.h # Framebuffer API & font rendering
│   ├── kernel.h      # Core kernel types & prototypes
│   ├── main.c        # Kernel entry point (kernel_main)
│   ├── serial.c      # COM1 UART serial port driver
│   └── serial.h      # Serial I/O interface
├── docs/             # Architecture, design decisions, and roadmap
│   ├── ARCHITECTURE.md
│   └── ROADMAP.md
├── tools/            # Non-admin build tools & disk packaging
│   ├── mkdisk.py     # Pure-Python FAT32/FAT16 UEFI disk generator
│   └── setup_toolchain.py # Automated toolchain installer
├── build.py          # Top-level build script
├── run.py            # QEMU launcher & test runner
└── README.md
```

## Documentation
* [Architecture Design Document](docs/ARCHITECTURE.md)
* [Development Roadmap](docs/ROADMAP.md)
