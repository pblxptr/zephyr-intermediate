#!/usr/bin/env python3
"""Capture a 2.9 s native_sim run, inspect threads, and decode its CTF."""
import pathlib
import re
import shutil
import subprocess
import sys
import time

exe, output, metadata = map(pathlib.Path, sys.argv[1:])
output.mkdir(parents=True, exist_ok=True)
ctf = output / 'ctf'
ctf.mkdir(exist_ok=True)
shutil.copyfile(metadata, ctf / 'metadata')
with (output / 'console.raw').open('wb') as console:
    proc = subprocess.Popen(
        [str(exe.resolve()), '-stop_at=2.9',
         '-trace-file=' + str((ctf / 'channel0_0').resolve())],
        stdin=subprocess.PIPE, stdout=console, stderr=subprocess.STDOUT)
    try:
        time.sleep(0.2)
        proc.stdin.write(b'kernel thread list\n')
        proc.stdin.flush()
        assert proc.wait(timeout=20) == 0
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait()
text = (output / 'console.raw').read_text()
text = re.sub(r'\x1b\[[0-?]*[ -/]*[@-~]', '', text).replace('\r', '')
(output / 'console.txt').write_text('\n'.join(line.rstrip() for line in text.splitlines()) + '\n')
(output / 'console.raw').unlink()
with (output / 'decoded.txt').open('w') as decoded:
    subprocess.run(['babeltrace2', '--clock-seconds', str(ctf)], stdout=decoded, check=True)
decoded = output / 'decoded.txt'
decoded.write_text('\n'.join(line.rstrip() for line in decoded.read_text().splitlines()) + '\n')
print(text)
