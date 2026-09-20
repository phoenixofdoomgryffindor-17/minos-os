"""
MinOS Toolchain Setup Script
Downloads and installs portable, non-admin tools into tools/
- w64devkit (GCC, Make, Binutils)
- QEMU x86_64 + OVMF UEFI Firmware
- 7-Zip extraction binaries
"""

import os
import sys
import shutil
import urllib.request
import subprocess
import zipfile

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS_DIR = os.path.join(PROJECT_ROOT, "tools")
BIN_DIR = os.path.join(TOOLS_DIR, "bin")
W64DEVKIT_DIR = os.path.join(TOOLS_DIR, "w64devkit")
QEMU_DIR = os.path.join(TOOLS_DIR, "qemu")
DOWNLOADS_DIR = os.path.join(TOOLS_DIR, "downloads")

def log(msg):
    print(f"[MinOS Setup] {msg}", flush=True)

def download_file(url, dest_path):
    if os.path.exists(dest_path) and os.path.getsize(dest_path) > 0:
        log(f"Already downloaded: {os.path.basename(dest_path)}")
        return
    log(f"Downloading {url} ...")
    os.makedirs(os.path.dirname(dest_path), exist_ok=True)
    
    req = urllib.request.Request(
        url,
        headers={"User-Agent": "MinOS-Toolchain-Setup/1.0"}
    )
    with urllib.request.urlopen(req) as resp, open(dest_path, "wb") as out_file:
        total_size = resp.getheader("Content-Length")
        total_size = int(total_size) if total_size else None
        downloaded = 0
        chunk_size = 1024 * 1024  # 1MB chunks
        
        while True:
            chunk = resp.read(chunk_size)
            if not chunk:
                break
            out_file.write(chunk)
            downloaded += len(chunk)
            if total_size:
                pct = (downloaded / total_size) * 100
                print(f"\r  {downloaded // (1024*1024)}MB / {total_size // (1024*1024)}MB ({pct:.1f}%)", end="", flush=True)
        print()
    log(f"Saved to {dest_path} ({os.path.getsize(dest_path)} bytes)")

def ensure_7z():
    seven_z = os.path.join(BIN_DIR, "7z.exe")
    seven_z_dll = os.path.join(BIN_DIR, "7z.dll")
    if os.path.exists(seven_z) and os.path.exists(seven_z_dll):
        return seven_z

    log("Setting up standalone 7-Zip extractor...")
    os.makedirs(BIN_DIR, exist_ok=True)
    msi_path = os.path.join(DOWNLOADS_DIR, "7z.msi")
    download_file("https://www.7-zip.org/a/7z2409-x64.msi", msi_path)
    
    extract_temp = os.path.join(DOWNLOADS_DIR, "7z-temp")
    os.makedirs(extract_temp, exist_ok=True)
    cmd = ["msiexec.exe", "/a", msi_path, "/qn", f"TARGETDIR={extract_temp}"]
    res = subprocess.run(cmd)
    if res.returncode != 0:
        raise RuntimeError(f"msiexec failed with code {res.returncode}")
    
    # Locate 7z.exe and 7z.dll
    for root, dirs, files in os.walk(extract_temp):
        for f in files:
            if f.lower() == "7z.exe":
                shutil.copy2(os.path.join(root, f), seven_z)
            elif f.lower() == "7z.dll":
                shutil.copy2(os.path.join(root, f), seven_z_dll)
    
    shutil.rmtree(extract_temp, ignore_errors=True)
    if os.path.exists(seven_z) and os.path.exists(seven_z_dll):
        log(f"7-Zip ready at: {seven_z}")
        return seven_z
    raise RuntimeError("Failed to extract 7z.exe and 7z.dll from MSI")

