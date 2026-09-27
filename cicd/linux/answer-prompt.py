#!/usr/bin/env python3

# Run a command on a terminal of its own and type one answer at its prompt.
#
# The installers only ask when stdin is a terminal, so a pipe cannot answer
# them. script(1) gives them one, but nothing on it answers the cursor
# position queries .NET sends before reading a line, and each of those waits
# out a timeout of its own - fourteen seconds a question for install.ps1.
# Here every query is answered at once.
#
# The answer is typed once the prompt text has been seen and the output has
# gone quiet. Everything the command prints is passed through; the exit code
# is the command's.
#
# Syntax: answer-prompt.py <prompt-text> <answer> <command> [args...]

# Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
# Licensed under The MIT License (MIT). Full text at:
#     https://mit-license.org/
# SPDX-License-Identifier: MIT

import os
import pty
import select
import signal
import sys
import time

CURSOR_QUERY = b"\x1b[6n"
CURSOR_REPLY = b"\x1b[1;1R"
TIMEOUT_SECONDS = 120


def main(argv):
    if len(argv) < 4:
        sys.stderr.write("usage: answer-prompt.py <prompt-text> <answer> <command> [args...]\n")
        return 2
    prompt = argv[1].encode()
    answer = argv[2].encode() + b"\r"
    command = argv[3:]

    pid, fd = pty.fork()
    if pid == 0:
        try:
            os.execvp(command[0], command)
        finally:
            os._exit(127)

    seen = b""
    queries = 0
    typed = False
    deadline = time.monotonic() + TIMEOUT_SECONDS
    while True:
        if time.monotonic() > deadline:
            os.kill(pid, signal.SIGKILL)
            os.waitpid(pid, 0)
            sys.stderr.write("answer-prompt.py: gave up waiting on %s\n" % command[0])
            return 124
        ready, _, _ = select.select([fd], [], [], 0.3)
        if not ready:
            if prompt in seen and not typed:
                os.write(fd, answer)
                typed = True
            continue
        try:
            data = os.read(fd, 4096)
        except OSError:  # the terminal goes away with the command
            break
        if not data:
            break
        sys.stdout.buffer.write(data)
        sys.stdout.buffer.flush()
        seen += data
        while seen.count(CURSOR_QUERY) > queries:
            os.write(fd, CURSOR_REPLY)
            queries += 1

    _, status = os.waitpid(pid, 0)
    return os.waitstatus_to_exitcode(status)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
