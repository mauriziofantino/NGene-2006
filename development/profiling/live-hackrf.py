"""Finite Windows live RX with isolated configuration and graceful console stop.

Uses the production receiver and its existing logs, without profiling patches.
The receiver gets its own hidden console so its stop event cannot reach the
user's terminal or unrelated processes. Requires the native Windows executable.
"""
import argparse
import ctypes
import hashlib
import json
import pathlib
import re
import shutil
import subprocess
import sys
import time


def stop_receiver(pid):
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.FreeConsole()
    if not kernel.AttachConsole(pid):
        raise ctypes.WinError(ctypes.get_last_error())
    # Handle CTRL_BREAK in this helper as well; ignoring CTRL_C alone does
    # not suppress the default CTRL_BREAK termination action on Windows.
    handler_type = ctypes.WINFUNCTYPE(ctypes.c_int, ctypes.c_uint)
    handler = handler_type(lambda event: 1)
    kernel.SetConsoleCtrlHandler.argtypes = [handler_type, ctypes.c_int]
    if not kernel.SetConsoleCtrlHandler(handler, True):
        raise ctypes.WinError(ctypes.get_last_error())
    if not kernel.GenerateConsoleCtrlEvent(1, 0):  # CTRL_BREAK, this console only
        raise ctypes.WinError(ctypes.get_last_error())
    time.sleep(.2)
    kernel.FreeConsole()


if len(sys.argv) == 3 and sys.argv[1] == '--stop-pid':
    stop_receiver(int(sys.argv[2]))
    sys.exit(0)

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('output', type=pathlib.Path)
p.add_argument('--seconds', type=int, default=180)
p.add_argument('--vga', type=int)
p.add_argument('--gps-only', action='store_true')
p.add_argument('--holdover-ms', type=int)
p.add_argument('--kf', action='store_true')
p.add_argument('--tracking-log', action='store_true')
a = p.parse_args()
assert sys.platform == 'win32'
assert 1 <= a.seconds <= 1800
root = pathlib.Path(__file__).resolve().parents[2]
out = a.output.resolve()
assert root in out.parents and not out.exists()
out.mkdir(parents=True)
shutil.copy2(root/'ngene.exe', out/'ngene.exe')
for folder in ['configuration', 'otherData']:
    shutil.copytree(root/folder, out/folder)


def set_tag(filename, tag, value):
    path = out/'configuration'/filename
    text = path.read_text(encoding='utf-8-sig')
    pattern = r'(\['+re.escape(tag)+r'\][ \t]*\r?\n)[ \t]*[^\r\n]+'
    if re.search(pattern, text):
        text = re.sub(pattern, lambda m:m[1]+str(value), text)
    else:
        text += f'\n[{tag}]\n{value}\n'
    path.write_text(text, encoding='utf-8')


set_tag('GNSS_RX.conf', 'Input method', 0)
if a.gps_only:
    set_tag('GNSS_RX.conf', 'Number of supported Galileo channels', 0)
    set_tag('GNSS_RX.conf', 'Number of supported EGNOS channels', 0)
if a.vga is not None:
    assert 0 <= a.vga <= 62 and a.vga % 2 == 0
    set_tag('HackRF.conf', 'VGA gain (dB)', a.vga)
if a.holdover_ms is not None:
    assert 0 <= a.holdover_ms <= 10000
    set_tag('Tracking.conf', 'Tracking holdover ms', a.holdover_ms)
if a.kf:
    set_tag('PVT.conf', 'PVT method', 1)
set_tag('Logs.conf', 'Dump Path', './logs')
set_tag('Logs.conf', 'Enable PVT Text Log', 1)
set_tag('Logs.conf', 'Enable Navigation Message Log', 1)
set_tag('Logs.conf', 'Enable Tracking Log', int(a.tracking_log))

startup = subprocess.STARTUPINFO()
startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
startup.wShowWindow = 0
command = [str(out/'ngene.exe'), '-t', '--frontend', 'hackrf', '-v']
start = time.monotonic()
metadata = dict(command=command, requested_seconds=a.seconds, vga=a.vga,
                gps_only=a.gps_only, holdover_ms=a.holdover_ms, kf=a.kf,
                executable_sha256=hashlib.sha256((out/'ngene.exe').read_bytes()).hexdigest())
metadata['config_sha256'] = {f.name:hashlib.sha256(f.read_bytes()).hexdigest()
                              for f in (out/'configuration').glob('*.conf')}
with (out/'stdout.log').open('wb') as stdout, (out/'stderr.log').open('wb') as stderr:
    proc = subprocess.Popen(command, cwd=out, stdout=stdout, stderr=stderr,
                            creationflags=subprocess.CREATE_NEW_CONSOLE, startupinfo=startup)
    metadata['pid'] = proc.pid
    (out/'running.json').write_text(json.dumps(metadata, indent=2), encoding='utf-8')
    print(f'LIVE PID {proc.pid}: {out}', flush=True)
    stopped = False
    try:
        while proc.poll() is None and time.monotonic()-start < a.seconds:
            time.sleep(.5)
        if proc.poll() is None:
            helper = subprocess.run([sys.executable, __file__, '--stop-pid', str(proc.pid)],
                                    creationflags=subprocess.CREATE_NO_WINDOW,
                                    capture_output=True, timeout=10)
            (out/'stop-helper.log').write_bytes(helper.stdout+helper.stderr)
            metadata['stop_helper_exit'] = helper.returncode
            proc.wait(timeout=15)
            stopped = True
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait()
            metadata['forced_stop'] = True
    metadata.update(wall_seconds=time.monotonic()-start, exit_code=proc.returncode,
                    planned_stop=stopped)
(out/'results.json').write_text(json.dumps(metadata, indent=2), encoding='utf-8')
print(json.dumps(metadata), flush=True)
sys.exit(0 if metadata['planned_stop'] and proc.returncode == 0 else 1)
