#!/usr/bin/env python3
"""Run guest Gabriel benchmarks on a private Xvfb display and collect CSV output.

Requires an existing TAP/NFS setup; refuses to run alongside another Genera.
Only the disposable test instance is stopped on completion or timeout.
"""

import ctypes as c
import argparse
import json
import os
import re
import resource
import signal
import subprocess
import time
import shutil
from pathlib import Path
x = c.CDLL('libX11.so.6')
x.XOpenDisplay.argtypes = [c.c_char_p]
x.XOpenDisplay.restype = c.c_void_p
x.XDefaultRootWindow.argtypes = [c.c_void_p]
x.XDefaultRootWindow.restype = c.c_ulong
x.XQueryTree.argtypes = [c.c_void_p, c.c_ulong, c.POINTER(c.c_ulong), c.POINTER(c.c_ulong), c.POINTER(c.POINTER(c.c_ulong)), c.POINTER(c.c_uint)]
x.XFetchName.argtypes = [c.c_void_p, c.c_ulong, c.POINTER(c.c_char_p)]
x.XFree.argtypes = [c.c_void_p]
x.XCloseDisplay.argtypes = [c.c_void_p]
x.XKeysymToKeycode.argtypes = [c.c_void_p, c.c_ulong]
x.XKeysymToKeycode.restype = c.c_uint
x.XkbKeycodeToKeysym.argtypes = [c.c_void_p, c.c_uint, c.c_int, c.c_int]
x.XkbKeycodeToKeysym.restype = c.c_ulong
x.XSendEvent.argtypes = [c.c_void_p, c.c_ulong, c.c_int, c.c_long, c.c_void_p]
x.XFlush.argtypes = [c.c_void_p]

class Event(c.Structure):
    _fields_ = [('type', c.c_int), ('serial', c.c_ulong), ('send_event', c.c_int), ('display', c.c_void_p), ('window', c.c_ulong), ('root', c.c_ulong), ('subwindow', c.c_ulong), ('time', c.c_ulong), ('x', c.c_int), ('y', c.c_int), ('x_root', c.c_int), ('y_root', c.c_int), ('state', c.c_uint), ('code', c.c_uint), ('same_screen', c.c_int)]

def windows(w):
    root = c.c_ulong()
    parent = c.c_ulong()
    children = c.POINTER(c.c_ulong)()
    n = c.c_uint()
    if x.XQueryTree(d, w, c.byref(root), c.byref(parent), c.byref(children), c.byref(n)):
        ids = [children[i] for i in range(n.value)]
        x.XFree(children)
        for child in ids:
            name = c.c_char_p()
            if x.XFetchName(d, child, c.byref(name)) and name.value:
                title = name.value.decode(errors='replace')
                x.XFree(name)
                yield (child, title)
            yield from windows(child)

def key(w, sym):
    e = Event(2, 0, 1, d, w, root, 0, 0, 10, 10, 10, 10, 0, x.XKeysymToKeycode(d, sym), 1)
    x.XSendEvent(d, w, 0, 1, c.byref(e))
    e.type = 3
    x.XSendEvent(d, w, 0, 2, c.byref(e))
    x.XFlush(d)

def type_text(w, text):
    for ch in text:
        sym = 65293 if ch == '\n' else ord(ch)
        kc = x.XKeysymToKeycode(d, sym)
        shift = x.XkbKeycodeToKeysym(d, kc, 0, 0) != sym
        if shift:
            e = Event(2, 0, 1, d, w, root, 0, 0, 10, 10, 10, 10, 0, x.XKeysymToKeycode(d, 65505), 1)
            x.XSendEvent(d, w, 0, 1, c.byref(e))
        key(w, sym)
        if shift:
            e.type = 3
            x.XSendEvent(d, w, 0, 2, c.byref(e))
            x.XFlush(d)
        time.sleep(0.025)

def shot(w, stage):
    subprocess.run(['import', '-window', str(w), str(output_dir / f'{args.label}-{stage}.png')], env=env, timeout=5)
parser = argparse.ArgumentParser(description='Run Gabriel benchmarks on a private X display.')
parser.add_argument('binary')
parser.add_argument('--label', required=True)
parser.add_argument('--compile', action='store_true')
parser.add_argument('--samples', type=int, default=1)
parser.add_argument('--counts', type=Path)
parser.add_argument('--timeout', type=int, default=900)
parser.add_argument('--cpu', type=int, default=6)
parser.add_argument('--world', default='/home/nathan/dev/Genera-8-6.vlod')
parser.add_argument('--debugger', default='/home/nathan/dev/VLM_debugger')
parser.add_argument('--network', default='tap0:INTERNET|192.168.250.2;mask=255.255.255.0;host=192.168.250.1')
parser.add_argument('--export-dir', type=Path, default=Path('/var/lib/symbolics/vlm-benchmarks'))
parser.add_argument('--guest-dir', default='CHESTNUT:/var/lib/symbolics/vlm-benchmarks')
parser.add_argument('--xvfb', default=shutil.which('Xvfb') or '/tmp/vlm-xvfb/usr/bin/Xvfb')
parser.add_argument('--display', default=':99')
parser.add_argument('--output', type=Path, default=Path('build-bench/results'))
args = parser.parse_args()
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
resource.setrlimit(resource.RLIMIT_FSIZE, (64 * 1024 * 1024, 64 * 1024 * 1024))
os.nice(10)
if subprocess.run(['pgrep', '-x', 'genera'], stdout=subprocess.DEVNULL).returncode == 0:
    raise SystemExit('Another Genera instance is running; refusing to start')
