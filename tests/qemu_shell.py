"""Short QEMU shell regression using the real BashPlus console and VFS."""
import json
import os
import socket
import subprocess
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
QEMU = os.path.join(ROOT, "tools", "qemu", "qemu-system-x86_64.exe")
OVMF = os.path.join(ROOT, "tools", "qemu", "share", "edk2-x86_64-code.fd")
IMAGE = os.path.join(ROOT, "build", "minos.img")
LOG = os.path.join(ROOT, "build", "shell_acceptance.log")


def qmp(sock, command):
    sock.sendall((json.dumps(command) + "\n").encode())
    time.sleep(.03)
    return sock.recv(8192)


def qcode(ch):
    if ch == " ":
        return "spc"
    return {"\\": "backslash", "/": "slash", ".": "dot", "-": "minus"}.get(ch, ch)


def type_command(sock, text):
    for ch in text:
        if "A" <= ch <= "Z":
            qmp(sock, {"execute": "send-key", "arguments": {
                "keys": [{"type": "qcode", "data": "shift"}, {"type": "qcode", "data": ch.lower()}]}})
        else:
            qmp(sock, {"execute": "send-key", "arguments": {
                "keys": [{"type": "qcode", "data": qcode(ch)}]}})
    qmp(sock, {"execute": "send-key", "arguments": {
        "keys": [{"type": "qcode", "data": "ret"}]}})


def wait_for(marker, seconds=15):
    end = time.time() + seconds
    while time.time() < end:
        if os.path.exists(LOG) and marker in open(LOG, encoding="utf-8", errors="replace").read():
            return True
        time.sleep(.1)
    return False


def main():
    commands = [
        "about", "help", "mem", "ticks", "uptime", "uname", "pwd", "ls",
        "MKDIR MyProject", "mkdir MyProject", "touch MyProject/README.txt",
        "touch MyProject/readme.txt", "write MyProject/README.txt \"MinOS test\"",
        "cat MyProject/README.txt", "stat MyProject/README.txt",
        "cp MyProject/README.txt MyProject/Copy.txt",
        "mv MyProject/Copy.txt MyProject/Renamed.txt",
        "mkdir MyProject/Nested", "cd MyProject", "pwd", "cat ./README.txt",
        "cat ../MyProject/readme.txt", "tree .", "find . README",
        "echo \"Hello MinOS\"", "echo 'Quoted text'", "df", "history",
        "rmdir .", "cd Missing", "cat missing.txt", "rm missing.txt",
        "rmdir MyProject", "cd ..", "rm MyProject/README.txt",
        "rm MyProject/readme.txt", "rm MyProject/Renamed.txt",
        "rmdir MyProject/Nested", "rmdir MyProject",
    ]
    with open(LOG, "w", encoding="utf-8"):
        pass
    proc = subprocess.Popen([
        QEMU, "-drive", "if=pflash,format=raw,readonly=on,file=" + OVMF,
        "-drive", "format=raw,file=" + IMAGE, "-m", "256M",
        "-serial", "file:" + LOG, "-display", "none",
        "-qmp", "tcp:127.0.0.1:5565,server,nowait", "-no-reboot",
    ])
    sock = None
    try:
        if not wait_for("[MinOS Console] Ready. Type help for commands."):
            raise RuntimeError("shell test kernel did not boot")
        sock = socket.create_connection(("127.0.0.1", 5565), 5)
        sock.settimeout(5)
        sock.recv(4096)
        qmp(sock, {"execute": "qmp_capabilities"})
        for command in commands:
            type_command(sock, command)
            time.sleep(.12)
        time.sleep(.5)
    finally:
        if sock:
            sock.close()
        if proc.poll() is None:
            proc.terminate()
            proc.wait(timeout=5)
    log = open(LOG, encoding="utf-8", errors="replace").read()
    required = [
        "[MinOS Console] cat output: MinOS test",
        "[MinOS Console] stat: file size=10",
        "Hello MinOS",
        "Quoted text",
        "[MinOS Console] df: total=",
        "[MinOS Console] error",
        "[MinOS Console] command: MKDIR MyProject",
    ]
    missing = [marker for marker in required if marker not in log]
    if missing:
        raise RuntimeError("shell acceptance missing: " + ", ".join(missing))
    print("shell acceptance passed")


if __name__ == "__main__":
    main()
