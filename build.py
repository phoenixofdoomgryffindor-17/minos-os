"""
MinOS Build System
Compiles the UEFI bootloader and kernel into BOOTX64.EFI and creates build/minos.img
Invoked cleanly with: python build.py
"""

import os
import sys
import subprocess
import shutil

PROJECT_ROOT = os.path.dirname(os.path.abspath(__file__))
TOOLS_DIR = os.path.join(PROJECT_ROOT, "tools")
BUILD_DIR = os.path.join(PROJECT_ROOT, "build")
GCC_EXE = os.path.join(TOOLS_DIR, "w64devkit", "bin", "gcc.exe")
MKDISK_PY = os.path.join(TOOLS_DIR, "mkdisk.py")

SOURCES = [
    os.path.join(PROJECT_ROOT, "boot", "bootx64.c"),
    os.path.join(PROJECT_ROOT, "kernel", "main.c"),
    os.path.join(PROJECT_ROOT, "kernel", "serial.c"),
    os.path.join(PROJECT_ROOT, "kernel", "framebuffer.c"),
]

def check_toolchain():
    if not os.path.exists(GCC_EXE):
        print(f"[MinOS Build] Portable GCC not found at: {GCC_EXE}")
        print("[MinOS Build] Running tools/setup_toolchain.py automatically...")
        setup_script = os.path.join(TOOLS_DIR, "setup_toolchain.py")
        res = subprocess.run([sys.executable, setup_script])
        if res.returncode != 0 or not os.path.exists(GCC_EXE):
            print("[MinOS Build] Toolchain setup failed!")
            sys.exit(1)

def build():
    check_toolchain()
    os.makedirs(BUILD_DIR, exist_ok=True)
    
    efi_output = os.path.join(BUILD_DIR, "BOOTX64.EFI")
    img_output = os.path.join(BUILD_DIR, "minos.img")

    print("[MinOS Build] Compiling MinOS UEFI Kernel...")
    cmd = [
        GCC_EXE,
        "-Wall", "-Wextra",
        "-O2",
        "-ffreestanding",
        "-fno-stack-protector",
        "-fno-stack-check",
        "-mno-red-zone",
        "-nostdlib",
        "-Wl,--subsystem,10",     # IMAGE_SUBSYSTEM_EFI_APPLICATION
        "-Wl,-e,efi_main",        # Entry Point
        "-I", PROJECT_ROOT,
        "-I", os.path.join(PROJECT_ROOT, "boot"),
        "-I", os.path.join(PROJECT_ROOT, "kernel"),
        "-o", efi_output,
    ] + SOURCES

    print("  Command:", " ".join(cmd))
    res = subprocess.run(cmd)
    if res.returncode != 0:
        print("[MinOS Build] Compilation failed!")
        sys.exit(res.returncode)

    print(f"[MinOS Build] Successfully built: {efi_output} ({os.path.getsize(efi_output)} bytes)")

    print("[MinOS Build] Creating bootable UEFI disk image...")
    res = subprocess.run([sys.executable, MKDISK_PY, efi_output, img_output])
    if res.returncode != 0:
        print("[MinOS Build] Disk image generation failed!")
        sys.exit(res.returncode)

    print("[MinOS Build] Build completed successfully!")
    print(f"  UEFI Application: {efi_output}")
    print(f"  Boot Disk Image:  {img_output}")

if __name__ == "__main__":
    build()
