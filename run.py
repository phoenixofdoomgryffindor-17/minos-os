"""
MinOS Run and Test Harness
Launches QEMU with the generated MinOS UEFI image and captures COM1 serial logs.
Usage:
  python run.py             # Normal run (opens graphical window)
  python run.py --headless  # Run without GUI window
  python run.py --test      # Automated test mode (verifies boot & halts)
"""

import os
import sys
import subprocess
import time

PROJECT_ROOT = os.path.dirname(os.path.abspath(__file__))
TOOLS_DIR = os.path.join(PROJECT_ROOT, "tools")
BUILD_DIR = os.path.join(PROJECT_ROOT, "build")
QEMU_EXE = os.path.join(TOOLS_DIR, "qemu", "qemu-system-x86_64.exe")
OVMF_BIOS = os.path.join(TOOLS_DIR, "qemu", "share", "edk2-x86_64-code.fd")
DISK_IMG = os.path.join(BUILD_DIR, "minos.img")
SERIAL_LOG = os.path.join(BUILD_DIR, "serial.log")

def ensure_built():
    if not os.path.exists(DISK_IMG):
        print("[MinOS Run] Disk image not found. Running build.py...")
        build_script = os.path.join(PROJECT_ROOT, "build.py")
        res = subprocess.run([sys.executable, build_script])
        if res.returncode != 0:
            print("[MinOS Run] Build failed!")
            sys.exit(res.returncode)

def run():
    ensure_built()
    
    if not os.path.exists(QEMU_EXE):
        print(f"[MinOS Run] ERROR: QEMU not found at {QEMU_EXE}")
        sys.exit(1)
        
    if not os.path.exists(OVMF_BIOS):
        print(f"[MinOS Run] ERROR: OVMF firmware not found at {OVMF_BIOS}")
        sys.exit(1)

    is_test = "--test" in sys.argv
    is_headless = "--headless" in sys.argv or is_test
    screenshot_ppm = os.path.join(BUILD_DIR, "boot_screen.ppm")

    # Clear previous logs
    for f_to_clean in (SERIAL_LOG, screenshot_ppm):
        if os.path.exists(f_to_clean):
            try:
                os.remove(f_to_clean)
            except OSError:
                pass

    cmd = [
        QEMU_EXE,
        "-drive", f"if=pflash,format=raw,readonly=on,file={OVMF_BIOS}",
        "-drive", f"format=raw,file={DISK_IMG}",
        "-m", "256M",
        "-serial", f"file:{SERIAL_LOG}",
        "-vga", "std",
        "-no-reboot",
    ]

    if is_headless:
        cmd += ["-display", "none"]

    if is_test:
        # Add QMP monitor on port 5555 for screendump
        cmd += ["-qmp", "tcp:127.0.0.1:5555,server,nowait"]

    print("[MinOS Run] Starting QEMU...")
    print("  Command:", " ".join(cmd))
    
    proc = subprocess.Popen(cmd)

    if is_test:
        print("[MinOS Run] Waiting for kernel boot and serial confirmation...")
        success = False
        start_time = time.time()
        timeout = 15 # 15 seconds max

        while time.time() - start_time < timeout:
            time.sleep(0.5)
            if os.path.exists(SERIAL_LOG):
                try:
                    with open(SERIAL_LOG, "r", encoding="utf-8", errors="replace") as f:
                        log_content = f.read()
                        if ("[MinOS Kernel] Milestone 1 verification complete." in log_content
                                and "[MinOS Console] Ready. Type help for commands." in log_content
                                and "[MinOS Timer] First periodic IRQ received" in log_content):
                            success = True
                            break
                except Exception:
                    pass

        # Capture framebuffer screenshot via QMP if booted
        if success:
            try:
                import socket
                import json
                s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                s.connect(("127.0.0.1", 5555))
                s.recv(1024) # Greeting
                s.sendall(b'{"execute": "qmp_capabilities"}\n')
                s.recv(1024)
                for key in ("h", "e", "l", "p", "enter"):
                    key_cmd = json.dumps({
                        "execute": "send-key",
                        "arguments": {"keys": [{"type": "qcode", "data": key}]}
                    }).encode("utf-8") + b"\n"
                    s.sendall(key_cmd)
                    time.sleep(0.05)
                    s.recv(1024)
                screendump_cmd = json.dumps({"execute": "screendump", "arguments": {"filename": screenshot_ppm}}).encode('utf-8') + b'\n'
                s.sendall(screendump_cmd)
                time.sleep(0.5)
                s.close()
                if os.path.exists(screenshot_ppm):
                    print(f"[MinOS Run] Framebuffer screendump captured: {screenshot_ppm} ({os.path.getsize(screenshot_ppm)} bytes)")
            except Exception as e:
                print(f"[MinOS Run] Warning: Screendump capture failed ({e})")

        # Terminate QEMU after test run
        try:
            proc.terminate()
            proc.wait(timeout=3)
        except Exception:
            proc.kill()

        print("\n" + "=" * 60)
        print("                  MINOS SERIAL OUTPUT                    ")
        print("=" * 60)
        if os.path.exists(SERIAL_LOG):
            with open(SERIAL_LOG, "r", encoding="utf-8", errors="replace") as f:
                print(f.read())
        print("=" * 60)

        if success:
            with open(SERIAL_LOG, "r", encoding="utf-8", errors="replace") as f:
                log_content = f.read()
            if "[MinOS Keyboard] key=" not in log_content:
                print("[MinOS Run] TEST FAILED: QEMU keyboard input did not reach the console.")
                sys.exit(1)
            print("[MinOS Run] TEST PASSED: Timer IRQ and keyboard IRQ delivery verified!")
            sys.exit(0)
        else:
            print("[MinOS Run] TEST FAILED: periodic APIC timer IRQ marker not found within timeout.")
            sys.exit(1)
    else:
        print("[MinOS Run] QEMU running. Close the QEMU window or terminate to exit.")
        try:
            proc.wait()
        except KeyboardInterrupt:
            proc.terminate()

        if os.path.exists(SERIAL_LOG):
            print("\n[MinOS Run] Serial log captured at:", SERIAL_LOG)

if __name__ == "__main__":
    run()
