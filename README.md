# MinOS

MinOS is an independent operating system project combining:
* The polished, modern user experience and visual design philosophy associated with macOS
* The broad hardware/software compatibility and practicality associated with Windows
* Strong security by default
* A clean, fast, beginner-friendly experience
* A distinctive MinOS identity rather than being a clone of another operating system

The project builds incrementally toward a real, bootable bare-metal operating system.

---

## Milestone 4: PIO storage and MinOS filesystem

The kernel now probes the ATA primary-master channel with bounded 28-bit PIO
identify/read/write operations. A small persistent MinOS filesystem stores
directory and file metadata on the disk, normalizes absolute and relative
paths, and exposes safe file operations through the console. When no ATA disk
is present the same API intentionally falls back to volatile memory.

The console includes `pwd`, `ls`, `cd`, `mkdir`, `rmdir`, `touch`, `cat`,
`write`, `append`, `rm`, `cp`, `mv`, `tree`, `find`, `df`, `echo`, and
`history`, `reboot`, and `shutdown`, in addition to the original diagnostics.
`reboot` uses the keyboard-controller/reset-control hardware paths; `shutdown`
uses QEMU/ACPI power-control ports. Ctrl-A/E/U/K and arrow
history provide practical line editing.

## Milestone 3: Interactive Kernel Console

The kernel now provides a framebuffer console driven by the reusable input-event
queue (rather than by keyboard hardware directly). It supports a `MinOS>` prompt,
typed characters, cursor, newline, backspace, tab, scrolling, and the safe
commands `help`, `clear`, `about`, `mem`, and `ticks`. The APIC timer and COM1
serial diagnostics remain enabled, and the main loop sleeps with interrupts
enabled between input events.

`python run.py --test` verifies the UEFI boot, console initialization, and a real
periodic timer interrupt in headless QEMU, and captures `build/boot_screen.ppm`.
QEMU's standard headless harness does not inject PS/2 keystrokes, so command
execution must currently be verified interactively with `python run.py`; the test
does not fake keyboard input.

## Milestone 2: Protected Kernel Foundations

Milestone 2 keeps the direct UEFI design while establishing the foundations needed
for later multitasking and device drivers:
* A 32 KiB, 16-byte-aligned kernel stack and an explicit post-`ExitBootServices`
  transfer (the firmware stack is never reused).
* A flat kernel GDT and an exception-only IDT (vectors 0, 1, 3, 6, 8, 13, 14).
  Hardware interrupts remain masked.
* A conservative 4 KiB physical frame allocator populated only from
  `EfiConventionalMemory`, excluding handoff data, framebuffer, image, and stack.
* Kernel-owned identity and higher-half (`0xFFFF800000000000`) page tables, with
  a direct physical (HHDM) view, loaded into CR3.

The implementation intentionally maps the first 4 GiB with 2 MiB pages for this
bootstrap milestone; larger physical memory is reported but not yet mapped.

## Milestone 1: Minimal Bootable Prototype

Milestone 1 implements the smallest possible real bootable x86_64 UEFI kernel that boots in QEMU and visibly demonstrates full hardware control.

### Features
* **Pure UEFI Boot:** Boots natively as an x86_64 UEFI application (`BOOTX64.EFI`).
* **Serial Diagnostics:** Transmits real-time boot status over COM1 (`0x3F8`) UART.
* **Keyboard Mapping:** PS/2 Set 1 input supports Shift-modified letters, digits,
  and standard US punctuation (`Shift+1` produces `!`), plus explicit modifier
  events for Shift, Ctrl, Alt, and Caps Lock.
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
The build updates the UEFI boot area while preserving the filesystem area of an
existing image. Use `python build.py --clean-image` only when an intentionally
clean filesystem is required.

Run the deterministic filesystem acceptance test with:

```cmd
python tests\qemu_filesystem.py
```

It invokes the kernel's `fstest` diagnostic through QEMU and exercises real
filesystem allocation, reads, deletion, block reuse, and multi-boot
persistence without injecting thousands of shell keystrokes.

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
