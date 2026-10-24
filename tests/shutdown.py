#!/usr/bin/env python3
"""Exercise exit confirmation with a real world, X display, and configured TAP.

Usage: python3 tests/shutdown.py -- ./src/genera -world WORLD -debugger DEBUGGER ...
The world must boot unattended. This starts and stops four temporary instances.
"""

import argparse
import os
import selectors
import signal
import subprocess
import time


class Session:
    def __init__(self, command, timeout):
        self.process = subprocess.Popen(
            command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, start_new_session=True, bufsize=0,
        )
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.process.stdout, selectors.EVENT_READ)
        self.timeout = timeout
        self.output = bytearray()
        self.position = 0

    def expect(self, text):
        deadline = time.monotonic() + self.timeout
        while True:
            found = self.output.find(text, self.position)
            if found >= 0:
                self.position = found + len(text)
                return
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not self.selector.select(remaining):
                raise AssertionError(f"Timed out waiting for {text!r}")
            chunk = os.read(self.process.stdout.fileno(), 65536)
            if not chunk:
                raise AssertionError(f"Process exited before {text!r}")
            self.output.extend(chunk)

    def answer(self, text):
        self.process.stdin.write(text)
        self.process.stdin.flush()

    def close(self):
        if self.process.poll() is None:
            self.process.kill()
        self.process.wait()
        self.process.stdin.close()
        self.process.stdout.close()
        self.selector.close()


def check_shutdown(command, signum, startup_wait, timeout):
    session = Session(command, timeout)
    try:
        session.expect(b"NumLock will not be functional in genera")
        time.sleep(startup_wait)
        assert session.process.poll() is None, "World exited during startup"

        session.process.send_signal(signum)
        session.expect(b"Lisp is running!")
        session.expect(b"Do you still wish to exit?  (yes or no) ")
        session.answer(b"maybe\n")
        session.expect(b"Please answer 'yes' or 'no'.")
        session.answer(b"no\n")
        time.sleep(1)
        assert session.process.poll() is None, "Declining exit stopped the world"

        session.process.send_signal(signum)
        session.expect(b"Lisp is running!")
        session.expect(b"Do you still wish to exit?  (yes or no) ")
        started = time.monotonic()
        session.answer(b"yes\n")
        output, _ = session.process.communicate(timeout=timeout)
        session.output.extend(output)
        status = session.process.returncode
        assert status == 0, f"Shutdown returned {status}"
        print(f"PASS {signum.name}: declined exit, resumed, then exited in "
              f"{time.monotonic() - started:.2f}s", flush=True)
    except Exception:
        print(session.output.decode(errors="replace"), flush=True)
        raise
    finally:
        session.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--startup-wait", type=float, default=5,
                        help="seconds to allow the world to boot (default: 5)")
    parser.add_argument("--timeout", type=float, default=10,
                        help="timeout for each prompt/exit (default: 10)")
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command
    if command and command[0] == "--":
        command = command[1:]
    if not command:
        parser.error("provide the genera command after --")
    for signum in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP, signal.SIGQUIT):
        check_shutdown(command, signum, args.startup_wait, args.timeout)
