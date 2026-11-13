"""Exercise first-use executable capture with an existing revision-isolated BIOS cache.

Usage: python tests/browser_exec_learning_tests.py ps2n.exe prepared-bios-dir owned.log
Only the executable receipt is temporarily withheld; originals and AOT caches stay in place.
"""
from pathlib import Path
import hashlib
import os
import re
import signal
import subprocess
import sys
import time

tool, game, log = (Path(arg).resolve() for arg in sys.argv[1:4])
receipt = game / 'exec/current.txt'
backup = receipt.with_name('current.txt.learning-test')
original = receipt.read_bytes()
fields = dict(line.split('=', 1) for line in original.decode().splitlines()[1:])
assert game.name == 'bios-' + fields['parent_sha256'], 'Not a revision-isolated BIOS cache'
assert re.fullmatch('[0-9a-f]{64}', fields['sha256']), 'Invalid existing identity'
assert not backup.exists(), 'An earlier recovery receipt exists; preserve it'
assert backup.parent.resolve() == receipt.parent.resolve()
binary = receipt.parent / (fields['sha256'] + '.bin')
assert hashlib.sha256(binary.read_bytes()).hexdigest() == fields['sha256']
binary_time = binary.stat().st_mtime_ns
object_suffix = '.obj' if os.name == 'nt' else '.o'
objects = list((tool.parent / 'CMakeFiles' / ('ps2n-ee-' + fields['sha256'] + '.dir/Unity')).glob('unity_ee_*' + object_suffix))
objects += list((tool.parent / 'CMakeFiles' / ('ps2n-game-' + game.name + '.dir/Unity')).glob('*' + object_suffix))
before = {path: path.stat().st_mtime_ns for path in objects}
assert before, 'Expected an existing AOT control'
process = None
success = False
receipt.rename(backup)
try:
    env = os.environ.copy()
    env['PS2N_DEVICE_TRACE'] = '1'
    startup = None
    if os.name == 'nt':
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
    with log.open('wb') as output:
        process = subprocess.Popen([str(tool), 'run', str(game)], stdout=output,
                                   stderr=subprocess.STDOUT, env=env, startupinfo=startup,
                                   start_new_session=os.name != 'nt')
        print('owned_supervisor_pid', process.pid, flush=True)
        start = time.monotonic()
        while process.poll() is None:
            native_log = game / 'run.log'
            if native_log.exists() and native_log.stat().st_size > 4 * 1024 * 1024:
                break
            text = native_log.read_text(encoding='utf-8', errors='replace') if native_log.exists() else ''
            supervisor_text = log.read_text(encoding='utf-8', errors='replace')
            if ('Original EE payload verified; preparing its native module' in supervisor_text and
                    receipt.exists() and '[guest-exec:handoff] module=' + fields['sha256'] in text):
                success = True
                break
            if time.monotonic() - start >= 90 or log.stat().st_size + len(text.encode()) > 4 * 1024 * 1024:
                break
            time.sleep(0.2)
        assert success, 'First-use capture did not reach the prepared native module\n' + log.read_text(errors='replace')[-2000:]
        assert receipt.read_bytes() == original, 'Captured receipt differs from verified control'
        assert binary.stat().st_mtime_ns == binary_time, 'Capture rewrote the verified image'
        assert all(path.stat().st_mtime_ns == value for path, value in before.items()), 'Capture rebuilt cached AOT'
        assert 'Original EE payload verified; preparing its native module' in log.read_text(errors='replace')
        print(f'PASS automatic first-use capture and native handoff; retained {len(before)} AOT objects; seconds={time.monotonic() - start:.2f}')
finally:
    if process is not None and process.poll() is None:
        if os.name == 'nt':
            subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'], capture_output=True, timeout=15)
        else:
            os.killpg(process.pid, signal.SIGKILL)
        process.wait(timeout=15)
    # Restore the last valid receipt even when the supervised run fails.
    if backup.exists():
        os.replace(backup, receipt)
