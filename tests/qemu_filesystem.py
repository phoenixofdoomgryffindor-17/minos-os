"""Deterministic QEMU acceptance test for the persistent MinOS filesystem.

The 100-file workload is invoked once through the kernel's real diagnostic
command, avoiding thousands of keyboard events.  Persistence still uses the
real BashPlus commands across separate QEMU boots.
"""
import json
import os
import socket
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
QEMU = os.path.join(ROOT, "tools", "qemu", "qemu-system-x86_64.exe")
OVMF = os.path.join(ROOT, "tools", "qemu", "share", "edk2-x86_64-code.fd")
IMAGE = os.path.join(ROOT, "build", "minos.img")
BUILD = os.path.join(ROOT, "build")

def qmp(sock, command):
    sock.sendall((json.dumps(command) + "\n").encode())
    time.sleep(.02)
    return sock.recv(8192)

def type_command(sock, text):
    # QEMU qcodes cover the intentionally simple ASCII test grammar.
    encoded = []
    for ch in text:
        key = {" ": "spc", "/": "slash", ".": "dot", "-": "minus"}.get(ch, ch)
        encoded.append({"type": "qcode", "data": key})
    for start in range(0, len(encoded), 2):
        qmp(sock, {"execute": "send-key", "arguments": {"keys": encoded[start:start + 2]}})
        time.sleep(.04)
    qmp(sock, {"execute": "send-key", "arguments": {"keys": [{"type": "qcode", "data": "ret"}]}})

def wait_for(log, marker, seconds=20):
    end = time.time() + seconds
    while time.time() < end:
        if os.path.exists(log) and marker in open(log, encoding="utf-8", errors="replace").read():
            return True
        time.sleep(.2)
    return False

def main():
    subprocess.check_call([sys.executable, os.path.join(ROOT, "build.py"), "--clean-image"])
    def boot(phase, commands):
        log = os.path.join(BUILD, "fs_acceptance_%d.log" % phase)
        port = 5556 + phase
        with open(log, "w", encoding="utf-8"):
            pass
        proc = subprocess.Popen([QEMU, "-drive", "if=pflash,format=raw,readonly=on,file=" + OVMF,
                                 "-drive", "format=raw,file=" + IMAGE, "-m", "256M",
                                 "-serial", "file:" + log, "-display", "none",
                                 "-qmp", "tcp:127.0.0.1:%d,server,nowait" % port,
                                 "-no-reboot"])
        s = None
        try:
            if not wait_for(log, "[MinOS Console] Ready. Type help for commands."):
                raise RuntimeError("kernel did not boot in phase %d" % phase)
            s = socket.create_connection(("127.0.0.1", port), 5)
            s.settimeout(5)
            s.recv(4096); qmp(s, {"execute": "qmp_capabilities"})
            for command in commands:
                type_command(s, command)
                if command == "fstest":
                    if not wait_for(log, "[MinOS FS Test] passed 100/100", 60):
                        raise RuntimeError("100-file filesystem test did not finish")
                else:
                    time.sleep(.2)
            time.sleep(.5)
        finally:
            if s is not None:
                s.close()
            if proc.poll() is None:
                proc.terminate()
                proc.wait(timeout=5)
        return open(log, encoding="utf-8", errors="replace").read()

    try:
        first = boot(1, ["fstest", "touch /persist", "write /persist first"])
        if ("[MinOS VFS] metadata initialized" not in first and
                "[MinOS VFS] metadata loaded" not in first) or \
                "[MinOS FS Test] passed 100/100" not in first:
            raise RuntimeError("filesystem did not mount or complete the test")

        second = boot(2, ["append /persist second", "cat /persist", "rm /persist"])
        if "[MinOS VFS] metadata loaded" not in second or "[MinOS Console] cat output: firstsecond" not in second:
            raise RuntimeError("append/read persistence failed")

        third = boot(3, ["cat /persist"])
        if "[MinOS VFS] metadata loaded" not in third or "cat failed" not in third:
            raise RuntimeError("deletion did not survive reboot")
        print("filesystem acceptance passed")
    except Exception:
        raise

if __name__ == "__main__":
    main()
