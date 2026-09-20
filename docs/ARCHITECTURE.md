# MinOS Architecture Specification

**Version:** 0.1.0  
**Status:** Approved for Milestone 1  
**Target Architecture:** x86_64 (AMD64 / Intel 64)

---

## 1. Boot Architecture & Bootloader Evaluation

Before implementing Milestone 1, three primary bootloading architectures were systematically evaluated:

### 1.1 Comparative Evaluation

| Architecture | Boot Mode | Kernel Format | Portability on Non-Admin Windows | UEFI Transition Control | Verdict |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Multiboot2 / GRUB** | Legacy BIOS / Multiboot2 | ELF32 / ELF64 | Infeasible without WSL, root loopback mounts, and `xorriso` / `mtools`. | Firmware services exited by GRUB; limited runtime control. | **Rejected** |
| **Limine** | UEFI / BIOS | ELF64 | Requires cross-compiling ELF64 binaries on Windows (`clang` target setup or Linux toolchain). | Exits boot services internally before kernel handoff; hides UEFI transition. | **Deferred for future microkernel iterations** |
| **Custom Direct UEFI (MinOS Boot)** | Pure 64-bit UEFI | PE32+ (`.efi`) | Native match for Windows host toolchains (`w64devkit` / MinGW GCC / Clang natively emit PE32+ with MS x64 ABI). | Direct, transparent control of GOP negotiation, memory map parsing, and `ExitBootServices`. | **Selected for Milestone 1** |

### 1.2 Architectural Decision: Custom Direct UEFI Loader

* **Native Binary Compatibility:** UEFI applications are PE32+ binaries using the Microsoft x64 calling convention (`__attribute__((ms_abi))` / MS ABI). Standard portable Windows compilers compile freestanding UEFI executables directly without cross-compilation toolchains.
* **Firmware Handshake:** MinOS interfaces directly with the UEFI System Table:
  1. Locates `EFI_GRAPHICS_OUTPUT_PROTOCOL` (GOP) to acquire the linear 32-bit framebuffer.
  2. Queries the UEFI memory map (`GetMemoryMap`) to discover physical RAM topology.
  3. Detects CPU capabilities via `CPUID`.
  4. Calls `ExitBootServices(ImageHandle, MapKey)` to sever firmware ties.
  5. Passes control directly to `kernel_main(&boot_info)`.
* **Standardized Handoff (`MinOS_BootInfo`):** The bootloader populates a clean C struct passed as the first parameter (`RCX` under MS x64 ABI, or `RDI` under System V ABI).

---

## 2. Kernel Architecture

MinOS utilizes a **modular hybrid kernel**:
* **Kernel Core:** A compact, high-performance monolithic core executing in Ring 0, providing:
  * Physical & virtual memory management
  * Preemptive thread scheduling
  * Interrupt and exception dispatch
  * Virtual File System (VFS)
* **Subsystem Modules:** Storage, networking, display, and bus drivers are organized as modular components with standardized function-pointer dispatch tables, enabling hot-reloading and clear isolation without message-passing IPC overhead.

---

## 3. Memory Management

### 3.1 Physical Memory Manager (PMM)
* **Allocation Unit:** 4096-byte (4 KB) page frames.
* **Algorithm:** Bitmap allocator initialized directly from the UEFI memory map descriptors (`EfiConventionalMemory`).
* **Protection:** Preserves ACPI reclaimable, ACPI NVS, and kernel image memory regions.

### 3.2 Virtual Memory Manager (VMM)
* **Model:** 4-Level Paging (PML4 -> PDPT -> Page Directory -> Page Table).
* **Higher-Half Mapping:** The kernel resides in the upper canonical half (e.g. `0xFFFF_8000_0000_0000`).
* **Direct Physical Map (HHDM):** Linear identity mapping of all physical memory into the higher half to facilitate page table manipulation.
* **Page Protection:**
  * User/Supervisor bit (`U/S`) strictly separates Ring 3 from Ring 0.
  * No-Execute (`NX`) bit enforced on all data and stack pages.
  * Write-Protect (`WP`) enabled in CR0 to prevent kernel writes to read-only user pages.

### 3.3 Kernel Heap Allocator
* Sub-page allocations managed by a bucketed slab/SLUB allocator providing `kmalloc(size)` and `kfree(ptr)`.

---

## 4. CPU & Execution Environment