if args.samples < 1 or args.timeout < 1:
    parser.error('samples and timeout must be positive')
if not args.label.replace('-', '').isalnum():
    parser.error('label must be alphanumeric or hyphens')
output_dir = args.output.resolve()
output_dir.mkdir(parents=True, exist_ok=True)
args.export_dir.mkdir(exist_ok=True)
if args.compile and any(args.export_dir.glob('*.vbin*')):
    parser.error('Compile once in a fresh export directory; later boots must reuse the binaries')
for name in ('suite.lisp', 'driver.lisp'):
    source = Path(__file__).parent / name
    target = args.export_dir / name
    if not args.compile:
        if not target.exists() or target.read_bytes() != source.read_bytes():
            parser.error('Staged Lisp sources differ; compile in a fresh export directory')
        if not target.with_suffix('.vbin').exists():
            parser.error('Compiled guest code is missing; first run requires --compile')
    if not target.exists() or target.read_bytes() != source.read_bytes():
        shutil.copyfile(source, target)
result_name = f'{args.label}.csv'
if list(args.export_dir.glob(result_name + '*')):
    raise SystemExit('This label already has output; choose a new label')
counts = json.loads(args.counts.read_text()) if args.counts else {}
if any(not re.fullmatch(r'[A-Z][A-Z0-9-]*', name) or type(n) is not int or n < 1
       for name, n in counts.items()):
    parser.error('Counts must map uppercase benchmark names to positive integers')
counts_lisp = '(' + ' '.join((f'(gabriel::{name} . {int(n)})' for name, n in counts.items())) + ')'
forms = [';;; -*- Mode: Lisp; Package: USER; Syntax: Common-Lisp -*-']
for name in ('suite', 'driver'):
    if args.compile:
        forms.append(f'(load (compile-file "{args.guest_dir}/{name}.lisp"))')
    else:
        forms.append(f'(load "{args.guest_dir}/{name}.vbin")')
forms.append(f"""(gabriel::vlm-benchmarks "{args.guest_dir}/{result_name}" {args.samples} '{counts_lisp})""")
(args.export_dir / 'start.lisp').write_text('\n'.join(forms) + '\n')
env = dict(os.environ, DISPLAY=args.display)
xlog = open(output_dir / f'{args.label}-xvfb.log', 'wb')
log = open(output_dir / f'{args.label}.log', 'wb')
server = subprocess.Popen([args.xvfb, args.display, '-screen', '0', '1920x1200x24', '-ac', '-nolisten', 'tcp', '-extension', 'GLX'], stdout=xlog, stderr=xlog)
p = None
d = None
try:
    time.sleep(1)
    if server.poll() is not None:
        raise RuntimeError('Xvfb failed')
    d = x.XOpenDisplay(args.display.encode())
    assert d
    root = x.XDefaultRootWindow(d)
    cmd = ['taskset', '-c', str(args.cpu), args.binary, '-world', args.world, '-debugger', args.debugger, '-network', args.network, '-vm', '1024', '-coldloadiconic', 'yes', '-coldloadgeometry', '800x600', '-spy', 'no']
    p = subprocess.Popen(cmd, env=env, stdin=subprocess.PIPE, stdout=log, stderr=log, start_new_session=True)
    print('benchmark pid', p.pid, 'label', args.label, flush=True)
    time.sleep(8)
    win = [item for item in windows(root) if 'Genera on ' in item[1]]
    assert win, list(windows(root))
    w = win[0][0]
    type_text(w, 'Login lisp-machine\n')
    time.sleep(3)
    type_text(w, f'(load "{args.guest_dir}/start.lisp")\n')
    start_time = time.monotonic()
    last_rows = -1
    last_progress = start_time
    while time.monotonic() - start_time < args.timeout:
        if p.poll() is not None:
            raise RuntimeError(f'Genera exited: {p.returncode}')
        time.sleep(5)
        files = list(args.export_dir.glob(result_name + '*'))
        done = [f for f in files if '.done' in f.name]
        csvs = [f for f in files if '.done' not in f.name and '.values' not in f.name]
        if csvs:
            result = max(csvs, key=lambda f: f.stat().st_mtime)
            data = result.read_bytes()
            rows = data.count(b'\n')
            if rows != last_rows:
                print(args.label, 'elapsed', round(time.monotonic() - start_time, 1), 'rows', rows, flush=True)
                (output_dir / result_name).write_bytes(data)
                last_rows = rows
                last_progress = time.monotonic()
        if done or time.monotonic() - last_progress > 30:
            shot(w, 'screen')
        if done:
            print('COMPLETE', args.label, 'elapsed', round(time.monotonic() - start_time, 1), flush=True)
            (output_dir / f'{args.label}-complete.txt').write_text(done[0].read_text())
            for f in files:
                if '.values' in f.name:
                    shutil.copyfile(f, output_dir / f.name)
            break
    else:
        raise RuntimeError('Benchmark timed out; inspect screenshot')
finally:
    if p is not None and p.poll() is None:
        os.killpg(p.pid, signal.SIGTERM)
        try:
            p.communicate(b'yes\n', timeout=3)
        except subprocess.TimeoutExpired:
            os.killpg(p.pid, signal.SIGKILL)
            p.wait()
    if d:
        x.XCloseDisplay(d)
    server.terminate()
    server.wait(timeout=3)
    print('exit', p.returncode if p else None, flush=True)
