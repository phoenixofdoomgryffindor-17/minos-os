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

Completed: kernel stack handoff, flat GDT, exception IDT, conventional-memory
frame allocator, and identity/higher-half/HHDM bootstrap page tables. Interrupts
remain disabled until an APIC/IRQ policy is introduced.
* **Objective:** Establish foundational CPU structures and physical/virtual memory management.
* **Deliverables:**
  * Global Descriptor Table (GDT) and Interrupt Descriptor Table (IDT) for 64-bit long mode.
  * Exception handlers for `#PF`, `#GP`, `#DF` with full register dumps.
  * Physical Memory Manager (PMM) with bitmap tracking of 4 KB pages.
  * Virtual Memory Manager (VMM) with 4-level paging and higher-half kernel mapping (`0xFFFF800000000000`).
  * Kernel heap allocator (`kmalloc` / `kfree` slab allocator).

---

## Milestone 3: Interrupts, Timing & Interactive Console
* **Objective:** Hardware timing and a responsive framebuffer kernel console.
* **Current increment:** The BSP now performs CPUID-gated xAPIC discovery, masks
  the legacy PIC, calibrates an xAPIC periodic timer against PIT channel 2, and
  exposes a monotonic `timer_ticks()` API. It also initializes the PS/2
  controller, routes legacy PIC IRQ1 to vector 33, and publishes set-1 keyboard
  events through a reusable input queue. Interrupts remain disabled until GDT,
  IDT, memory, APIC, timer, and keyboard state are ready; the QEMU serial test
  then verifies that vector 32 is delivered by a real periodic interrupt.
* **Final increment:** The framebuffer console consumes the generic input-event
  queue and provides a `MinOS>` prompt, cursor editing, scrolling, and the safe
  commands `help`, `clear`, `about`, `mem`, and `ticks`. Headless QEMU verifies
  console readiness and a real timer interrupt. The harness deliberately does
  not inject fake PS/2 keyboard input, so commands are verified interactively.
* **Deliverables:**
  * Local APIC timer initialization and calibration.
  * PS/2 keyboard input and reusable input-event queue.
  * Process Control Block (PCB) and Thread Control Block (TCB) structures.
  * Context switching in assembly (`context_switch.s`).
  * Preemptive Round-Robin / MLFQ scheduler.
  * Kernel threads and synchronization primitives (spinlocks, mutexes).

---

## Milestone 4: Drivers, VFS & Initial Ramdisk *(implemented increment)*
* **Objective:** Storage abstraction and file system access.
* **Deliverables:**
  * [x] Safe ATA primary-master 28-bit PIO identify/read/write driver.
  * [x] Persistent bounded MinOS VFS metadata, directories, files, and path normalization.
  * [x] Console filesystem commands and command history/editing shortcuts.
  * [ ] PCI bus enumeration and configuration space access.
  * [ ] Tar/CPIO Initial Ramdisk (InitRD) loaded at boot.
  * PS/2 mouse driver with the input event queue.

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
# Milestone 4 acceptance notes

The current shell supports quoted and escaped arguments, case-preserving file
contents, `uname`, actual VFS allocation statistics via `df`, Ctrl+C/Ctrl+L,
and PS/2 extended Home/End/Delete keys.  The filesystem remains deliberately
bounded to 64 directory entries and 4 KiB per entry; this is reported by the
filesystem rather than presented as an unbounded disk.
