"""A KIM-1 on a pty: the monitor commands kimtape uses, and G running the
simulated PAL-II (kimformer.sim) on what was loaded."""

import os
import pty
import termios
import threading

from kimdos.sim import INPUT, MONITOR
from kimtape.tape import Tape
from kimformer.sim import Machine

HEX = b"0123456789ABCDEF"
EAL = 0x17F7


class FakeKim:
    """Serves the monitor on a pty from a thread; dev is the device to open.
    G runs the machine until it returns to the monitor."""

    def __init__(self, stories):
        self.fd, self.slave = pty.openpty()
        for fd in (self.fd, self.slave):
            attrs = termios.tcgetattr(fd)
            attrs[0] = attrs[1] = attrs[3] = 0
            termios.tcsetattr(fd, termios.TCSANOW, attrs)
        self.dev = os.ttyname(self.slave)
        self.machine = Machine(stories, image=os.devnull, timer=0x5A)
        self.addr = 0
        self.thread = threading.Thread(target=self.serve, daemon=True)
        self.thread.start()

    def getc(self):
        """One character from the line, echoed; b'' once the line closes."""
        try:
            ch = os.read(self.fd, 1)
        except OSError:
            return b""
        os.write(self.fd, ch)
        return ch

    def prompt(self):
        """The monitor's address and contents display."""
        value = self.machine.kim.read(self.addr)[0]
        os.write(self.fd, b"\r\n\0\0%04X %02X " % (self.addr, value))

    def serve(self):
        """The monitor command loop."""
        digits = b""
        while ch := self.getc():
            if ch in HEX:
                digits += ch
                continue
            if ch == b" ":
                self.addr = int(digits[-4:] or b"0", 16)
            elif ch == b".":
                self.machine.kim.write(self.addr, bytes([int(digits[-2:], 16)]))
                self.addr += 1
            elif ch == b"\r":
                os.write(self.fd, b"\r\nKIM")
            elif ch == b"L":
                self.load()
            elif ch == b"Q":
                self.punch()
            elif ch == b"G":
                self.run()
            digits = b""
            self.prompt()

    def load(self):
        """The ROM's L: skip to each ;, store the record, check its sum; a zero
        count ends the tape with KIM, a bad sum leaves L with ERR."""
        while ch := self.getc():
            if ch != b";":
                continue
            head = self.hex_bytes(3)
            data = self.hex_bytes(head[0])
            got = self.hex_bytes(2)
            if not head[0]:
                os.write(self.fd, b"\x13 KIM\r\n\0\0\r\n\0\0KIM")
                return
            self.machine.kim.write(int.from_bytes(head[1:], "big"), data)
            if int.from_bytes(got, "big") != sum(head + data) & 0xFFFF:
                os.write(self.fd, b"\x13 ERR\x13 KIM\r\n\0\0\r\n\0\0KIM")
                return

    def hex_bytes(self, n):
        """n bytes read as hex digit pairs."""
        return bytes.fromhex(b"".join(self.getc() for _ in range(2 * n)).decode())

    def punch(self):
        """Punch from the open address to the end address at EAL."""
        end = int.from_bytes(self.machine.kim.read(EAL, 2), "little")
        data = self.machine.kim.read(self.addr, end - self.addr)
        tape = Tape.from_memory("q", dict(zip(range(self.addr, end), data)))
        os.write(self.fd, b"\r\n" + tape.text())

    def run(self):
        """Run from the open address, typing what arrives, until the program
        returns to the monitor."""
        kim, sent = self.machine.kim, len(self.machine.kim.out)
        kim.mpu.pc = self.addr
        while True:
            reason = kim.run(1_000_000)
            os.write(self.fd, bytes(kim.out[sent:]))
            sent = len(kim.out)
            if reason == MONITOR:
                return
            if reason == INPUT:
                try:
                    kim.type(os.read(self.fd, 1).decode("latin-1"))
                except OSError:
                    return

    def close(self):
        """Hang up the line and stop serving; call after the port is closed."""
        os.close(self.slave)
        self.thread.join(5)
        os.close(self.fd)