* **Target Mode:** 64-bit Long Mode (`EFER.LME = 1`, `EFER.LMA = 1`, `CR0.PG = 1`, `CR4.PAE = 1`).
* **Segmentation:** Flat model (64-bit GDT: Null, Kernel Code 64, Kernel Data 64, User Code 64, User Data 64, TSS).
* **Stack:** Dedicated 32 KB kernel bootstrap stack, aligned to 16 bytes per ABI requirements.
* **Interrupt Stack Table (IST):** Double Fault (`#DF`) and Page Fault (`#PF`) handlers allocated independent IST stacks to recover from stack exhaustion.

---

## 5. Interrupts, Exceptions & Scheduling

* **IDT:** 256 interrupt gates in 64-bit format.
* **Exceptions:** Trap and fault handlers capturing complete CPU register snapshots (`RAX`, `RBX`, `RCX`, `RDX`, `RSI`, `RDI`, `RBP`, `R8`-`R15`, `RIP`, `CS`, `RFLAGS`, `RSP`, `SS`, `CR2`, `CR3`).
* **APIC:** Legacy 8259 PIC disabled via port masking (`0xFF` to `0x21`/`0xA1`); Local APIC and I/O APIC configured for timer and hardware IRQs.
* **Scheduler:** Preemptive Priority-based Multi-Level Feedback Queue (MLFQ) driven by the APIC timer (100 Hz–1000 Hz).

---

## 6. Storage & Filesystems

The current Milestone 4 storage slice uses a deliberately conservative ATA
primary-master PIO path. It polls every command with a timeout, rejects
non-ATA signatures, and never enables device IRQs. The MinOS VFS keeps a
bounded directory/file table and an 8-sector metadata record area, with file
payloads in fixed data slots; metadata changes are flushed after each
mutation. This is an intentionally recoverable bootstrap format, not the
future MinFS journal.

* **Virtual File System (VFS):** Unified hierarchy root (`/`) supporting `mount`, `open`, `read`, `write`, `close`, `readdir`, and `stat`.
* **InitRD (RAMFS):** Tar/CPIO format ramdisk containing system fonts, default desktop shell assets, configuration files, and core userspace binaries loaded at boot.
* **Storage Drivers:** AHCI (SATA) and NVMe controller drivers built against an asynchronous request queue.
* **Native Filesystem (MinFS):** Future extent-based transactional filesystem with copy-on-write (COW) snapshotting and journaled metadata.

---

## 7. Graphics & Display Subsystem

* **Linear Framebuffer:** Acquired via UEFI GOP (`EFI_GRAPHICS_OUTPUT_PROTOCOL`). Direct 32-bit linear address mapping (RGBA/BGRA).
* **Double Buffering:** Screen updates rendered to an off-screen backbuffer and copied to the physical framebuffer on refresh to eliminate tearing.
* **Typography:** Built-in bitmap font engine for boot diagnostics and terminal, scaling to vector font rendering in userspace.
* **Compositor:** Shared-memory window buffers composited onto the display surface with alpha-blended glassmorphic effects.

---

## 8. Security Model

* **Token-Based Capabilities:** Processes are assigned capability tokens defining access to hardware devices, network sockets, and file subtrees.
* **Sandboxing:** Third-party applications execute in isolated namespaces with restricted filesystem views.
* **Hardware Mitigations:** SMEP (Supervisor Mode Execution Prevention), SMAP (Supervisor Mode Access Prevention), and W^X memory page enforcement.

---

## 9. Virtual-Machine Testing & CI

* Primary automated target: **QEMU x86_64** with OVMF UEFI firmware.
* Diagnostic feedback loop:
  * **COM1 UART (`0x3F8`):** Real-time serial logs for automated test assertion.
  * **Framebuffer Display:** Graphical inspection of visual styling and rendering.
# Storage and filesystem

ATA exposes a checked 512-byte block-device interface (`block_size`,
`block_count`, bounded read/write, and flush) to higher layers.  VFS metadata
starts at LBA 2048 and is twelve sectors: magic, version 2, generation, a
128-entry allocation bitmap, and fixed-size directory records.  A mount accepts
metadata only when the version, record count, root, parent links, and
allocation bitmap validate; otherwise it starts an empty filesystem rather
than interpreting stale bytes.  File extents grow in 512-byte sectors and are
reclaimed by deletion; deletion clears the persistent allocation record.
The allocator searches for non-overlapping free extents, so files are not
limited to a fixed 4 KiB payload.
deletion; allocation searches the disk for non-overlapping free extents.
`df` reports the actual data-region capacity and usage.  The metadata record
table remains bounded at 128 entries for this milestone.
