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


def capture_console(pid, output):
    from ctypes import wintypes as w
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    class Coord(ctypes.Structure):
        _fields_ = [('x', w.SHORT), ('y', w.SHORT)]
    class Rect(ctypes.Structure):
        _fields_ = [('left', w.SHORT), ('top', w.SHORT), ('right', w.SHORT), ('bottom', w.SHORT)]
    class Info(ctypes.Structure):
        _fields_ = [('size', Coord), ('cursor', Coord), ('attributes', w.WORD),
                    ('window', Rect), ('maximum', Coord)]
    kernel.FreeConsole()
    if not kernel.AttachConsole(pid):
        raise ctypes.WinError(ctypes.get_last_error())
    kernel.CreateFileW.argtypes = [w.LPCWSTR,w.DWORD,w.DWORD,w.LPVOID,w.DWORD,w.DWORD,w.HANDLE]
    kernel.CreateFileW.restype = w.HANDLE
    kernel.GetConsoleScreenBufferInfo.argtypes = [w.HANDLE,ctypes.POINTER(Info)]
    kernel.ReadConsoleOutputCharacterW.argtypes = [w.HANDLE,w.LPWSTR,w.DWORD,Coord,ctypes.POINTER(w.DWORD)]
    kernel.CloseHandle.argtypes = [w.HANDLE]
    screens=[]
    try:
        for _ in range(100):
            handle=kernel.CreateFileW('CONOUT$',0x80000000,3,None,3,0,None)
            info=Info()
            if kernel.GetConsoleScreenBufferInfo(handle,ctypes.byref(info)):
                count=info.size.x*(info.window.bottom-info.window.top+1)
                buffer=ctypes.create_unicode_buffer(count+1);read=w.DWORD()
                if kernel.ReadConsoleOutputCharacterW(handle,buffer,count,Coord(0,info.window.top),ctypes.byref(read)):
                    text='\n'.join(buffer.value[k:k+info.size.x].rstrip() for k in range(0,read.value,info.size.x))
                    if not screens or screens[-1]!=text:screens.append(text)
            kernel.CloseHandle(handle)
            time.sleep(.04)
    finally:
        kernel.FreeConsole()
        pathlib.Path(output).write_text(json.dumps(screens,ensure_ascii=False,indent=2),encoding='utf-8')


if len(sys.argv) == 3 and sys.argv[1] == '--stop-pid':
    stop_receiver(int(sys.argv[2]))
    sys.exit(0)
if len(sys.argv) == 4 and sys.argv[1] == '--capture-console':
    capture_console(int(sys.argv[2]),sys.argv[3])
    sys.exit(0)

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('output', type=pathlib.Path)
p.add_argument('--seconds', type=int, default=180)
p.add_argument('--vga', type=int)
p.add_argument('--gps-only', action='store_true')
p.add_argument('--holdover-ms', type=int)
p.add_argument('--kf', action='store_true')
p.add_argument('--tracking-log', action='store_true')
p.add_argument('--ncurses', action='store_true')
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
command = [str(out/'ngene.exe'), '-n' if a.ncurses else '-t', '--frontend', 'hackrf', '-v']
if a.ncurses:
    command += ['-l', str(out/'receiver.log')]
start = time.monotonic()
metadata = dict(command=command, requested_seconds=a.seconds, vga=a.vga,
                gps_only=a.gps_only, holdover_ms=a.holdover_ms, kf=a.kf,
                executable_sha256=hashlib.sha256((out/'ngene.exe').read_bytes()).hexdigest())
metadata['config_sha256'] = {f.name:hashlib.sha256(f.read_bytes()).hexdigest()
                              for f in (out/'configuration').glob('*.conf')}
with (out/'stdout.log').open('wb') as stdout, (out/'stderr.log').open('wb') as stderr:
    proc = subprocess.Popen(command, cwd=out, stdout=None if a.ncurses else stdout,
                            stderr=None if a.ncurses else stderr,
                            creationflags=subprocess.CREATE_NEW_CONSOLE, startupinfo=startup)
    metadata['pid'] = proc.pid
    (out/'running.json').write_text(json.dumps(metadata, indent=2), encoding='utf-8')
    print(f'LIVE PID {proc.pid}: {out}', flush=True)
    if a.ncurses:
        helper = subprocess.run([sys.executable,__file__,'--capture-console',str(proc.pid),str(out/'console-screens.json')],
                                creationflags=subprocess.CREATE_NO_WINDOW,capture_output=True,timeout=10)
        (out/'console-helper.log').write_bytes(helper.stdout+helper.stderr)
        metadata['console_helper_exit']=helper.returncode
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
if a.ncurses and (out/'receiver.log').exists():
    shutil.copyfile(out/'receiver.log',out/'stdout.log')
print(json.dumps(metadata), flush=True)
sys.exit(0 if metadata['planned_stop'] and proc.returncode == 0 else 1)