def setup_w64devkit(seven_z_exe):
    gcc_exe = os.path.join(W64DEVKIT_DIR, "bin", "gcc.exe")
    if os.path.exists(gcc_exe):
        log("w64devkit is already installed.")
        return gcc_exe

    log("Setting up w64devkit (GCC, Binutils, Make)...")
    url = "https://github.com/skeeto/w64devkit/releases/download/v2.10.0/w64devkit-x64-2.10.0.7z.exe"
    dest_sfx = os.path.join(DOWNLOADS_DIR, "w64devkit-x64.7z.exe")
    download_file(url, dest_sfx)

    log("Extracting w64devkit...")
    cmd = [seven_z_exe, "x", dest_sfx, f"-o{TOOLS_DIR}", "-y"]
    res = subprocess.run(cmd, stdout=subprocess.DEVNULL)
    if res.returncode != 0:
        raise RuntimeError(f"7z extraction of w64devkit failed with code {res.returncode}")
    
    if os.path.exists(gcc_exe):
        log(f"w64devkit GCC ready: {gcc_exe}")
        return gcc_exe
    raise RuntimeError(f"w64devkit extraction completed but {gcc_exe} was not found")

def setup_qemu(seven_z_exe):
    qemu_exe = os.path.join(QEMU_DIR, "qemu-system-x86_64.exe")
    ovmf_fd = os.path.join(QEMU_DIR, "share", "edk2-x86_64-code.fd")
    
    if os.path.exists(qemu_exe) and (os.path.exists(ovmf_fd) or os.path.exists(os.path.join(QEMU_DIR, "ovmf.fd"))):
        log("QEMU is already installed.")
        return qemu_exe

    log("Setting up QEMU for Windows + UEFI (OVMF)...")
    # Download QEMU installer
    url = "https://qemu.weilnetz.de/w64/2026/qemu-w64-setup-20260811.exe"
    dest_exe = os.path.join(DOWNLOADS_DIR, "qemu-setup.exe")
    download_file(url, dest_exe)

    log("Extracting QEMU system emulator and firmware...")
    os.makedirs(QEMU_DIR, exist_ok=True)
    # Extract only x86_64 emulator, DLLs, and share/ (firmware)
    cmd = [
        seven_z_exe, "x", dest_exe,
        f"-o{QEMU_DIR}",
        "qemu-system-x86_64.exe",
        "*.dll",
        "share/*",
        "-r", "-y"
    ]
    res = subprocess.run(cmd, stdout=subprocess.DEVNULL)
    if res.returncode != 0:
        raise RuntimeError(f"7z extraction failed with code {res.returncode}")

    if os.path.exists(qemu_exe):
        log(f"QEMU ready: {qemu_exe}")
        return qemu_exe
    raise RuntimeError(f"QEMU extraction completed but {qemu_exe} was not found")

def setup_git(seven_z_exe):
    git_dir = os.path.join(TOOLS_DIR, "git")
    git_exe = os.path.join(git_dir, "cmd", "git.exe")
    if os.path.exists(git_exe):
        log("MinGit is already installed.")
        return git_exe

    log("Setting up portable MinGit...")
    url = "https://github.com/git-for-windows/git/releases/download/v2.55.0.windows.5/MinGit-2.55.0.5-64-bit.zip"
    dest_zip = os.path.join(DOWNLOADS_DIR, "mingit.zip")
    download_file(url, dest_zip)

    log("Extracting MinGit...")
    os.makedirs(git_dir, exist_ok=True)
    with zipfile.ZipFile(dest_zip, 'r') as zip_ref:
        zip_ref.extractall(git_dir)

    if os.path.exists(git_exe):
        log(f"MinGit ready: {git_exe}")
        return git_exe
    raise RuntimeError(f"MinGit extraction completed but {git_exe} was not found")

def main():
    log("Starting portable toolchain setup...")
    os.makedirs(DOWNLOADS_DIR, exist_ok=True)
    os.makedirs(BIN_DIR, exist_ok=True)
    
    seven_z = ensure_7z()
    gcc = setup_w64devkit(seven_z)
    qemu = setup_qemu(seven_z)
    git = setup_git(seven_z)
    
    log("=" * 60)
    log("All MinOS portable tools installed successfully!")
    log(f"  GCC:  {gcc}")
    log(f"  QEMU: {qemu}")
    log(f"  Git:  {git}")
    log("=" * 60)

if __name__ == "__main__":
    main()
