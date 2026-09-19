# MinOS Development Roadmap

This roadmap defines the incremental development trajectory for MinOS. Each milestone builds on verified previous functionality and is tested inside a virtual machine.

---

## Milestone 1: Minimal Bootable Prototype *(Current)*
* **Objective:** Smallest real bootable x86_64 kernel running under UEFI in QEMU.
* **Deliverables:**
  * [x] Architecture design document & bootloader comparative evaluation.
  * [x] Zero-admin portable toolchain configuration (`tools/setup_toolchain.py`).
  * [ ] UEFI bootloader entry (`boot/bootx64.c`) with GOP acquisition.
  * [ ] COM1 serial port output driver (`kernel/serial.c`).
  * [ ] CPU vendor detection via `CPUID`.
  * [ ] UEFI memory map discovery and reporting.
  * [ ] Clean exit from UEFI Boot Services (`ExitBootServices`).
  * [ ] Kernel entry transition (`kernel_main`).
  * [ ] Framebuffer graphical boot card rendering (`kernel/framebuffer.c`).
  * [ ] Python build and run harness (`build.py`, `run.py`, `tools/mkdisk.py`).
  * [ ] Verification in QEMU with serial and graphical confirmation.

---

## Milestone 2: Kernel Core & Memory Management
* **Objective:** Establish foundational CPU structures and physical/virtual memory management.
* **Deliverables:**
  * Global Descriptor Table (GDT) and Interrupt Descriptor Table (IDT) for 64-bit long mode.
  * Exception handlers for `#PF`, `#GP`, `#DF` with full register dumps.
  * Physical Memory Manager (PMM) with bitmap tracking of 4 KB pages.
  * Virtual Memory Manager (VMM) with 4-level paging and higher-half kernel mapping (`0xFFFF800000000000`).
  * Kernel heap allocator (`kmalloc` / `kfree` slab allocator).

---

## Milestone 3: Interrupts, Timing & Multitasking
* **Objective:** Preemptive multitasking and hardware timing.
* **Deliverables:**
  * Local APIC timer initialization and calibration.
  * Process Control Block (PCB) and Thread Control Block (TCB) structures.
  * Context switching in assembly (`context_switch.s`).
  * Preemptive Round-Robin / MLFQ scheduler.
  * Kernel threads and synchronization primitives (spinlocks, mutexes).

---

## Milestone 4: Drivers, VFS & Initial Ramdisk
* **Objective:** Storage abstraction and file system access.
* **Deliverables:**
  * PCI bus enumeration and configuration space access.
  * Virtual File System (VFS) abstraction (`mount`, `open`, `read`, `write`).
  * Tar/CPIO Initial Ramdisk (InitRD) loaded at boot.
  * PS/2 keyboard and mouse drivers with an event queue.

---

## Milestone 5: Userspace & System Calls
* **Objective:** Separation of Ring 0 and Ring 3 with system call dispatch.
* **Deliverables:**
  * x86_64 `syscall` / `sysret` mechanism via `MSR_LSTAR`.
  * User address space isolation and page protection.
  * Freestanding user C runtime (`libminos`).
  * Initial userspace programs (hello world, interactive shell) running in Ring 3.

---

## Milestone 6: High-Performance Graphics & Compositor
* **Objective:** Double-buffered window compositor and visual foundation.
* **Deliverables:**
  * Fast software blitter with dirty rectangle tracking.
  * Anti-aliased bitmap / vector font engine.
  * Window management protocol with shared memory buffers.
  * MinOS design system: translucent glassmorphism, rounded window frames, drop shadows.

---

## Milestone 7: MinOS Desktop Shell
* **Objective:** Complete native desktop environment.
* **Deliverables:**
  * Top status bar with clock, battery/system stats, and control menus.
  * Fluid application dock / launcher.
  * Spotlight / Flow command palette.
  * Window manager with drag, resize, snap, minimize, and maximize.

---

## Milestone 8: Core Applications & Storage
* **Objective:** Built-in productivity suite and non-volatile storage.
* **Deliverables:**
  * Native terminal emulator with ANSI color support.
  * File manager (`Finder`/`Explorer` style).
  * System monitor and Settings panel.
  * AHCI / NVMe block device driver with on-disk MinFS support.
